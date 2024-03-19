/* A work-in-progess MEGA65 (Commodore 65 clone origins) emulator
   Part of the Xemu project, please visit: https://github.com/lgblgblgb/xemu
   Copyright (C)2017-2024 LGB (Gábor Lénárt) <lgblgblgb@gmail.com>

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA */


// This source tries to present a bit complex but seems to be optimized solution
// for memory+I/O decoding on M65.
// Currently there is *NO* emulation of different speed (ie wait-states) of given
// memory blocks, and that would slow down the emulation as well to always check
// that information as well.


#include "xemu/emutools.h"
#include "memory_mapper.h"
#include "mega65.h"
#include "io_mapper.h"
#include "xemu/cpu65.h"
#include "hypervisor.h"
#include "vic4.h"
#include "dma65.h"
#include "ethernet65.h"
#include "audio65.h"
#include "sdcard.h"
#include "cart.h"
#include "configdb.h"
#include <string.h>

#define	  ALLOW_CPU_CUSTOM_FUNCTIONS_INCLUDE
#include "cpu_custom_functions.h"
#undef    ALLOW_CPU_CUSTOM_FUNCTIONS_INCLUDE

//#define DEBUGMEM DEBUG
//#define MEM_USE_HINTS

// 512K is the max "main" RAM. Currently only 384K is used by M65. We want to make sure, the _total_ size is power of 2, so we can protect accesses with simple bit masks as a last-resort-protection
Uint8 main_ram[512 << 10];
// 32K of colour RAM. VIC-IV can see this as for colour information only. The first 2K can be seen at the last 2K of
// the chip-RAM. Also, the first 1 or 2K can be seen in the C64-style I/O area too, at $D800
Uint8 colour_ram[0x8000];
// RAM of character fetch source when it would be the ROM (on eg. C64)
// FUN FACT: once it was named as "WOM" (Write-Only-Memory) since CPU could only write it. It's not true anymore though
Uint8 char_ram[0x2000];
// 16K of hypervisor RAM, can be only seen in hypervisor mode.
Uint8 hypervisor_ram[0x4000];
// I2C registers
Uint8 i2c_regs[0x1000];
// Attic RAM, aka hyper-RAM (not to be confused with hypervisor RAM!) aka slow-RAM
Uint8 attic_ram[8 << 20];


static Uint8 cpu_io_port[2];
Uint8 io_mode;			// "VIC" I/O mode: do not change this value directly, use memory_set_io_mode() instead
static bool rom_protect = false;
static Uint32 mem_legacy_io_addr32;	// linear address of the I/O range in use (given by the current I/O mode)
int cpu_rmw_old_data;
Uint32 map_offset_low, map_offset_high, map_megabyte_low, map_megabyte_high;
Uint8  map_mask;
int skip_unhandled_mem = 0;
Uint32 main_ram_size = 0;	// will be set by memory_init() after parsing the memory map

// First 256 slots are for the regular 16 bit CPU addresses, per 256 bytes (256*256=64K)
// All the special ones follows that region:
#define MEM_SLOT_DMA_LIST	0x100
#define MEM_SLOT_DMA_SOURCE	0x101
#define MEM_SLOT_DMA_TARGET	0x102
#define MEM_SLOT_CPU_LINEAR	0x103
// Last "real" slot, must be defined here again with the name MEM_SLOT_LAST_REAL
#define MEM_SLOT_LAST_REAL	0x103
// Not "real" slots
#define MEM_SLOT_DEBUG		0x104
#define MEM_SLOT_SDEBUG		0x105
// must be +1 of the last one, that is: the total number of slots
#define MEM_SLOTS_TOTAL		0x106

// CHECK: If MEM_SLOTS_TOTAL changes, you must modify MEM_SLOTS_TOTAL_EXPORTED in cpu_custom_functions.h to match that!
#if MEM_SLOTS_TOTAL != MEM_SLOTS_TOTAL_EXPORTED
#error "Inconsistency, please modify MEM_SLOTS_TOTAL_EXPORTED in cpu_custom_functions.h to match the value of MEM_SLOTS_TOTAL in memory_mapper.c"
#endif

#define VIC3_ROM_D030_MASK	(0x08U|0x10U|0x20U|0x80U)

// !!!!!
// function pointer types mem_slot_rd_func_t, mem_slot_wr_func_t are in cpu_custom_functions.h

Uint32 mem_slot_rd_addr32[MEM_SLOTS_TOTAL];
Uint32 mem_slot_wr_addr32[MEM_SLOTS_TOTAL];
mem_slot_rd_func_t mem_slot_rd_func[MEM_SLOTS_TOTAL];
mem_slot_wr_func_t mem_slot_wr_func[MEM_SLOTS_TOTAL];
static mem_slot_rd_func_t mem_slot_rd_func_real[MEM_SLOTS_TOTAL];
static mem_slot_wr_func_t mem_slot_wr_func_real[MEM_SLOTS_TOTAL];
#ifdef MEM_USE_DATA_POINTERS
#warning "Feature MEM_USE_DATA_POINTERS is experimental!"
Uint8 *mem_slot_rd_data[MEM_SLOTS_TOTAL];
Uint8 *mem_slot_wr_data[MEM_SLOTS_TOTAL];
#endif

// Very important variable, every slot reader/writer may depend on this to set correctly!
Uint32 ref_slot;

typedef enum {
	MEM_SLOT_TYPE_UNRESOLVED,	// invalidated slots
	MEM_SLOT_TYPE_IMPOSSIBLE,	// should not happen ever! software bug ...
	MEM_SLOT_TYPE_UNDECODED,	// there is corresponding memory area
	MEM_SLOT_TYPE_DUMMY,		// like the above, but should not give you warnings!
	// These can occur only in CPU slots, assigned by the cpu address resolver (NOT the linear one!)
	MEM_SLOT_TYPE_UNMAPPED,
	MEM_SLOT_TYPE_BANKED_ROM,
	MEM_SLOT_TYPE_LEGACY_IO,
	// These can occur only in MAP'ed slots (or in special slots), assigned by the linear address decoder (NOT the CPU's one!)
	MEM_SLOT_TYPE_MAIN_RAM,
	MEM_SLOT_TYPE_ROM,		// maybe normal RAM, if ROM write protection is off
	MEM_SLOT_TYPE_SHARED_RAM,
	MEM_SLOT_TYPE_IO,
	MEM_SLOT_TYPE_COLOUR_RAM,
	MEM_SLOT_TYPE_CHAR_RAM,
	MEM_SLOT_TYPE_ETH_BUFFER,
	MEM_SLOT_TYPE_I2C,
	MEM_SLOT_TYPE_ATTIC_RAM,
	MEM_SLOT_TYPE_SLOW_DEVICES,
	MEM_SLOT_TYPE_OPL3,
	MEM_SLOT_TYPE_1541_RAM,
	// All hypervisor dependent slots must be after this mark! AND MEM_SLOT_TYPE_HYPERVISOR_RAM must be the first!!!!!!!
	// DO not put other kind of slots here!
	MEM_SLOT_TYPE_HYPERVISOR_RAM,
	MEM_SLOT_TYPE_DISK_BUFFER,
} mem_slot_type_t;

static mem_slot_type_t mem_slot_type[MEM_SLOTS_TOTAL];

static Uint32 policy4k_banking[0x10];	// memory policy according to banking only (no MAP effect!): BANK_POLICY_RAM, BANK_POLICY_ROM, BANK_POLICY_IO
static Uint32 policy4k[0x10];		// memory policy with MAP taken account (the real deal!): the same values as above, PLUS special values (see below for the defined values)
// Impossible MAP addresses are used for special purposes above the 28 bit linear address space of MEGA65. BANK_POLICY_INVALID must be the first just above the valid MEGA65 address range (@ $10000000)
#define BANK_POLICY_INVALID	0x10000000U
#define BANK_POLICY_RAM		0x10000001U
#define BANK_POLICY_ROM		0x10000002U
#define BANK_POLICY_IO		0x10000003U


static Uint8 zero_page_reader ( const Uint32 addr32 );
static void  zero_page_writer ( const Uint32 addr32, const Uint8 data );

static Uint8 main_ram_reader ( const Uint32 addr32 ) {
	return main_ram[addr32];
}
static void  main_ram_writer ( const Uint32 addr32, const Uint8 data ) {
	main_ram[addr32] = data;
}
static void  shared_main_ram_writer ( const Uint32 addr32, const Uint8 data ) {
	main_ram[addr32] = data;
	colour_ram[addr32 - 0x1F800U] = data;
}
static void  shared_colour_ram_writer ( const Uint32 addr32, const Uint8 data ) {
	main_ram[addr32 + 0x1F800U - 0xFF80000U] = data;
	colour_ram[addr32 - 0xFF80000U] = data;
}
static Uint8 colour_ram_reader ( const Uint32 addr32 ) {
	return colour_ram[addr32 - 0xFF80000U];
}
static void  colour_ram_writer ( const Uint32 addr32, const Uint8 data ) {
	colour_ram[addr32 - 0xFF80000U] = data;
}
static Uint8 attic_ram_reader ( const Uint32 addr32 ) {
	return attic_ram[addr32 - 0x8000000U];
}
static void  attic_ram_writer ( const Uint32 addr32, const Uint8 data ) {
	attic_ram[addr32 - 0x8000000U] = data;
}
static Uint8 hypervisor_ram_reader ( const Uint32 addr32 ) {
	return hypervisor_ram[addr32 - 0xFFF8000U];
}
static void  hypervisor_ram_writer ( const Uint32 addr32, const Uint8 data ) {
	hypervisor_ram[addr32 - 0xFFF8000U] = data;
}
static void  opl3_writer ( const Uint32 addr32, const Uint8 data ) {
	audio65_opl3_write(addr32 & 0xFFU, data);
}
static Uint8 io_reader ( const Uint32 addr32 ) {
	return io_read(addr32 - 0xFFD0000U);
}
static void  io_writer ( const Uint32 addr32, const Uint8 data ) {
	io_write(addr32 - 0xFFD0000U, data);
}
static Uint8 char_ram_reader ( const Uint32 addr32 ) {
	return char_ram[addr32 - 0xFF7E000U];
}
static void  char_ram_writer ( const Uint32 addr32, const Uint8 data ) {
	char_ram[addr32 - 0xFF7E000U] = data;
}
static Uint8 disk_buffer_hypervisor_reader ( const Uint32 addr32 ) {
	return disk_buffers[addr32 - 0xFFD6000U];
}
static void  disk_buffer_hypervisor_writer ( const Uint32 addr32, const Uint8 data ) {
	disk_buffers[addr32 - 0xFFD6000U] = data;
}
static Uint8 disk_buffer_user_reader ( const Uint32 addr32 ) {
	return disk_buffer_cpu_view[(addr32 - 0xFFD6000U) & 0x1FF];
}
static void  disk_buffer_user_writer ( const Uint32 addr32, const Uint8 data ) {
	disk_buffer_cpu_view[(addr32 - 0xFFD6000U) & 0x1FF] = data;
}
static Uint8 eth_buffer_reader ( const Uint32 addr32 ) {
	return eth65_read_rx_buffer(addr32 - 0xFFDE800U);
}
static void  eth_buffer_writer ( const Uint32 addr32, const Uint8 data ) {
	eth65_write_tx_buffer(addr32 - 0xFFDE800U, data);
}
static Uint8 slow_devices_reader ( const Uint32 addr32 ) {
	return cart_read_byte(addr32 - 0x4000000U);
}
static void  slow_devices_writer ( const Uint32 addr32, const Uint8 data ) {
	cart_write_byte(addr32 - 0x4000000U, data);
}
static Uint8 i2c_reader ( const Uint32 addr32 ) {
	return i2c_regs[addr32 - 0x0FFD7000U];
}
static void  i2c_writer ( const Uint32 addr32, const Uint8 data ) {
	const Uint32 rel = addr32 - 0x0FFD7000U;
	if (rel >= I2C_NVRAM_OFFSET && rel < (I2C_NVRAM_OFFSET + I2C_NVRAM_SIZE)) {
		//DEBUGPRINT("I2C: NVRAM write ($%02X->$%02X) @ NVRAM+$%X" NL, i2c_regs[rel], data, rel - I2C_NVRAM_OFFSET);
		i2c_regs[rel] = data;
	} else if (configdb.mega65_model == 3 && rel >= 0x1D0 && rel <= 0x1EF) {	// Hyppo needs this on PCB R3 for I2C target setup (audio mixer settings)
		// TODO: emulate the mixer stuff
		i2c_regs[rel] = data;
	} else {
		DEBUGPRINT("I2C: unhandled write ($%02X) @ I2C+$%X" NL, data, rel);
	}
}
static Uint8 dummy_reader ( const Uint32 addr32 ) {
	return 0xFF;
}
static void  dummy_writer ( const Uint32 addr32, const Uint8 data ) {
	// well, do nothing
}


static Uint8 undecoded_reader ( const Uint32 addr32 )
{
	char msg[128];
	sprintf(msg, "Unhandled memory read operation for linear address $%X (PC=$%04X)", addr32, cpu65.old_pc);
	if (skip_unhandled_mem <= 1)
		skip_unhandled_mem = QUESTION_WINDOW("EXIT|Ignore now|Ignore all|Silent ignore all", msg);
	switch (skip_unhandled_mem) {
		case 0:
			XEMUEXIT(1);
			break;
		case 1:
		case 2:
			DEBUGPRINT("WARNING: %s" NL, msg);
			break;
		default:
			DEBUG("WARNING: %s" NL, msg);
			break;
	}
	return 0xFF;
}

static void undecoded_writer ( const Uint32 addr32, const Uint8 data )
{
	char msg[128];
	sprintf(msg, "Unhandled memory write operation for linear address $%X (PC=$%04X)", addr32, cpu65.old_pc);
	if (skip_unhandled_mem <= 1)
		skip_unhandled_mem = QUESTION_WINDOW("EXIT|Ignore now|Ignore all|Silent ignore all", msg);
	switch (skip_unhandled_mem) {
		case 0:
			XEMUEXIT(1);
			break;
		case 1:
		case 2:
			DEBUGPRINT("WARNING: %s" NL, msg);
			break;
		default:
			DEBUG("WARNING: %s" NL, msg);
			break;
	}
}


struct mem_map_st {
	Uint32 first;
	Uint32 last;
	mem_slot_type_t type;
};


#ifdef MEM_USE_HINTS
#define MEM_HINT_SLOTS	(MEM_SLOTS_TOTAL - 0x100 + 2)
static const struct mem_map_st *mem_map_hints[MEM_HINT_SLOTS];
#endif

// Rules:
// * Every areas must start in address having the least significant byte as zero
// * Every areas must end in address having the least significant byte as $FF
// * Every areas (other than the first) must start on address which is the previous one's last one PLUS 1
// * The first entry must start at address 0
// * The last entry must be $10000000 - $FFFFFFFF with type MEM_SLOT_TYPE_IMPOSSIBLE as a safe-guard
static struct mem_map_st mem_map[] = {
	{ 0x00000000U, 0x0001F7FFU, MEM_SLOT_TYPE_MAIN_RAM	}, // OLD memory model used 0-FF as "ZP" to handle CPU I/O port. But now it's VIRTUAL and only exists in CPU's view not in mem-map!
	{ 0x0001F800U, 0x0001FFFFU, MEM_SLOT_TYPE_SHARED_RAM	}, // "shared" area, 2K of coulour RAM [C65 legacy], b/c of performance, we must distribute between both of normal and colour RAM
	{ 0x00020000U, 0x0003FFFFU, MEM_SLOT_TYPE_ROM		}, // though it's called ROM, it can be normal RAM, if "ROM write protection" is off
	{ 0x00040000U, 0x0005FFFFU, MEM_SLOT_TYPE_MAIN_RAM	},
	{ 0x00060000U, 0x000FFFFFU, MEM_SLOT_TYPE_DUMMY		}, // upper "unused" area of C65 (!) memory map. It seems C65 ROMs want it (Expansion RAM?) so we define as unused.
	{ 0x00100000U, 0x03FFFFFFU, MEM_SLOT_TYPE_UNDECODED	},
	{ 0x04000000U, 0x07FFFFFFU, MEM_SLOT_TYPE_SLOW_DEVICES	},
	{ 0x08000000U, 0x087FFFFFU, MEM_SLOT_TYPE_ATTIC_RAM	}, // "slow RAM" also called "hyper RAM" (not to be confused with hypervisor RAM!) or "Attic RAM"
	{ 0x08800000U, 0x0FDFFFFFU, MEM_SLOT_TYPE_DUMMY		}, // ununsed big part of the "slow RAM area" or so ...
	{ 0x0FE00000U, 0x0FE000FFU, MEM_SLOT_TYPE_OPL3		},
	{ 0x0FE00100U, 0x0FF7DFFFU, MEM_SLOT_TYPE_UNDECODED	},
	{ 0x0FF7E000U, 0x0FF7FFFFU, MEM_SLOT_TYPE_CHAR_RAM	}, // Character "WriteOnlyMemory" (which is not write-only any more, but it was initially, so the name ...)
	{ 0x0FF80000U, 0x0FF87FFFU, MEM_SLOT_TYPE_COLOUR_RAM	}, // 32K colour RAM
	{ 0x0FF88000U, 0x0FFCFFFFU, MEM_SLOT_TYPE_UNDECODED	},
	{ 0x0FFD0000U, 0x0FFD3FFFU, MEM_SLOT_TYPE_IO		},
	{ 0x0FFD4000U, 0x0FFD5FFFU, MEM_SLOT_TYPE_UNDECODED	},
	{ 0x0FFD6000U, 0x0FFD6FFFU, MEM_SLOT_TYPE_DISK_BUFFER	}, // disk buffer for SD (can be mapped to I/O space too), F011, and some "3.5K scratch space" [??]
	{ 0x0FFD7000U, 0x0FFD7FFFU, MEM_SLOT_TYPE_I2C		}, // I2C devices
	{ 0x0FFD8000U, 0x0FFDAFFFU, MEM_SLOT_TYPE_UNDECODED	},
	{ 0x0FFDB000U, 0x0FFDE7FFU, MEM_SLOT_TYPE_1541_RAM	}, // 1541's 16K ROM + 4K RAM, not so much used currently, but freezer seems to access it, for example ... FIXME: wrong area?!
	{ 0x0FFDE800U, 0x0FFDEFFFU, MEM_SLOT_TYPE_ETH_BUFFER	}, // ethernet RX/TX buffer(s)
	{ 0x0FFDF000U, 0x0FFF7FFFU, MEM_SLOT_TYPE_UNDECODED	},
	{ 0x0FFF8000U, 0x0FFFBFFFU, MEM_SLOT_TYPE_HYPERVISOR_RAM}, // 16KB HYPPO hickup/hypervisor ROM [do not confuse with Hyper-RAM aka Attic-RAM aka Slow-RAM!]
	{ 0x0FFFC000U, 0x0FFFFFFFU, MEM_SLOT_TYPE_UNDECODED	},
	{ 0x10000000U, 0xFFFFFFFFU, MEM_SLOT_TYPE_IMPOSSIBLE	}  // must be the last item! (above 28 bit address space)
};
#define MEM_MAP_SIZE (sizeof(mem_map) / sizeof(struct mem_map_st))

#ifdef MEM_WATCH_SUPPORT
static Uint8 memwatch_reader  ( const Uint32 addr32 );
static void  memwatch_writer  ( const Uint32 addr32, const Uint8 data );
#define MEM_SLOT_WATCHER_READ	1
#define MEM_SLOT_WATCHER_WRITE	2
static Uint8 mem_slot_watcher[MEM_SLOTS_TOTAL];
#endif

static XEMU_INLINE void slot_assignment_postprocessing ( const Uint32 slot )
{
	mem_slot_rd_func_real[slot] = mem_slot_rd_func[slot];
	mem_slot_wr_func_real[slot] = mem_slot_wr_func[slot];
#ifdef	MEM_WATCH_SUPPORT
	//mem_slot_rd_data_real[slot] = mem_slot_rd_data[slot];
	//mem_slot_wr_data_real[slot] = mem_slot_wr_data[slot];
	register const Uint8 watcher = mem_slot_watcher[slot];
	if (XEMU_UNLIKELY(watcher)) {
		if ((watcher & MEM_SLOT_WATCHER_READ)) {
			mem_slot_rd_func[slot] = memwatch_reader;
#ifdef			MEM_USE_DATA_POINTERS
			mem_slot_rd_data[slot] = NULL;
#endif
			if (mem_slot_rd_func_real[slot] == undecoded_reader)
				mem_slot_rd_func_real[slot] = dummy_reader;
		}
		if ((watcher & MEM_SLOT_WATCHER_WRITE)) {
			mem_slot_wr_func[slot] = memwatch_writer;
#ifdef			MEM_USE_DATA_POINTERS
			mem_slot_wr_data[slot] = NULL;
#endif
			if (mem_slot_wr_func_real[slot] == undecoded_writer)
				mem_slot_wr_func_real[slot] = dummy_writer;
		}
	}
#endif
}


// Warning: input linear address must be ranged/normalized (& 0xFFFFF00U) by the caller otherwise bad things will happen (TM)
static void resolve_linear_slot ( const Uint32 slot, const Uint32 addr )
{
#ifdef	MEM_USE_HINTS
	const Uint32 hint_slot = (slot < 0x100U) ? slot >> 7 : slot - 0x100U + 2U;
	const struct mem_map_st *p = mem_map_hints[hint_slot];
#else
	static const struct mem_map_st *p = mem_map;
#endif
	//if (addr < p->first)
	//	p = mem_map;
	for (;;) {
		if (addr > p->last)
			p++;
		else if (addr < p->first)
			p--;
		else
			break;
	}
	//DEBUGPRINT("PHYS-RAM: $%X linear address is mapped into $%X-$%X in slot $%X" NL, addr, p->first, p->last, slot);	// REMOVE
#ifdef	MEM_USE_HINTS
	mem_map_hints[hint_slot] = p;
#endif
	mem_slot_rd_addr32[slot] = mem_slot_wr_addr32[slot] = addr;
#ifdef	MEM_USE_DATA_POINTERS
	mem_slot_rd_data[slot] = mem_slot_wr_data[slot] = NULL;	// by default state: no data pointers, actual case handlers below can override this, of course
#endif
	mem_slot_type[slot] = p->type;
	switch (p->type) {
		case MEM_SLOT_TYPE_MAIN_RAM:
			mem_slot_rd_func[slot] = main_ram_reader;
			mem_slot_wr_func[slot] = main_ram_writer;
#ifdef			MEM_USE_DATA_POINTERS
			mem_slot_rd_data[slot] = mem_slot_wr_data[slot] = main_ram + addr;
#endif
			break;
		case MEM_SLOT_TYPE_ROM:
			mem_slot_rd_func[slot] = main_ram_reader;
			mem_slot_wr_func[slot] = (rom_protect && slot != MEM_SLOT_SDEBUG) ? dummy_writer : main_ram_writer;
#ifdef			MEM_USE_DATA_POINTERS
			mem_slot_rd_data[slot] = main_ram + addr;
			if (!rom_protect || slot == MEM_SLOT_SDEBUG)
				mem_slot_wr_data[slot] = main_ram + addr;
#endif
			break;
		case MEM_SLOT_TYPE_SHARED_RAM:
			mem_slot_rd_func[slot] = main_ram_reader;
			mem_slot_wr_func[slot] = shared_main_ram_writer;
#ifdef			MEM_USE_DATA_POINTERS
			mem_slot_rd_data[slot] = main_ram + addr;	// NOTE: writing does NOT have data pointer, as it requires special care
#endif
			break;
		case MEM_SLOT_TYPE_COLOUR_RAM:
			mem_slot_rd_func[slot] = colour_ram_reader;
			mem_slot_wr_func[slot] = addr >= 0x0FF80800U ? colour_ram_writer : shared_colour_ram_writer;
#ifdef			MEM_USE_DATA_POINTERS
			mem_slot_rd_data[slot] = colour_ram + (addr - 0x0FF80000U);
			if (addr >= 0x0FF80800U)
				mem_slot_wr_data[slot] = colour_ram + (addr - 0x0FF80000U);
#endif
			break;
		case MEM_SLOT_TYPE_ATTIC_RAM:
			mem_slot_rd_func[slot] = attic_ram_reader;
			mem_slot_wr_func[slot] = attic_ram_writer;
#ifdef			MEM_USE_DATA_POINTERS
			mem_slot_rd_data[slot] = mem_slot_wr_data[slot] = attic_ram + (addr - 0x08000000U);
#endif
			break;
		case MEM_SLOT_TYPE_HYPERVISOR_RAM:
			if (XEMU_LIKELY(in_hypervisor || slot == MEM_SLOT_SDEBUG)) {
				mem_slot_rd_func[slot] = hypervisor_ram_reader;
				mem_slot_wr_func[slot] = hypervisor_ram_writer;
#ifdef				MEM_USE_DATA_POINTERS
				mem_slot_rd_data[slot] = mem_slot_wr_data[slot] = hypervisor_ram + (addr - 0xFFF8000U);
#endif
			} else {
				mem_slot_rd_func[slot] = dummy_reader;
				mem_slot_wr_func[slot] = dummy_writer;
			}
			break;
		case MEM_SLOT_TYPE_DISK_BUFFER:
			if (in_hypervisor || slot == MEM_SLOT_SDEBUG) {
				mem_slot_rd_func[slot] = disk_buffer_hypervisor_reader;
				mem_slot_wr_func[slot] = disk_buffer_hypervisor_writer;
			} else {
				mem_slot_rd_func[slot] = disk_buffer_user_reader;
				mem_slot_wr_func[slot] = disk_buffer_user_writer;
			}
			break;
		case MEM_SLOT_TYPE_ETH_BUFFER:
			mem_slot_rd_func[slot] = eth_buffer_reader;
			mem_slot_wr_func[slot] = eth_buffer_writer;
			break;
		case MEM_SLOT_TYPE_OPL3:
			mem_slot_rd_func[slot] = dummy_reader;	// TODO: what should I do here?
			mem_slot_wr_func[slot] = opl3_writer;
			break;
		case MEM_SLOT_TYPE_SLOW_DEVICES:
			mem_slot_rd_func[slot] = slow_devices_reader;
			mem_slot_wr_func[slot] = slow_devices_writer;
			break;
		case MEM_SLOT_TYPE_IO:
			mem_slot_rd_func[slot] = io_reader;
			mem_slot_wr_func[slot] = io_writer;
			break;
		case MEM_SLOT_TYPE_I2C:
			mem_slot_rd_func[slot] = i2c_reader;
			mem_slot_wr_func[slot] = i2c_writer;
			break;
		case MEM_SLOT_TYPE_CHAR_RAM:
			mem_slot_rd_func[slot] = char_ram_reader;
			mem_slot_wr_func[slot] = char_ram_writer;
#ifdef			MEM_USE_DATA_POINTERS
			mem_slot_rd_data[slot] = mem_slot_wr_data[slot] = char_ram + (addr - 0xFF7E000U);
#endif
			break;
		case MEM_SLOT_TYPE_1541_RAM:
			// TODO: Not implemented yet, just here, since freezer accesses this memory area, and without **some** dummy
			// support, it would cause "unhandled memory access" warning in Xemu.
			mem_slot_rd_func[slot] = dummy_reader;
			mem_slot_wr_func[slot] = dummy_writer;
			break;
		case MEM_SLOT_TYPE_DUMMY:
			mem_slot_rd_func[slot] = dummy_reader;
			mem_slot_wr_func[slot] = dummy_writer;
			break;
		case MEM_SLOT_TYPE_UNDECODED:
			if (slot <= MEM_SLOT_LAST_REAL) {
				mem_slot_rd_func[slot] = undecoded_reader;
				mem_slot_wr_func[slot] = undecoded_writer;
			} else {
				mem_slot_rd_func[slot] = dummy_reader;
				mem_slot_wr_func[slot] = dummy_writer;
			}
			break;
		case MEM_SLOT_TYPE_UNMAPPED:
		case MEM_SLOT_TYPE_BANKED_ROM:
		case MEM_SLOT_TYPE_LEGACY_IO:
		case MEM_SLOT_TYPE_UNRESOLVED:
		case MEM_SLOT_TYPE_IMPOSSIBLE:
			FATAL("Impossible address ($%X) or bad slot type (%d) error in %s()", addr, p->type, __func__);
			break;
	}
	slot_assignment_postprocessing(slot);
}


static void resolve_cpu_slot ( const Uint8 slot )
{
	const Uint32 policy = policy4k[slot >> 4];
	//DEBUGPRINT("CPU-ADDR: slot $%X mapping for policy $%X" NL, slot, policy);	// REMOVE
	// !!! if MEM_USE_DATA_POINTERS is used, mem_slot_wr_data and mem_slot_rd_data must be ALWAYS set!
	switch (policy) {
		case BANK_POLICY_RAM:
			mem_slot_type[slot] = MEM_SLOT_TYPE_UNMAPPED;
			if (XEMU_LIKELY(slot)) {
				mem_slot_rd_addr32[slot] = mem_slot_wr_addr32[slot] = slot << 8;
				mem_slot_rd_func[slot] = main_ram_reader;
				mem_slot_wr_func[slot] = main_ram_writer;
#ifdef				MEM_USE_DATA_POINTERS
				mem_slot_rd_data[slot] = mem_slot_wr_data[slot] = main_ram + (slot << 8);
#endif
			} else {
				mem_slot_rd_addr32[0] = mem_slot_wr_addr32[0] = 0U;
				mem_slot_rd_func[0] = zero_page_reader;
				mem_slot_wr_func[0] = zero_page_writer;
#ifdef				MEM_USE_DATA_POINTERS
				mem_slot_rd_data[0] = mem_slot_wr_data[0] = NULL;
#endif
			}
			break;
		case BANK_POLICY_ROM:
			mem_slot_type[slot] = MEM_SLOT_TYPE_BANKED_ROM;
			mem_slot_rd_addr32[slot] = 0x20000U + (slot << 8);
			mem_slot_wr_addr32[slot] =             slot << 8 ;
			mem_slot_rd_func[slot] = main_ram_reader;
			mem_slot_wr_func[slot] = main_ram_writer;
#ifdef			MEM_USE_DATA_POINTERS
			mem_slot_rd_data[slot] = main_ram + 0x20000U + (slot << 8);
			mem_slot_wr_data[slot] = main_ram +            (slot << 8);
#endif
			break;
		case BANK_POLICY_IO:
			mem_slot_type[slot] = MEM_SLOT_TYPE_LEGACY_IO;
			mem_slot_rd_addr32[slot] = mem_slot_wr_addr32[slot] = mem_legacy_io_addr32 + ((slot - 0xD0U) << 8);	// BANK_POLICY_IO can happen only from $D000
			//DEBUGPRINT("Legacy I/O assignment in slot $%X: $%X has been assigned (mem_legacy_io_addr32=$%X)" NL, slot, mem_slot_rd_addr32[slot], mem_legacy_io_addr32); // REMOVE
			mem_slot_rd_func[slot] = io_reader;
			mem_slot_wr_func[slot] = io_writer;
#ifdef			MEM_USE_DATA_POINTERS
			mem_slot_rd_data[slot] = mem_slot_wr_data[slot] = NULL;
#endif
			break;
		default:
			// Some mapping for other "policy" values, using that value (non-negative integer!)
			// mem_slot_* things will be set up by resolve_linear_addr() in this case
			if (XEMU_UNLIKELY(policy >= BANK_POLICY_INVALID))	// these must not happen, which can happen is handled in other "case" branches. FIXME: remove this later? It's a sanity check.
				FATAL("Invalid bank_policy4k[%d >> 4] = $%X in %s()!", slot, policy, __func__);
			resolve_linear_slot(slot, (policy & 0xFF00000U) + ((policy + (slot << 8)) & 0xFFF00U));
			return;	// return, not break! Unlike other cases in this "swhitch" statement. That's important!
	}
	slot_assignment_postprocessing(slot);	// NOT for MAP'ed slots (it has its own code path for that). That's the reason for "return" instead of "break" above
}


// NOTE: since the lazy resolver is intended to resolve the CPU address slot on-demand, this is an exception to the rule:
// the input "addr32" parameter is not really a linear address (the whole purpose here is to GET THAT!) but some fake one.
// Thus only the low 1 byte can be re-used here to form the offset within the "slot" after we resolved the linear start
// address of the slot itself.
static Uint8 lazy_cpu_read_resolver ( const Uint32 addr32 )
{
	resolve_cpu_slot(ref_slot);
	//DEBUGPRINT("LAZY RESOLVER reading at $%X (slot $%X) at PC = $%04X" NL, addr32, ref_slot, cpu65.old_pc);				// REMOVE
	return mem_slot_rd_func[ref_slot](mem_slot_rd_addr32[ref_slot] + (addr32 & 0xFFU));	// re-using the low byte as the offset within the slot
}


// See the comments above with the lazy read resolver!
static void lazy_cpu_write_resolver ( const Uint32 addr32, const Uint8 data )
{
	resolve_cpu_slot(ref_slot);
	//DEBUGPRINT("LAZY RESOLVER writing at $%X (slot $%X) with data $%02X at PC = $%04X" NL, addr32, ref_slot, data, cpu65.old_pc);	// REMOVE
	mem_slot_wr_func[ref_slot](mem_slot_wr_addr32[ref_slot] + (addr32 & 0xFFU), data);	// re-using the low byte as the offset within the slot
}


static inline void invalidate_slot ( const unsigned int slot )
{
	// the value here: signal impossibility: >=28 bit address
	// WARNING: these offsets are added by the CPU memory handling callback, so the lower bytes should be zero!
	mem_slot_rd_addr32[slot] = mem_slot_wr_addr32[slot] = 0x20000000U;
	mem_slot_type[slot] = MEM_SLOT_TYPE_UNRESOLVED;
#ifdef	MEM_USE_DATA_POINTERS
	mem_slot_rd_data[slot] = mem_slot_wr_data[slot] = NULL;
#endif
	if (slot < 0x100U) {
		mem_slot_rd_func[slot] = mem_slot_rd_func_real[slot] = lazy_cpu_read_resolver;
		mem_slot_wr_func[slot] = mem_slot_wr_func_real[slot] = lazy_cpu_write_resolver;
	}
}


static inline void invalidate_slot_range ( unsigned int first_slot, const unsigned int last_slot )
{
	while (first_slot <= last_slot)
		invalidate_slot(first_slot++);
}


static void apply_cpu_memory_policy ( Uint8 i )
{
	for (; i < 0x10U; i++) {
		const Uint32 new_policy = (map_mask & (1U << (i >> 1))) ?
			( (i >= 8) ? map_megabyte_high + map_offset_high : map_megabyte_low + map_offset_low ) : policy4k_banking[i];
		if (new_policy != policy4k[i]) {
			policy4k[i] = new_policy;
			invalidate_slot_range(i << 4, (i << 4) + 0xF);
		}
	}
}


static bool set_banking_config ( Uint8 vic3_d030 )
{
	static Uint8 c64_cfg_old = 0xFF;
	static Uint8 vic3_d030_old = 0xFF;
	const Uint8 c64_cfg = (cpu_io_port[1] | (~cpu_io_port[0])) & 7U;
	vic3_d030 = in_hypervisor ? 0 : vic3_d030 & VIC3_ROM_D030_MASK;
	if (vic3_d030_old == vic3_d030 && c64_cfg_old == c64_cfg)
		return false;
	c64_cfg_old   = c64_cfg;
	vic3_d030_old = vic3_d030;
	// Let's resolve the banking situation, we must fil policy4k_banking at this point (note: the first 32K - ie first 8 entiries of policy4k_banking[]) can never change
	/*    -- Port pin (bit)    $A000 to $BFFF       $D000 to $DFFF       $E000 to $FFFF
	      -- 2 1 0             Read       Write     Read       Write     Read       Write
	      -- --------------    ----------------     ----------------     ----------------
	0     -- 0 0 0             RAM        RAM       RAM        RAM       RAM        RAM
	1     -- 0 0 1             RAM        RAM       CHAR-ROM   RAM       RAM        RAM
	2     -- 0 1 0             RAM        RAM       CHAR-ROM   RAM       KERNAL-ROM RAM
	3     -- 0 1 1             BASIC-ROM  RAM       CHAR-ROM   RAM       KERNAL-ROM RAM
	4     -- 1 0 0             RAM        RAM       RAM        RAM       RAM        RAM
	5     -- 1 0 1             RAM        RAM       I/O        I/O       RAM        RAM
	6     -- 1 1 0             RAM        RAM       I/O        I/O       KERNAL-ROM RAM
	7     -- 1 1 1             BASIC-ROM  RAM       I/O        I/O       KERNAL-ROM RAM

	   D030:	C65 $D030.0 VIC-III:CRAM2K Map 2nd KB of colour RAM @ $DC00-$DFFF
			C65 $D030.1 VIC-III:EXTSYNC Enable external video sync (genlock input)
			C65 $D030.2 VIC-III:PAL Use PALETTE ROM (0) or RAM (1) entries for colours 0 - 15
			C65 $D030.3 VIC-III:ROM8 Map C65 ROM @ $8000
			C65 $D030.4 VIC-III:ROMA Map C65 ROM @ $A000
			C65 $D030.5 VIC-III:ROMC Map C65 ROM @ $C000
			C65 $D030.6 VIC-III:CROM9 Select between C64 and C65 charset.
			C65 $D030.7 VIC-III:ROME Map C65 ROM @ $E000	*/
	policy4k_banking[0x8] = policy4k_banking[0x9] = (vic3_d030 & 0x08)                         ? BANK_POLICY_ROM : BANK_POLICY_RAM ;
	policy4k_banking[0xA] = policy4k_banking[0xB] = (vic3_d030 & 0x10) ||  (c64_cfg & 3) == 3  ? BANK_POLICY_ROM : BANK_POLICY_RAM ;
	policy4k_banking[0xC] =                         (vic3_d030 & 0x20)                         ? BANK_POLICY_ROM : BANK_POLICY_RAM ;
	policy4k_banking[0xD] =                                                (c64_cfg > 4)       ? BANK_POLICY_IO  : (
                                                                               (c64_cfg & 3) == 0  ? BANK_POLICY_RAM : BANK_POLICY_ROM);
	policy4k_banking[0xE] = policy4k_banking[0xF] = (vic3_d030 & 0x80) ||  (c64_cfg & 2)       ? BANK_POLICY_ROM : BANK_POLICY_RAM ;
	return true;
}


int memory_cpu_addr_to_desc ( const Uint16 cpu_addr, char *p, const unsigned int n )
{
	const Uint32 policy = policy4k[cpu_addr >> 12];
	switch (policy) {
		case BANK_POLICY_RAM:
			return snprintf(p, n, "unmap");
		case BANK_POLICY_ROM:
			switch (cpu_addr >> 12) {
				case 0x8: case 0x9:	return snprintf(p, n, "ROM8000");
				case 0xA: case 0xB:	return snprintf(p, n, "BASIC");
				case 0xC:		return snprintf(p, n, "ROMC000");
				case 0xD:		return snprintf(p, n, "CHARGEN");
				case 0xE: case 0xF:	return snprintf(p, n, "KERNAL");
				default:		goto   error;
			}
		case BANK_POLICY_IO:
			return snprintf(p, n, "IO");
		default:
			if (policy >= BANK_POLICY_INVALID)
				goto error;
			return snprintf(p, n, "%X", policy + cpu_addr);
	}
error:
	FATAL("Invalid bank_policy4k[%u >> 12] = $%X in %s()!", cpu_addr, policy, __func__);
	XEMU_UNREACHABLE();
}


Uint32 memory_cpu_addr_to_linear ( const Uint16 cpu_addr, Uint32 *wr_addr_p )
{
	const Uint32 policy = policy4k[cpu_addr >> 12];
	Uint32 wr_addr, rd_addr;
	switch (policy) {
		case BANK_POLICY_RAM:
			rd_addr = wr_addr = cpu_addr;
			break;
		case BANK_POLICY_ROM:
			rd_addr = 0x20000U + cpu_addr;
			wr_addr =            cpu_addr;
			break;
		case BANK_POLICY_IO:
			rd_addr = wr_addr = mem_legacy_io_addr32 + (cpu_addr & 0xFFFU);
			break;
		default:
			if (XEMU_UNLIKELY(policy >= BANK_POLICY_INVALID))
				FATAL("Invalid bank_policy4k[%u >> 12] = $%X in %s()!", cpu_addr, policy, __func__);
			rd_addr = wr_addr = policy + cpu_addr;
			break;
	}
	if (wr_addr_p)
		*wr_addr_p = wr_addr;
	return rd_addr;
}


// This implements the MAP opcode, ie "AUG" in case of 65CE02, which was re-defined to "MAP" in C65's CPU
// M65's extension to select "MB" (ie: megabyte slice, which wraps within!) is supported as well
void cpu65_do_aug_callback ( void )
{
	/*   7       6       5       4       3       2       1       0    BIT
	+-------+-------+-------+-------+-------+-------+-------+-------+
	| LOWER | LOWER | LOWER | LOWER | LOWER | LOWER | LOWER | LOWER | A
	| OFF15 | OFF14 | OFF13 | OFF12 | OFF11 | OFF10 | OFF9  | OFF8  |
	+-------+-------+-------+-------+-------+-------+-------+-------+
	| MAP   | MAP   | MAP   | MAP   | LOWER | LOWER | LOWER | LOWER | X
	| BLK3  | BLK2  | BLK1  | BLK0  | OFF19 | OFF18 | OFF17 | OFF16 |
	+-------+-------+-------+-------+-------+-------+-------+-------+
	| UPPER | UPPER | UPPER | UPPER | UPPER | UPPER | UPPER | UPPER | Y
	| OFF15 | OFF14 | OFF13 | OFF12 | OFF11 | OFF10 | OFF9  | OFF8  |
	+-------+-------+-------+-------+-------+-------+-------+-------+
	| MAP   | MAP   | MAP   | MAP   | UPPER | UPPER | UPPER | UPPER | Z
	| BLK7  | BLK6  | BLK5  | BLK4  | OFF19 | OFF18 | OFF17 | OFF16 |
	+-------+-------+-------+-------+-------+-------+-------+-------+
	-- C65GS extension: Set the MegaByte register for low and high mobies
	-- so that we can address all 256MB of RAM.
	if reg_x = x"0f" then
		reg_mb_low <= reg_a;
	end if;
	if reg_z = x"0f" then
		reg_mb_high <= reg_y;
	end if; */
	cpu65.cpu_inhibit_interrupts = 1;	// disable interrupts till the next "EOM" (ie: NOP) opcode
	DEBUG("CPU: MAP opcode, input A=$%02X X=$%02X Y=$%02X Z=$%02X" NL, cpu65.a, cpu65.x, cpu65.y, cpu65.z);
	map_offset_low	= (cpu65.a <<   8) | ((cpu65.x & 15) << 16);	// offset of lower half (blocks 0-3)
	map_offset_high	= (cpu65.y <<   8) | ((cpu65.z & 15) << 16);	// offset of higher half (blocks 4-7)
	map_mask	= (cpu65.z & 0xF0) | ( cpu65.x >> 4);		// "is mapped" mask for blocks (1 bit for each)
	// M65 specific "MB" (megabyte) selector "mode":
	if (cpu65.x == 0x0F)
		map_megabyte_low  = (int)cpu65.a << 20;
	if (cpu65.z == 0x0F)
		map_megabyte_high = (int)cpu65.y << 20;
	DEBUG("MEM: applying new memory configuration because of MAP CPU opcode" NL);
	DEBUG("LOW -OFFSET = $%03X, MB = $%02X" NL, map_offset_low , map_megabyte_low  >> 20);
	DEBUG("HIGH-OFFSET = $%03X, MB = $%02X" NL, map_offset_high, map_megabyte_high >> 20);
	DEBUG("MASK        = $%02X" NL, map_mask);
	apply_cpu_memory_policy(0);	// take new MAP config into account
}


// *** Implements the EOM opcode of 4510, called by the 65CE02 emulator
void cpu65_do_nop_callback ( void )
{
	if (cpu65.cpu_inhibit_interrupts) {
		cpu65.cpu_inhibit_interrupts = 0;
		DEBUG("CPU: EOM, interrupts were disabled because of MAP till the EOM" NL);
	} else
		DEBUG("CPU: NOP not treated as EOM (no MAP before)" NL);
}


static Uint8 zero_page_reader ( const Uint32 addr32 )
{
	// this could be called only with linear addr (addr32) being in the first 256 byte anyway, so it's OK to use addr32 directly to address cpu_io_port, etc ...
	//DEBUGPRINT("ZERO PAGE READER: query address $%X (mem_slot_rd_addr32[$%X]=$%X) at PC=$%04X" NL, addr32, ref_slot, mem_slot_rd_addr32[ref_slot], cpu65.old_pc);	// REMOVE
	return XEMU_LIKELY(addr32 & 0xFEU) ? main_ram[addr32] : cpu_io_port[addr32];
}


static void zero_page_writer ( const Uint32 addr32, const Uint8 data )
{
	//DEBUGPRINT("ZERO PAGE READER: set address $%X (mem_slot_wr_addr32[$%X]=$%X) to $%X at PC=$%04X" NL, addr32, ref_slot, mem_slot_wr_addr32[ref_slot], data, cpu65.old_pc);	// REMOVE
	if (XEMU_LIKELY(addr32 & 0xFEU)) {
		main_ram[addr32] = data;
	} else {
		if (XEMU_UNLIKELY(!addr32 && (data == 64 || data == 65))) {	// special "magic" values used on MEGA65 to set the speed gate
			if (((D6XX_registers[0x7D] >> 4) ^ data) & 1U) {
				D6XX_registers[0x7D] ^= 16U;
				machine_set_speed(0);
			}
		} else {
			cpu_io_port[addr32] = data;
			if (set_banking_config(vic_registers[0x30]))
				apply_cpu_memory_policy(8);	// start with 4K region 8 (@$8000): banking cannot change the lower 32K
		}
	}
}


Uint8 memory_get_cpu_io_port ( const Uint8 port )
{
	return cpu_io_port[port & 1U];
}


// Re-set linear address of memory slots given to "legacy I/O" which can be only at the range $D000-$DFFF
// Note: this function happily sets I/O mode even in hypervisor! So only call this, if you know, that's OK! [on MEGA65 hypervisor mode _always_ uses MEGA65 I/O mode!]
void memory_set_io_mode ( const Uint8 new_io_mode )
{
	if (io_mode == new_io_mode)
		return;
	io_mode = new_io_mode;
	mem_legacy_io_addr32 = 0xFFD0000U + ((unsigned)(new_io_mode) << 12);	// this value is used at other places as well
	for (Uint8 slot = 0xD0U; slot <= 0xDFU; slot++)
		if (mem_slot_type[slot] == MEM_SLOT_TYPE_LEGACY_IO)		// if it's a resolved legacy I/O slot @ $DXXX, we re-set the corresponding linear address for those
			mem_slot_rd_addr32[slot] = mem_slot_wr_addr32[slot] = mem_legacy_io_addr32 + ((slot - 0xD0U) << 8);
	// TODO: I must think of a solution to the DMA I/O mode, how to handle that on I/O mode changes
}


void memory_write_d030 ( const Uint8 data )
{
	if (set_banking_config(data))
		apply_cpu_memory_policy(8);	// start with 4K region 8 (@$8000): banking cannot change the lower 32K
}


void memory_set_rom_protection ( const bool protect )
{
	if (protect == rom_protect)
		return;
	rom_protect = protect;
	DEBUGPRINT("MEGA65: ROM protection has been turned %s." NL, rom_protect ? "ON" : "OFF");	// FIXME: should I left this "DEBUGPRINT"?
	for (unsigned int slot = 0; slot < MEM_SLOTS_TOTAL; slot++)
		if (mem_slot_type[slot] == MEM_SLOT_TYPE_ROM)
			invalidate_slot(slot);
}


void memory_reconfigure (
	const Uint8 d030_value, const Uint8 new_io_mode, const Uint8 new_cpu_port0, const Uint8 new_cpu_port1,
	const Uint32 new_map_mb_lo, const Uint32 new_map_ofs_lo,
	const Uint32 new_map_mb_hi, const Uint32 new_map_ofs_hi,
	const Uint8 new_map_mask,
	const bool new_in_hypervisor
) {
	if (new_in_hypervisor != in_hypervisor) {
		in_hypervisor = new_in_hypervisor;
		// Invalidate slots, where behaviour is hypervisor/user mode dependent!
		for (unsigned int slot = 0; slot < MEM_SLOTS_TOTAL; slot++)
			if (mem_slot_type[slot] >= MEM_SLOT_TYPE_HYPERVISOR_RAM)	// see the mem_slot_type enum type definition for more details. This is kind of abusing enums ...
				invalidate_slot(slot);
	}
	map_megabyte_low  = new_map_mb_lo;
	map_offset_low    = new_map_ofs_lo;
	map_megabyte_high = new_map_mb_hi;
	map_offset_high   = new_map_ofs_hi;
	map_mask          = new_map_mask;
	cpu_io_port[0] = new_cpu_port0;
	cpu_io_port[1] = new_cpu_port1;
	memory_set_io_mode(new_io_mode);
	(void)set_banking_config(d030_value);
	apply_cpu_memory_policy(0);
}


#define SIZEOF_KILO(b) ((Uint32)sizeof(b) >> 10)


void memory_init (void )
{
	// Check memory map table consistency
	for (unsigned int i = 0, start_at = 0; i < MEM_MAP_SIZE; start_at = mem_map[i++].last + 1U) {
		if (
			mem_map[i].first != start_at || mem_map[i].last <= mem_map[i].first ||
			(mem_map[i].first & 0xFFU) != 0 || (mem_map[i].last & 0xFFU) != 0xFFU ||
			(i == MEM_MAP_SIZE - 1 && (mem_map[i].first != 0x10000000U || mem_map[i].last != 0xFFFFFFFFU || mem_map[i].type != MEM_SLOT_TYPE_IMPOSSIBLE)) ||
			(i != MEM_MAP_SIZE - 1 && (mem_map[i].first == 0x10000000U || mem_map[i].last == 0xFFFFFFFFU || mem_map[i].type == MEM_SLOT_TYPE_IMPOSSIBLE))
		)
			FATAL("INTERNAL XEMU FATAL ERROR: Bad memory decoding table 'mem_map' at entry #%d ($%X-$%X) [should start at $%X]", i, mem_map[i].first, mem_map[i].last, start_at);
		if (mem_map[i].type == MEM_SLOT_TYPE_MAIN_RAM)
			main_ram_size = mem_map[i].last + 1;
	}
	memset(D6XX_registers, 0, sizeof D6XX_registers);
	memset(D7XX, 0xFFU, sizeof D7XX);
	memset(i2c_regs, 0xFFU, sizeof i2c_regs);
	// generate UUID. It will be overwritten anyway in mega65.c if there is i2c backup (not totally new Xemu install)
	for (unsigned int i = I2C_UUID_OFFSET; i < I2C_UUID_OFFSET + I2C_UUID_SIZE; i++)
		i2c_regs[i] = rand();
	memset(i2c_regs + I2C_NVRAM_OFFSET, 0, I2C_NVRAM_SIZE);	// also fill NVRAM area with 0 (FIXME: needed?)
	cart_init();
	rom_protect = false;
	D6XX_registers[0x7D] &= ~4;
	in_hypervisor = false;
#ifdef	MEM_USE_HINTS
	for (unsigned int i = 0; i < MEM_HINT_SLOTS; i++)
		mem_map_hints[i] = mem_map;
#endif
	for (unsigned int i = 0; i < 0x10; i++) {
		policy4k[i] = BANK_POLICY_INVALID;
		policy4k_banking[i] = (i >= 8) ? BANK_POLICY_INVALID : BANK_POLICY_RAM;	// lower 32K cannot be banked ever, but we need the value of BANK_POLICY_RAM to simplify logic
	}
#ifdef	MEM_WATCH_SUPPORT
	memset(mem_slot_watcher, 0, sizeof mem_slot_watcher);	// initially no watchers at all
#endif
	invalidate_slot_range(0, MEM_SLOTS_TOTAL - 1);	// make sure we have a consistent state
	cpu_rmw_old_data = -1;
	vic_registers[0x30] &= ~VIC3_ROM_D030_MASK;
	memory_reconfigure(
		0,		// $D030 ROM banking
		VIC2_IOMODE,
		0, 0,		// some initial CPU I/O port values
		0, 0, 0, 0,	// MAP lo/hi mb/ofs
		0,		// MAP mask
		false		// not-hypervisor
	);
	// Initiailize memory content with something ...
	// NOTE: make sure the first 2K of colour_ram is the **SAME** as the 2K part of main_ram at offset $1F800
	memset(main_ram,   0x00, sizeof main_ram);
	memset(colour_ram, 0x00, sizeof colour_ram);
	memset(attic_ram,  0xFF, sizeof attic_ram);
	DEBUGPRINT("MEM: memory decoder initialized, %uK fast, %uK attic, %uK colour, %uK font RAM" NL,
		main_ram_size >> 10,
		SIZEOF_KILO(attic_ram),
		SIZEOF_KILO(colour_ram),
		SIZEOF_KILO(char_ram)
	);
}


// Warning: this overwrites ref_slot!
static XEMU_INLINE Uint32 cpu_get_flat_addressing_mode_address ( Uint32 index )
{
	// FIXME: really, BP/ZP is wrapped around in case of linear addressing and eg BP addr of $FF got?????? (I think IT SHOULD BE!)
	Uint8 bp_addr = cpu65_read_callback(cpu65.pc++);	// fetch base page address (we plays the role of the CPU here)
	ref_slot = cpu65.bphi >> 8;				// basically the page number of the base page, what BP would mean (however CPU65 emulator uses BPHI ... the offset of the base page ...)
	if (mem_slot_type[ref_slot] == MEM_SLOT_TYPE_UNRESOLVED)	// make sure the slot is resolved, otherwise the query of rd_base_addr below can be invalid before the first read!!!!!!!
		resolve_cpu_slot(ref_slot);
	const Uint32 rd_base_addr = mem_slot_rd_addr32[ref_slot];
	index += mem_slot_rd_func[ref_slot](rd_base_addr + bp_addr++)      ;
	index += mem_slot_rd_func[ref_slot](rd_base_addr + bp_addr++) <<  8;
	index += mem_slot_rd_func[ref_slot](rd_base_addr + bp_addr++) << 16;
	index += mem_slot_rd_func[ref_slot](rd_base_addr + bp_addr  ) << 24;
	return index;
}


static XEMU_INLINE void resolve_special_rd_slot_on_demand ( const Uint32 slot, Uint32 addr32 )
{
	addr32 &= 0xFFFFF00U;
	if (XEMU_UNLIKELY(addr32 ^ mem_slot_rd_addr32[slot]))
		resolve_linear_slot(slot, addr32);
}


static XEMU_INLINE void resolve_special_wr_slot_on_demand ( const Uint32 slot, Uint32 addr32 )
{
	addr32 &= 0xFFFFF00U;
	if (XEMU_UNLIKELY(addr32 ^ mem_slot_wr_addr32[slot]))
		resolve_linear_slot(slot, addr32);
}


Uint8 cpu65_read_linear_opcode_callback ( void )
{
	//DEBUGPRINT("cpu65_read_linear_opcode_callback fires at PC=$%04X" NL, cpu65.old_pc);		// REMOVE
	register const Uint32 addr32 = cpu_get_flat_addressing_mode_address(cpu65.z) & 0xFFFFFFFU;
	//DEBUGPRINT("cpu65_read_linear_opcode_callback fires-2 at PC=$%04X" NL, cpu65.old_pc);		// REMOVE
	//DEBUGPRINT("cpu65_read_linear_opcode_callback: about to call read for addr = $%02X at PC=$%04X" NL, addr32, cpu65.old_pc);	// REMOVE
	resolve_special_rd_slot_on_demand(MEM_SLOT_CPU_LINEAR, addr32);
	ref_slot = MEM_SLOT_CPU_LINEAR;
	return mem_slot_rd_func[MEM_SLOT_CPU_LINEAR](addr32);
}


void cpu65_write_linear_opcode_callback ( const Uint8 data )
{
	register const Uint32 addr32 = cpu_get_flat_addressing_mode_address(cpu65.z) & 0xFFFFFFFU;
	resolve_special_wr_slot_on_demand(MEM_SLOT_CPU_LINEAR, addr32);
	ref_slot = MEM_SLOT_CPU_LINEAR;
	mem_slot_wr_func[MEM_SLOT_CPU_LINEAR](addr32, data);
}


Uint32 cpu65_read_linear_long_opcode_callback ( const Uint8 index )
{
	Uint32 addr32 = cpu_get_flat_addressing_mode_address(index);
	ref_slot = MEM_SLOT_CPU_LINEAR;
	resolve_special_rd_slot_on_demand(MEM_SLOT_CPU_LINEAR,   addr32);
	Uint32 data =   mem_slot_rd_func[MEM_SLOT_CPU_LINEAR](addr32 & 0xFFFFFFFU) ;
	resolve_special_rd_slot_on_demand(MEM_SLOT_CPU_LINEAR, ++addr32);
	data += (Uint32)mem_slot_rd_func[MEM_SLOT_CPU_LINEAR](addr32 & 0xFFFFFFFU) <<  8;
	resolve_special_rd_slot_on_demand(MEM_SLOT_CPU_LINEAR, ++addr32);
	data += (Uint32)mem_slot_rd_func[MEM_SLOT_CPU_LINEAR](addr32 & 0xFFFFFFFU) << 16;
	resolve_special_rd_slot_on_demand(MEM_SLOT_CPU_LINEAR, ++addr32);
	data += (Uint32)mem_slot_rd_func[MEM_SLOT_CPU_LINEAR](addr32 & 0xFFFFFFFU) << 24;
	return data;

}


void cpu65_write_linear_long_opcode_callback ( const Uint8 index, const Uint32 data )
{
	Uint32 addr32 = cpu_get_flat_addressing_mode_address(index);
	ref_slot = MEM_SLOT_CPU_LINEAR;
	resolve_special_wr_slot_on_demand(MEM_SLOT_CPU_LINEAR,   addr32);
	mem_slot_wr_func[MEM_SLOT_CPU_LINEAR](addr32 & 0xFFFFFFFU,  data        & 0xFFU);
	resolve_special_wr_slot_on_demand(MEM_SLOT_CPU_LINEAR, ++addr32);
	mem_slot_wr_func[MEM_SLOT_CPU_LINEAR](addr32 & 0xFFFFFFFU, (data >>  8) & 0xFFU);
	resolve_special_wr_slot_on_demand(MEM_SLOT_CPU_LINEAR, ++addr32);
	mem_slot_wr_func[MEM_SLOT_CPU_LINEAR](addr32 & 0xFFFFFFFU, (data >> 16) & 0xFFU);
	resolve_special_wr_slot_on_demand(MEM_SLOT_CPU_LINEAR, ++addr32);
	mem_slot_wr_func[MEM_SLOT_CPU_LINEAR](addr32 & 0xFFFFFFFU, (data >> 24) & 0xFFU);
}


#define CREATE_LINEAR_READER(name,slot) \
	Uint8 name ( const Uint32 addr32 ) { \
		resolve_special_rd_slot_on_demand(slot, addr32); \
		ref_slot = slot; \
		return mem_slot_rd_func[slot](addr32 & 0xFFFFFFFU); \
	}

#define CREATE_LINEAR_WRITER(name,slot) \
	void name ( const Uint32 addr32, const Uint8 data ) { \
		resolve_special_wr_slot_on_demand(slot, addr32); \
		ref_slot = slot; \
		mem_slot_wr_func[slot](addr32 & 0xFFFFFFFU, data); \
	}

CREATE_LINEAR_READER(memory_dma_list_reader,    MEM_SLOT_DMA_LIST)
CREATE_LINEAR_READER(memory_dma_source_mreader, MEM_SLOT_DMA_SOURCE)
CREATE_LINEAR_WRITER(memory_dma_source_mwriter, MEM_SLOT_DMA_SOURCE)
CREATE_LINEAR_READER(memory_dma_target_mreader, MEM_SLOT_DMA_TARGET)
CREATE_LINEAR_WRITER(memory_dma_target_mwriter, MEM_SLOT_DMA_TARGET)
CREATE_LINEAR_READER(debug_read_linear_byte,    MEM_SLOT_DEBUG)
CREATE_LINEAR_WRITER(debug_write_linear_byte,   MEM_SLOT_DEBUG)
CREATE_LINEAR_READER(sdebug_read_linear_byte,   MEM_SLOT_SDEBUG)
CREATE_LINEAR_WRITER(sdebug_write_linear_byte,  MEM_SLOT_SDEBUG)


Uint8 debug_read_cpu_byte  ( const Uint16 addr16 )
{
	ref_slot = addr16 >> 8;
	return mem_slot_rd_func_real[ref_slot](mem_slot_rd_addr32[ref_slot] + (addr16 & 0xFFU));
}

void  debug_write_cpu_byte ( const Uint16 addr16, const Uint8 data )
{
	ref_slot = addr16 >> 8;
	mem_slot_wr_func_real[ref_slot](mem_slot_wr_addr32[ref_slot] + (addr16 & 0xFFU), data);
}

#ifdef MEM_WATCH_SUPPORT
static Uint8 memwatch_reader  ( const Uint32 addr32 )
{
	Uint8 data = mem_slot_rd_func_real[ref_slot](addr32);
	for (unsigned int i = 0, cpu_addr = (ref_slot << 8) + (addr32 & 0xFFU); i < mem_watchers.cpu_read_nums; i++)
		if (cpu_addr >= w->cpu_begin && cpu_addr <= w->cpu_end && addr32 >= w->lin_begin && addr32 <= w->lin_end)
			return watchmem_read_callback(i, addr32, cpu_addr, data);
	return data;



	if (mem_watchers.cpu_read_nums && ref_slot < 0x100U)
		for (unsigned int i = 0, cpu_addr = (ref_slot << 8) + (addr32 & 0xFFU); i < mem_watchers.cpu_read_nums; i++)
			if (cpu_addr >= mem_watchers.cpu_read_list[i].begin && cpu_addr <= mem_watchers.cpu_read_list[i].end)
				return memwatch_cpu_read_callback(i, addr32, cpu_addr, data);
	if (mem_watchers.lin_read_nums)
		for (unsigned int i = 0; i < mem_watchers.lin_read_nums; i++)
			if (addr32 >= mem_watchers.lin_read_list[i].begin && addr32 <= mem_watchers.lin_read_list[i].end)
				return memwatch_linear_read_callback(i, addr32, (ref_slot << 8) + (addr32 & 0xFFU), data);
	return data;
}

static void  memwatch_writer  ( const Uint32 addr32, const Uint8 data )
{
	if (mem_watchers_cpu_addr_read && ref_slot < 0x100U) {
		const Uint16 cpu_addr = (ref_slot << 8) + (addr32 & 0xFFU);
		for (unsigned int i = 0; i < mem_watchers_cpu_addr_read; i++)
			if (mem_watchers_cpu_addr_read_list[i] == cpu_addr) {
				data = debug_memwatch_cpu_read(addr32, cpu_addr, data);
				break;
			}
	}



	// TODO: if memwatch-on-write is enabled for the slot - mem_slot_watcher[slot] & MEM_SLOT_WATCHER_WRITE is non-zero - then we should do something here
	// mem_slot_wr_func_real[ref_slot](addr32, debug_memwatch_wrtie_callback(addr32, ref_slot, data));
	mem_slot_wr_func_real[ref_slot](addr32, data);
}
#endif
