/* A work-in-progess MEGA65 (Commodore 65 clone origins) emulator
   Part of the Xemu project, please visit: https://github.com/lgblgblgb/xemu
   Copyright (C)2017-2019 LGB (Gábor Lénárt) <lgblgblgb@gmail.com>

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
#include "xemu/f018_core.h"
#include "ethernet65.h"
#include "sdcard.h"
#include <string.h>

#define ALLOW_CPU_CUSTOM_FUNCTIONS_INCLUDE
#include "cpu_custom_functions.h"

//#define DEBUGMEM DEBUG
#define SLOW_RAM_SUPPORT 1

// 512K is the max "main" RAM. Currently only 384K is used by M65
Uint8 main_ram[512 << 10];

// Ugly hack for more RAM!
#define chip_ram  (main_ram + 0)
#define fast_ram  (main_ram + 0x20000)
#define extra_ram (main_ram + 0x40000)


// 128K of "chip-RAM". VIC-IV in M65 can see this, though the last 2K is also covered by the first 2K of the colour RAM.
// that area from chip-RAM cannot be modified by the CPU/DMA/etc though since the colour RAM is there. We emulate anyway
// 128K of chip-RAM so we don't need to check memory access limit all the time in VIC-IV emulation. But it's still true,
// that the last 2K of chip-RAM is a "static" content and not so much useful.
//Uint8 chip_ram[0x20000];
// 128K of "fast-RAM". In English, this is C65 ROM, but on M65 you can actually write this area too, and you can use it
// as normal RAM. However VIC-IV cannot see this.
//Uint8 fast_ram[0x20000];
// 32K of colour RAM. VIC-IV can see this as for colour information only. The first 2K can be seen at the last 2K of
// the chip-RAM. Also, the first 1 or 2K can be seen in the C64-style I/O area too, at $D800
Uint8 colour_ram[0x8000];
// Write-Only memory (WOM) for character fetch when it would be the ROM (on C64 eg)
Uint8 char_wom[0x2000];
// 16K of hypervisor RAM, can be only seen in hypervisor mode.
Uint8 hypervisor_ram[0x4000];
#define SLOW_RAM_SIZE (8 << 20)
Uint8 slow_ram[SLOW_RAM_SIZE];


struct m65_memory_map_st {
	int start, end;		// starting and ending physical address of a memory region
	mem_page_rd_f_type rd_f;
	mem_page_wr_f_type wr_f;
};


// table of readers/writers for 256 byte CPU pages are "public" (not static) as will be used
// from inlined function in the CPU emulator core level for better performance.
// The second 256 entries are for unmapped all-RAM cache.
// The extra elements over 0x200 used for ROM and I/O mapping, DMA access decoding, and 32 bit opcodes.

#define MEM_SLOT_C64_8KROM_A000	0x200
#define MEM_SLOT_C64_4KROM_D000	0x220
#define MEM_SLOT_OLD_4K_IO_D000	0x230
#define MEM_SLOT_C64_8KROM_E000	0x240
#define MEM_SLOT_C65_8KROM_8000	0x260
#define MEM_SLOT_C65_8KROM_A000	0x280
#define MEM_SLOT_C65_8KROM_E000	0x2A0
#define MEM_SLOT_C65_4KROM_C000	0x2C0
#define MEM_SLOT_DMA_RD_SRC	0x2D0
#define MEM_SLOT_DMA_WR_SRC	0x2D1
#define MEM_SLOT_DMA_RD_DST	0x2D2
#define MEM_SLOT_DMA_WR_DST	0x2D3
#define MEM_SLOT_DMA_RD_LST	0x2D4
#define MEM_SLOT_CPU_32BIT	0x2D5
#define MEM_SLOT_DEBUG_RESOLVER	0x2D6
#define MEM_SLOTS		0x2D7

#define VIC3_ROM_MASK_8000	0x08
#define VIC3_ROM_MASK_A000	0x10
#define VIC3_ROM_MASK_C000	0x20
#define VIC3_ROM_MASK_E000	0x80

#define MAP_MARKER_DUMMY_OFFSET	0x2000

static int mem_page_phys[MEM_SLOTS] MAXALIGNED;
int mem_page_rd_o[MEM_SLOTS] MAXALIGNED;
int mem_page_wr_o[MEM_SLOTS] MAXALIGNED;
mem_page_rd_f_type mem_page_rd_f[MEM_SLOTS] MAXALIGNED;
mem_page_wr_f_type mem_page_wr_f[MEM_SLOTS] MAXALIGNED;
static const struct m65_memory_map_st *mem_page_refp[MEM_SLOTS];

int cpu_rmw_old_data;

static int applied_memcfg[9];	// not 8, since one slot is actually halved because of CXXX/DXXX handled differently
static int memcfg_cpu_io_port_policy_A000_to_BFFF;
static int memcfg_cpu_io_port_policy_D000_to_DFFF;
static int memcfg_cpu_io_port_policy_E000_to_FFFF;

static const int memcfg_cpu_io_port_policies_A000_to_BFFF[8] = {
	0x1A0, 0x1A0, 0x1A0, MEM_SLOT_C64_8KROM_A000, 0x1A0, 0x1A0, 0x1A0, MEM_SLOT_C64_8KROM_A000
};
static const int memcfg_cpu_io_port_policies_D000_to_DFFF[8] = {
	0x1D0, MEM_SLOT_C64_4KROM_D000, MEM_SLOT_C64_4KROM_D000, MEM_SLOT_C64_4KROM_D000, 0x1D0, MEM_SLOT_OLD_4K_IO_D000, MEM_SLOT_OLD_4K_IO_D000, MEM_SLOT_OLD_4K_IO_D000
};
static const int memcfg_cpu_io_port_policies_E000_to_FFFF[8] = {
	0x1E0, 0x1E0, MEM_SLOT_C64_8KROM_E000, MEM_SLOT_C64_8KROM_E000, 0x1E0, 0x1E0, MEM_SLOT_C64_8KROM_E000, MEM_SLOT_C64_8KROM_E000
};

static Uint8 memcfg_vic3_rom_mapping_last, memcfg_cpu_io_port_last;
static Uint8 cpu_io_port[2];
int map_mask, map_offset_low, map_offset_high, map_megabyte_low, map_megabyte_high;
static int map_marker_low, map_marker_high;
int rom_protect;
int skip_unhandled_mem;


#define DEFINE_READER(name) static Uint8 name ( MEMORY_HANDLERS_ADDR_TYPE )
#define DEFINE_WRITER(name) static void  name ( MEMORY_HANDLERS_ADDR_TYPE, Uint8 data )

DEFINE_READER(zero_physical_page_reader) {
	return (XEMU_LIKELY(GET_OFFSET_BYTE_ONLY() > 1)) ? chip_ram[GET_OFFSET_BYTE_ONLY()] : cpu_io_port[GET_OFFSET_BYTE_ONLY()];
}
DEFINE_WRITER(zero_physical_page_writer)
{
	if (XEMU_LIKELY(GET_OFFSET_BYTE_ONLY() > 1))
		chip_ram[GET_OFFSET_BYTE_ONLY()] = data;
	else
		memory_set_cpu_io_port(GET_OFFSET_BYTE_ONLY(), data);
}
DEFINE_READER(chip_ram_from_page1_reader) {
	return chip_ram[GET_READER_OFFSET() + 0x100];
}
DEFINE_WRITER(chip_ram_from_page1_writer) {
	chip_ram[GET_WRITER_OFFSET() + 0x100] = data;
}
DEFINE_READER(fast_ram_reader) {
	return fast_ram[GET_READER_OFFSET()];
}
DEFINE_WRITER(fast_ram_writer) {
	if (XEMU_LIKELY(!rom_protect))
		fast_ram[GET_WRITER_OFFSET()] = data;
}
DEFINE_READER(extra_ram_reader) {
	return extra_ram[GET_READER_OFFSET()];
}
DEFINE_WRITER(extra_ram_writer) {
	extra_ram[GET_WRITER_OFFSET()] = data;
}
DEFINE_READER(colour_ram_reader) {
	return colour_ram[GET_READER_OFFSET()];
}
DEFINE_WRITER(colour_ram_writer) {
	colour_ram[GET_WRITER_OFFSET()] = data;
	// we also need the update the "real" RAM
	//main_ram[GET_WRITER_OFFSET() & 2047] = data;
}
DEFINE_READER(dummy_reader) {
	return 0xFF;
}
DEFINE_WRITER(dummy_writer) {
}
DEFINE_READER(hypervisor_ram_reader) {
	return (XEMU_LIKELY(in_hypervisor)) ? hypervisor_ram[GET_READER_OFFSET()] : 0xFF;
}
DEFINE_WRITER(hypervisor_ram_writer) {
	if (XEMU_LIKELY(in_hypervisor))
		hypervisor_ram[GET_WRITER_OFFSET()] = data;
}
DEFINE_WRITER(char_wom_writer) {	// Note: there is NO read for this, as it's write-only memory!
	char_wom[GET_WRITER_OFFSET()] = data;
}
DEFINE_READER(slow_ram_reader) {
	return slow_ram[GET_READER_OFFSET()];
}
DEFINE_WRITER(slow_ram_writer) {
	slow_ram[GET_WRITER_OFFSET()] = data;
}
DEFINE_READER(invalid_mem_reader) {
	if (XEMU_LIKELY(skip_unhandled_mem))
		DEBUGPRINT("WARNING: Unhandled memory read operation for linear address $%X (PC=$%04X)" NL, GET_READER_OFFSET(), cpu65.pc);
	else
		FATAL("Unhandled memory read operation for linear address $%X (PC=$%04X)" NL, GET_READER_OFFSET(), cpu65.pc);
	return 0xFF;
}
DEFINE_WRITER(invalid_mem_writer) {
	if (XEMU_LIKELY(skip_unhandled_mem))
		DEBUGPRINT("WARNING: Unhandled memory write operation for linear address $%X data = $%02X (PC=$%04X)" NL, GET_WRITER_OFFSET(), data, cpu65.pc);
	else
		FATAL("Unhandled memory write operation for linear address $%X data = $%02X (PC=$%04X)" NL, GET_WRITER_OFFSET(), data, cpu65.pc);
}
DEFINE_READER(fatal_mem_reader) {
	FATAL("Unhandled physical memory mapping on read map. Xemu software bug?");
}
DEFINE_WRITER(fatal_mem_writer) {
	FATAL("Unhandled physical memory mapping on write map. Xemu software bug?");
}
DEFINE_READER(unreferenced_mem_reader) {
	FATAL("Unreferenced physical memory mapping on read map. Xemu software bug?");
}
DEFINE_WRITER(unreferenced_mem_writer) {
	FATAL("Unreferenced physical memory mapping on write map. Xemu software bug?");
}
DEFINE_READER(m65_io_reader) {
	return io_read(GET_READER_OFFSET());
}
DEFINE_WRITER(m65_io_writer) {
	io_write(GET_WRITER_OFFSET(), data);
}
DEFINE_READER(legacy_io_reader) {
	return io_read(GET_READER_OFFSET() | (vic_iomode << 12));
}
DEFINE_WRITER(legacy_io_writer) {
	io_write(GET_WRITER_OFFSET() | (vic_iomode << 12), data);
}
DEFINE_READER(eth_buffer_reader) {
	return eth65_read_rx_buffer(GET_READER_OFFSET());
}
DEFINE_WRITER(eth_buffer_writer) {
	eth65_write_tx_buffer(GET_WRITER_OFFSET(), data);
}
DEFINE_READER(disk_buffers_reader) {
	return disk_buffers[GET_READER_OFFSET()];
}
DEFINE_WRITER(disk_buffers_writer) {
	disk_buffers[GET_WRITER_OFFSET()] = data;
}
DEFINE_READER(i2c_io_reader) {
	return 0;	// now just ignore, and give ZERO as answer [no I2C devices]
}
DEFINE_WRITER(i2c_io_writer) {
	// now just ignore [no I2C devices]
}

// Memory layout table for Mega-65
// Please note, that for optimization considerations, it should be organized in a way
// to have most common entries first, for faster hit in most cases.
static const struct m65_memory_map_st m65_memory_map[] = {
	// 126K chip-RAM (last 2K is not availbale because it's colour RAM), with physical zero page excluded (this is because it needs the CPU port handled with different handler!)
	{ 0x100,	0x1F7FF, chip_ram_from_page1_reader, chip_ram_from_page1_writer },
	// the "physical" zero page because of CPU port ...
	{ 0, 0xFF, zero_physical_page_reader, zero_physical_page_writer },
	// 128K of fast-RAM, normally ROM for C65, but can be RAM too!
	{ 0x20000, 0x3FFFF, fast_ram_reader, fast_ram_writer },
	{ 0x40000, 0x5FFFF, extra_ram_reader, extra_ram_writer },
	// the last 2K of the first 128K, being the first 2K of the colour RAM (quite nice sentence in my opinion)
	{ 0x1F800, 0x1FFFF, colour_ram_reader, colour_ram_writer },
	// As I/O can be handled quite uniformely, and needs other decoding later anyway, we handle the WHOLE I/O area for all modes in once!
	// This is 16K space, though one 4K is invalid for I/O modes ($FFD2000-$FFD2FFF), the sequence: C64,C65,INVALID,M65 of 4Ks
	{ 0xFFD0000, 0xFFD3FFF, m65_io_reader, m65_io_writer },
	// full colour RAM
	{ 0xFF80000, 0xFF87FFF, colour_ram_reader, colour_ram_writer },		// full colour RAM (32K)
	{ 0xFFF8000, 0xFFFBFFF, hypervisor_ram_reader, hypervisor_ram_writer },	// 16KB Kickstart/hypervisor ROM
	{ 0xFF7E000, 0xFF7FFFF, dummy_reader, char_wom_writer },		// Character "WriteOnlyMemory"
	{ 0xFFDE800, 0xFFDEFFF, eth_buffer_reader, eth_buffer_writer },		// ethernet RX/TX buffer, NOTE: the same address, reading is always the RX_read, writing is always TX_write
	{ 0xFFD6000, 0xFFD6FFF, disk_buffers_reader, disk_buffers_writer },	// disk buffer for SD (can be mapped to I/O space too), F011, and some "3.5K scratch space" [??]
	{ 0xFFD7000, 0xFFD70FF, i2c_io_reader, i2c_io_writer },			// I2C devices
	{ 0x8000000, 0x8000000 + SLOW_RAM_SIZE - 1, slow_ram_reader, slow_ram_writer },		// "slow RAM" also called "hyper RAM" (not to be confused with hypervisor RAM!)
	{ 0x8000000 + SLOW_RAM_SIZE, 0xFDFFFFF, dummy_reader, dummy_writer },			// ununsed big part of the "slow RAM" or so ...
	{ 0x4000000, 0x7FFFFFF, dummy_reader, dummy_writer },		// slow RAM memory area, not exactly known what it's for, let's define as "dummy"
	{ 0x60000, 0xFFFFF, dummy_reader, dummy_writer },			// upper "unused" area of C65 (!) memory map. It seems C65 ROMs want it (Expansion RAM?) so we define as unused.
	// the last entry *MUST* include the all possible addressing space to "catch" undecoded memory area accesses!!
	{ 0, 0xFFFFFFF, invalid_mem_reader, invalid_mem_writer },
	// even after the last entry :-) to filter out programming bugs, catch all possible even not valid M65 physical address space acceses ...
	{ INT_MIN, INT_MAX, fatal_mem_reader, fatal_mem_writer }
};
// a mapping item which NEVER matches (ie, starting address of region is higher then ending ...)
static const struct m65_memory_map_st impossible_mapping = {
	0x10000001, 0x10000000, unreferenced_mem_reader, unreferenced_mem_writer
};








static void phys_addr_decoder ( int phys, int slot, int hint_slot )
{
	const struct m65_memory_map_st *p;
	phys &= 0xFFFFF00;	// we map only at 256 bytes boundaries!!!! It also helps to wrap around 28 bit M65 addresses TODO/FIXME: is this correct behaviour?
	if (mem_page_phys[slot] == phys)	// kind of "mapping cache" for the given cache slot
		return;				// skip, if the slot already contains info on the current physical address
	mem_page_phys[slot] = phys;
	// tricky part: if hint_slot is non-negative, it's used for "contiunity" information related to this slot,
	// ie check, if the current map request can be fit into the region already mapped by hint_slot, then no
	// need for the search loop. hint_slot can be any slot, but logically it's sane to be used when the given
	// hint_slot is "likely" to have some contiunity with the slot given by "slot" otherwise it's just makes
	// thing worse. If not used, hint_slot should be negative to skip this feature. hint_slot can be even same
	// as "slot" if you need a "moving" mapping in a "caching" slot, ie DMA-aux access functions, etc.
	if (hint_slot >= 0 && mem_page_refp[hint_slot]->end < 0xFFFFFFF) {	// FIXME there was a serious bug here, takinking invalid mem slot for anything after hinting that
		p = mem_page_refp[hint_slot];
		if (phys >= p->start && phys <= p->end) {
#ifdef DEBUGMEM
			DEBUGMEM("MEM: PHYS-MAP: slot#$%03X: slot hint TAKEN :)" NL, slot);
#endif
			goto found;
		}
	}
	// Scan the memory map, as not found "cached" result on the same slot, or by the hinting slot
	for (p = m65_memory_map; phys < p->start || phys > p->end; p++)
		;
found:
	mem_page_rd_o[slot] = mem_page_wr_o[slot] = phys - p->start;
	mem_page_rd_f[slot] = p->rd_f;
	mem_page_wr_f[slot] = p->wr_f;
	//if (p->rd_f == invalid_mem_reader)
	//	FATAL("Invalid memory region is tried to be mapped to slot $%X for phys addr $%X" NL, slot, phys);
	mem_page_refp[slot] = p;
#ifdef DEBUGMEM
	DEBUGMEM("MEM: PHYS-MAP: slot#$%03X: phys = $%X mapped (area: $%X-$%X, rd_o=%X, wr_o=%X) [hint slot was: %03X]" NL,
		slot,
		phys,
		p->start, p->end,
		mem_page_rd_o[slot], mem_page_wr_o[slot],
		hint_slot
	);
#endif
}


static void XEMU_INLINE phys_addr_decoder_array ( int megabyte_offset, int offset, int slot, int slots, int hint_slot )
{
	for (;;) {
		// we try to use the "hint_slot" feature, which tries to optimize table building with exploiting the
		// fact, that "likely" the next page table entry suits into the same physical decoding "entry" just
		// with different offset (so we don't need to re-walk the memory configuration table)
		phys_addr_decoder(megabyte_offset | (offset & 0xFFFFF), slot, hint_slot);
		if (!--slots)
			return;
		hint_slot = slot++;
		offset += 0x100;
	}
}


#define MEM_TABLE_COPY(to,from,pages)	do { \
	memcpy(mem_page_rd_o + (to), mem_page_rd_o + (from), sizeof(int) * (pages)); \
	memcpy(mem_page_wr_o + (to), mem_page_wr_o + (from), sizeof(int) * (pages)); \
	memcpy(mem_page_rd_f + (to), mem_page_rd_f + (from), sizeof(mem_page_rd_f_type) * (pages)); \
	memcpy(mem_page_wr_f + (to), mem_page_wr_f + (from), sizeof(mem_page_wr_f_type) * (pages)); \
	memcpy(mem_page_refp + (to), mem_page_refp + (from), sizeof(const struct m65_memory_map_st*) * (pages)); \
	memcpy(mem_page_phys + (to), mem_page_phys + (from), sizeof(int) * (pages)); \
} while (0)



// Not a performance critical function, since it's only needed at emulator init time
static void init_helper_custom_memtab_policy (
	int rd_o,	// custom read-offset to set, apply value -1 for not to change
	mem_page_rd_f_type rd_f,	// custom read-function to set, apply NULL for not to change
	int wr_o,
	mem_page_wr_f_type wr_f,
	int slot,	// starting slot
	int slots	// number of slots
) {
	while (slots--) {
		if (rd_o >= 0) {
			mem_page_rd_o[slot] = rd_o;
			rd_o += 0x100;
		}
		if (rd_f)
			mem_page_rd_f[slot] = rd_f;
		if (wr_o >= 0) {
			mem_page_wr_o[slot] = wr_o;
			wr_o += 0x100;
		}
		if (wr_f)
			mem_page_wr_f[slot] = wr_f;
		mem_page_phys[slot] = 1;	// invalidate phys info, to avoid cache-missbehaviour in decoder
		mem_page_refp[slot] = &impossible_mapping;	// invalidate this too
		slot++;
	}
}


void memory_init ( void )
{
	int a;
	memset(D6XX_registers, 0, sizeof D6XX_registers);
	memset(D7XX, 0xFF, sizeof D7XX);
	rom_protect = 0;
	in_hypervisor = 0;
	for (a = 0; a < MEM_SLOTS; a++) {
		// First of ALL! Initialize mem_page_phys for an impossible value! or otherwise bad crashes would happen ...
		mem_page_phys[a] = 1;	// this is cool enough, since phys addr for this func, can be only 256 byte aligned, so it won't find these ever as cached!
		phys_addr_decoder((a & 0xFF) << 8, a, -1);	// at least we have well defined defaults :) with 'real' and 'virtual' slots as well ...
	}
	// Generate "templates" for VIC-III ROM mapping entry points
	// FIXME: the theory, that VIC-III ROM mapping is not like C64, ie writing a mapped in ROM, would write the ROM, not something "under" as with C64
	// static void XEMU_INLINE phys_addr_decoder_array ( int megabyte_offset, int offset, int slot, int slots, int hint_slot )
	phys_addr_decoder_array(0, 0x38000, MEM_SLOT_C65_8KROM_8000, 32, -1);	// 8K(32 pages) C65 VIC-III ROM mapping ($8000) from $38000
	phys_addr_decoder_array(0, 0x3A000, MEM_SLOT_C65_8KROM_A000, 32, -1);	// 8K(32 pages) C65 VIC-III ROM mapping ($A000) from $3A000
	phys_addr_decoder_array(0, 0x2C000, MEM_SLOT_C65_4KROM_C000, 16, -1);	// 4K(16 pages) C65 VIC-III ROM mapping ($C000) from $2C000
	phys_addr_decoder_array(0, 0x3E000, MEM_SLOT_C65_8KROM_E000, 32, -1);	// 8K(32 pages) C65 VIC-III ROM mapping ($E000) from $3E000
	phys_addr_decoder_array(0, 0x2A000, MEM_SLOT_C64_8KROM_A000, 32, -1);	// 8K(32 pages) C64 CPU I/O ROM mapping ($A000) from $2A000 [C64 BASIC]
	phys_addr_decoder_array(0, 0x2D000, MEM_SLOT_C64_4KROM_D000, 16, -1);	// 4K(16 pages) C64 CPU I/O ROM mapping ($D000) from $2D000 [C64 CHARGEN]
	phys_addr_decoder_array(0, 0x2E000, MEM_SLOT_C64_8KROM_E000, 32, -1);	// 8K(32 pages) C64 CPU I/O ROM mapping ($E000) from $2E000 [C64 KERNAL]
	// C64 ROM mappings by CPU I/O port should be "tuned" for the "write through RAM" policy though ...
	// we re-use some write-specific info for pre-initialized unmapped RAM for write access here
	init_helper_custom_memtab_policy(-1, NULL, mem_page_wr_o[0xA0], mem_page_wr_f[0xA0], MEM_SLOT_C64_8KROM_A000, 32);	// for C64 BASIC ROM
	init_helper_custom_memtab_policy(-1, NULL, mem_page_wr_o[0xD0], mem_page_wr_f[0xD0], MEM_SLOT_C64_4KROM_D000, 16);	// for C64 CHARGEN ROM
	init_helper_custom_memtab_policy(-1, NULL, mem_page_wr_o[0xE0], mem_page_wr_f[0xE0], MEM_SLOT_C64_8KROM_E000, 32);	// for C64 KERNAL ROM
	// The C64/C65-style I/O area is handled in this way: as it is I/O mode dependent unlike M65 high-megabyte areas,
	// we maps I/O (any mode) and "customize it" with an offset to transfer into the right mode (or such).
	phys_addr_decoder_array(0xFF << 20, 0xD0000, MEM_SLOT_OLD_4K_IO_D000,  16, -1);
	init_helper_custom_memtab_policy(-1, legacy_io_reader, -1, legacy_io_writer, MEM_SLOT_OLD_4K_IO_D000, 16);
	// Initialize some memory related "caching" stuffs and state etc ...
	cpu_rmw_old_data = -1;
	memcfg_vic3_rom_mapping_last = 0xFF;
	memcfg_cpu_io_port_last = 0xFF;
	cpu_io_port[0] = 0;
	cpu_io_port[1] = 0;
	map_mask = 0;
	map_offset_low = 0;
	map_offset_high = 0;
	map_megabyte_low = 0;
	map_megabyte_high = 0;
	map_marker_low =  MAP_MARKER_DUMMY_OFFSET;
	map_marker_high = MAP_MARKER_DUMMY_OFFSET;
	skip_unhandled_mem = 0;
	for (a = 0; a < 9; a++)
		applied_memcfg[a] = MAP_MARKER_DUMMY_OFFSET - 1;
	// Setting up the default memory configuration for M65 at least!
	// Note, the exact order is IMPORTANT as being the first use of memory subsystem, actually these will initialize some things ...
	memory_set_cpu_io_port_ddr_and_data(7, 7);
	memory_set_vic3_rom_mapping(0);
	memory_set_do_map();
	// Initiailize memory content with something ...
	memset(main_ram, 0xFF, sizeof main_ram);
	memset(colour_ram, 0xFF, sizeof colour_ram);
#ifdef SLOW_RAM_SUPPORT
	memset(slow_ram, 0xFF, sizeof slow_ram);
#endif
	DEBUG("MEM: End of memory initiailization" NL);
}





static void apply_memory_config_0000_to_7FFF ( void ) {
	int hint_slot = -1;
	// 0000 - 1FFF
	if (map_mask & 0x01) {
		if (applied_memcfg[0] != map_marker_low) {
			phys_addr_decoder_array(map_megabyte_low, map_offset_low, 0x00, 0x20, -1);
			applied_memcfg[0] = map_marker_low;
		}
		hint_slot = 0x1F;
	} else {
		if (applied_memcfg[0]) {
			MEM_TABLE_COPY(0x00, 0x100, 0x20);
			applied_memcfg[0] = 0;
		}
	}
	// 2000 - 3FFF
	if (map_mask & 0x02) {
		if (applied_memcfg[1] != map_marker_low) {
			phys_addr_decoder_array(map_megabyte_low, map_offset_low + 0x2000, 0x20, 0x20, hint_slot);
			applied_memcfg[1] = map_marker_low;
		}
		hint_slot = 0x3F;
	} else {
		if (applied_memcfg[1]) {
			MEM_TABLE_COPY(0x20, 0x120, 0x20);
			applied_memcfg[1] = 0;
		}
	}
	// 4000 - 5FFF
	if (map_mask & 0x04) {
		if (applied_memcfg[2] != map_marker_low) {
			phys_addr_decoder_array(map_megabyte_low, map_offset_low + 0x4000, 0x40, 0x20, hint_slot);
			applied_memcfg[2] = map_marker_low;
		}
		hint_slot = 0x5F;
	} else {
		if (applied_memcfg[2]) {
			MEM_TABLE_COPY(0x40, 0x140, 0x20);
			applied_memcfg[2] = 0;
		}
	}
	// 6000 - 7FFF
	if (map_mask & 0x08) {
		if (applied_memcfg[3] != map_marker_low) {
			phys_addr_decoder_array(map_megabyte_low, map_offset_low + 0x6000, 0x60, 0x20, hint_slot);
			applied_memcfg[3] = map_marker_low;
		}
	} else {
		if (applied_memcfg[3]) {
			MEM_TABLE_COPY(0x60, 0x160, 0x20);
			applied_memcfg[3] = 0;
		}
	}
}
static void apply_memory_config_8000_to_9FFF ( void ) {
	if (memcfg_vic3_rom_mapping_last & VIC3_ROM_MASK_8000) {
		if (applied_memcfg[4] >= 0) {
			MEM_TABLE_COPY(0x80, MEM_SLOT_C65_8KROM_8000, 0x20);
			applied_memcfg[4] = -1;
		}
	} else if (map_mask & 0x10) {
		if (applied_memcfg[4] != map_marker_high) {
			phys_addr_decoder_array(map_megabyte_high, map_offset_high + 0x8000, 0x80, 0x20, -1);
			applied_memcfg[4] = map_marker_high;
		}
	} else {
		if (applied_memcfg[4]) {
			MEM_TABLE_COPY(0x80, 0x180, 0x20);
			applied_memcfg[4] = 0;
		}
	}
}
static void apply_memory_config_A000_to_BFFF ( void ) {
	if (memcfg_vic3_rom_mapping_last & VIC3_ROM_MASK_A000) {
		if (applied_memcfg[5] >= 0) {
			MEM_TABLE_COPY(0xA0, MEM_SLOT_C65_8KROM_A000, 0x20);
			applied_memcfg[5] = -1;
		}
	} else if (map_mask & 0x20) {
		if (applied_memcfg[5] != map_marker_high) {
			phys_addr_decoder_array(map_megabyte_high, map_offset_high + 0xA000, 0xA0, 0x20, -1);
			applied_memcfg[5] = map_marker_high;
		}
	} else {
		if (applied_memcfg[5] != memcfg_cpu_io_port_policy_A000_to_BFFF) {
			MEM_TABLE_COPY(0xA0, memcfg_cpu_io_port_policy_A000_to_BFFF, 0x20);
			applied_memcfg[5] = memcfg_cpu_io_port_policy_A000_to_BFFF;
		}
	}
}
static void apply_memory_config_C000_to_CFFF ( void ) {
	// Special range, just 4K in length!
	if (memcfg_vic3_rom_mapping_last & VIC3_ROM_MASK_C000) {
		if (applied_memcfg[6] >= 0) {
			MEM_TABLE_COPY(0xC0, MEM_SLOT_C65_4KROM_C000, 0x10);
			applied_memcfg[6] = -1;
		}
	} else if (map_mask & 0x40) {
		if (applied_memcfg[6] != map_marker_high) {
			phys_addr_decoder_array(map_megabyte_high, map_offset_high + 0xC000, 0xC0, 0x10, -1);
			applied_memcfg[6] = map_marker_high;
		}
	} else {
		if (applied_memcfg[6]) {
			MEM_TABLE_COPY(0xC0, 0x1C0, 0x10);
			applied_memcfg[6] = 0;
		}
	}
}
static void apply_memory_config_D000_to_DFFF ( void ) {
	// Special range, just 4K in length!
	if (map_mask & 0x40) {
		if (applied_memcfg[7] != map_marker_high) {
			phys_addr_decoder_array(map_megabyte_high, map_offset_high + 0xD000, 0xD0, 0x10, -1);
			applied_memcfg[7] = map_marker_high;
		}
	} else {
		if (applied_memcfg[7] != memcfg_cpu_io_port_policy_D000_to_DFFF) {
			MEM_TABLE_COPY(0xD0, memcfg_cpu_io_port_policy_D000_to_DFFF, 0x10);
			applied_memcfg[7] = memcfg_cpu_io_port_policy_D000_to_DFFF;
		}
	}
}
static void apply_memory_config_E000_to_FFFF ( void ) {
	if (memcfg_vic3_rom_mapping_last & VIC3_ROM_MASK_E000) {
		if (applied_memcfg[8] >= 0) {
			MEM_TABLE_COPY(0xE0, MEM_SLOT_C65_8KROM_E000, 0x20);
			applied_memcfg[8] = -1;
		}
	} else if (map_mask & 0x80) {
		if (applied_memcfg[8] != map_marker_high) {
			phys_addr_decoder_array(map_megabyte_high, map_offset_high + 0xE000, 0xE0, 0x20, -1);
			applied_memcfg[8] = map_marker_high;
		}
	} else {
		if (applied_memcfg[8] != memcfg_cpu_io_port_policy_E000_to_FFFF) {
			MEM_TABLE_COPY(0xE0, memcfg_cpu_io_port_policy_E000_to_FFFF, 0x20);
			applied_memcfg[8] = memcfg_cpu_io_port_policy_E000_to_FFFF;
		}
	}
}





// must be called when VIC-III register $D030 is written, with the written value exactly
void memory_set_vic3_rom_mapping ( Uint8 value )
{
	// D030 regiser of VIC-III is:
	//   7       6       5       4       3       2       1       0
	// | ROM   | CROM  | ROM   | ROM   | ROM   | PAL   | EXT   | CRAM  |
	// | @E000 | @9000 | @C000 | @A000 | @8000 |       | SYNC  | @DC00 |
	if (in_hypervisor)
		value = 0;	// in hypervisor, VIC-III ROM banking should *not* work (newer M65 change)
	else
		value &= VIC3_ROM_MASK_8000 | VIC3_ROM_MASK_A000 | VIC3_ROM_MASK_C000 | VIC3_ROM_MASK_E000;	// only keep bits we're interested in
	if (value != memcfg_vic3_rom_mapping_last) {	// only do, if there was a change
		Uint8 change = memcfg_vic3_rom_mapping_last ^ value;	// change mask, bits have 1 only if there was a change
		DEBUG("MEM: VIC-III ROM mapping change $%02X -> %02X" NL, memcfg_vic3_rom_mapping_last, value);
		memcfg_vic3_rom_mapping_last = value;	// don't forget to store the current state for next check!
		// now check bits changed in ROM mapping
		if (change & VIC3_ROM_MASK_8000)
			apply_memory_config_8000_to_9FFF();
		if (change & VIC3_ROM_MASK_A000)
			apply_memory_config_A000_to_BFFF();
		if (change & VIC3_ROM_MASK_C000)
			apply_memory_config_C000_to_CFFF();
		if (change & VIC3_ROM_MASK_E000)
			apply_memory_config_E000_to_FFFF();
	}
}


static void apply_cpu_io_port_config ( void )
{
	Uint8 desired = (cpu_io_port[1] | (~cpu_io_port[0])) & 7;
	if (desired != memcfg_cpu_io_port_last) {
		DEBUG("MEM: CPUIOPORT: port composite value (new one) is %d" NL, desired);
		memcfg_cpu_io_port_last = desired;
		memcfg_cpu_io_port_policy_A000_to_BFFF = memcfg_cpu_io_port_policies_A000_to_BFFF[desired];
		memcfg_cpu_io_port_policy_D000_to_DFFF = memcfg_cpu_io_port_policies_D000_to_DFFF[desired];
		memcfg_cpu_io_port_policy_E000_to_FFFF = memcfg_cpu_io_port_policies_E000_to_FFFF[desired];
		// check only regions to apply, where CPU I/O port can change anything
		apply_memory_config_A000_to_BFFF();
		apply_memory_config_D000_to_DFFF();
		apply_memory_config_E000_to_FFFF();
		DEBUG("MEM: CPUIOPORT: new config had been applied" NL);
	}
}



// must be called on CPU I/O port write, addr=0/1 for DDR/DATA
// do not call with other addr than 0/1!
void memory_set_cpu_io_port ( int addr, Uint8 value )
{
	if (XEMU_UNLIKELY((addr == 0) && ((value & 0xFE) == 64))) {	// M65-specific speed control stuff!
		value &= 1;
		if (force_fast != value) {
			force_fast = value;
			machine_set_speed(0);
		}
	} else {
		cpu_io_port[addr] = value;
		apply_cpu_io_port_config();
	}
}


void memory_set_cpu_io_port_ddr_and_data ( Uint8 p0, Uint8 p1 )
{
	cpu_io_port[0] = p0;
	cpu_io_port[1] = p1;
	apply_cpu_io_port_config();
}


Uint8 memory_get_cpu_io_port ( int addr )
{
	return cpu_io_port[addr];
}





// Call this after MAP opcode, map_* variables must be pre-initialized
// Can be also used to set custom mapping (hypervisor enter/leave, maybe snapshot loading)
void memory_set_do_map ( void )
{
	// map_marker_low and map_maker_high are just articial markers, not so much to do with real offset, used to
	// detect already done operations. It must be unique for each possible mappings, that is the only rule.
	// to leave room for other values we use both of megabyte info and offset info, but moved from the zero
	// reference (WARNING: mapped from zero and unmapped are different states!) to have place for other markers too.
	map_marker_low  = (map_megabyte_low  | map_offset_low ) + MAP_MARKER_DUMMY_OFFSET;
	map_marker_high = (map_megabyte_high | map_offset_high) + MAP_MARKER_DUMMY_OFFSET;
	// We need to check every possible memory regions for the effect caused by MAPping ...
	apply_memory_config_0000_to_7FFF();
	apply_memory_config_8000_to_9FFF();
	apply_memory_config_A000_to_BFFF();
	apply_memory_config_C000_to_CFFF();
	apply_memory_config_D000_to_DFFF();
	apply_memory_config_E000_to_FFFF();
	DEBUG("MEM: memory_set_do_map() applied" NL);
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
	memory_set_do_map();
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


/* For 32 (28 ...) bit linear addressing we use a dedicated mapper slot. Please read
   command above, similar situation as with the DMA. However we need to fetch the
   base (+Z) from base page, so it can be a bit less efficient if different 32 bit
   pointers used all the time in 4510GS code. */


static XEMU_INLINE int cpu_get_flat_addressing_mode_address ( void )
{
	register int addr = cpu65_read_callback(cpu65.pc++);	// fetch base page address
	// FIXME: really, BP/ZP is wrapped around in case of linear addressing and eg BP addr of $FF got?????? (I think IT SHOULD BE!)
	// FIXME: migrate to cpu_read_paged(), but we need CPU emu core to utilize BP rather than BP << 8, and
	// similar older hacks ...
	return (
		 cpu65_read_callback(cpu65.bphi |   addr             )        |
		(cpu65_read_callback(cpu65.bphi | ((addr + 1) & 0xFF)) <<  8) |
		(cpu65_read_callback(cpu65.bphi | ((addr + 2) & 0xFF)) << 16) |
		(cpu65_read_callback(cpu65.bphi | ((addr + 3) & 0xFF)) << 24)
	) + cpu65.z;	// I don't handle the overflow of 28 bit addr.space situation, as addr will be anyway "trimmed" later in phys_addr_decoder() issued by the user of this func
}

Uint8 cpu65_read_linear_opcode_callback ( void )
{
	register int addr = cpu_get_flat_addressing_mode_address();
	phys_addr_decoder(addr, MEM_SLOT_CPU_32BIT, MEM_SLOT_CPU_32BIT);
	return CALL_MEMORY_READER(MEM_SLOT_CPU_32BIT, addr);
}

void cpu65_write_linear_opcode_callback ( Uint8 data )
{
	register int addr = cpu_get_flat_addressing_mode_address();
	phys_addr_decoder(addr, MEM_SLOT_CPU_32BIT, MEM_SLOT_CPU_32BIT);
	CALL_MEMORY_WRITER(MEM_SLOT_CPU_32BIT, addr, data);
}


// FIXME: very ugly and very slow and maybe very buggy implementation! Should be done in a sane way in the next memory decoder version being developmented ...
Uint32 cpu65_read_linear_long_opcode_callback ( void )
{
	register int addr = cpu_get_flat_addressing_mode_address();
	Uint32 ret = 0;
	for (int a = 0 ;;) {
		phys_addr_decoder(addr, MEM_SLOT_CPU_32BIT, MEM_SLOT_CPU_32BIT);
		ret += CALL_MEMORY_READER(MEM_SLOT_CPU_32BIT, addr);
		if (a == 3)
			return ret;
		addr++;
		ret <<= 8;
		a++;
	}
}

// FIXME: very ugly and very slow and maybe very buggy implementation! Should be done in a sane way in the next memory decoder version being developmented ...
void cpu65_write_linear_long_opcode_callback ( Uint32 data )
{
	register int addr = cpu_get_flat_addressing_mode_address();
	for (int a = 0 ;;) {
		phys_addr_decoder(addr, MEM_SLOT_CPU_32BIT, MEM_SLOT_CPU_32BIT);
		CALL_MEMORY_WRITER(MEM_SLOT_CPU_32BIT, addr, data & 0xFF);
		if (a == 3)
			break;
		addr++;
		data >>= 8;
		a++;
	}
}


/* DMA related call-backs. We use a dedicated memory mapper "slot" for each DMA functions.
   Source can be _written_ too (in case of SWAP operation for example). There are dedicated
   slots for each functionality, so we don't need to re-map physical address again and again,
   and we can take advantage of using the "cache" provided by phys_addr_decoder() which can
   be especially efficient in case of linear operations, what DMA usually does.
   Performance analysis (can be applied to other memory operations somewhat too, even CPU):
   * if the next access for the given DMA func is in the same 256 page, phys_addr_decoder will return after just a comparsion operation
   * if not, the "hint_slot" (3rd paramater) is used, if at least the same physical region (ie also fast-ram, etc) is used, again it's faster than full scan
   * if even the previous statment is not true, phys_addr_decoder will scan the phyisical M65 memory layout to find the region, only */


Uint8 memory_dma_source_mreader ( int addr )
{
	phys_addr_decoder(addr, MEM_SLOT_DMA_RD_SRC, MEM_SLOT_DMA_RD_SRC);
	return CALL_MEMORY_READER(MEM_SLOT_DMA_RD_SRC, addr);
}

void  memory_dma_source_mwriter ( int addr, Uint8 data )
{
	phys_addr_decoder(addr, MEM_SLOT_DMA_WR_SRC, MEM_SLOT_DMA_WR_SRC);
	CALL_MEMORY_WRITER(MEM_SLOT_DMA_WR_SRC, addr, data);
}

Uint8 memory_dma_target_mreader ( int addr )
{
	phys_addr_decoder(addr, MEM_SLOT_DMA_RD_DST, MEM_SLOT_DMA_RD_DST);
	return CALL_MEMORY_READER(MEM_SLOT_DMA_RD_DST, addr);
}

void  memory_dma_target_mwriter ( int addr, Uint8 data )
{
	phys_addr_decoder(addr, MEM_SLOT_DMA_WR_DST, MEM_SLOT_DMA_WR_DST);
	CALL_MEMORY_WRITER(MEM_SLOT_DMA_WR_DST, addr, data);
}

Uint8 memory_dma_list_reader    ( int addr )
{
	phys_addr_decoder(addr, MEM_SLOT_DMA_RD_LST, MEM_SLOT_DMA_RD_LST);
	return CALL_MEMORY_READER(MEM_SLOT_DMA_RD_LST, addr);
}

/* Debugger (ie: [uart]monitor) for reading/writing physical address */
Uint8 memory_debug_read_phys_addr ( int addr )
{
	phys_addr_decoder(addr, MEM_SLOT_DEBUG_RESOLVER, MEM_SLOT_DEBUG_RESOLVER);
	return CALL_MEMORY_READER(MEM_SLOT_DEBUG_RESOLVER, addr);
}

void  memory_debug_write_phys_addr ( int addr, Uint8 data )
{
	phys_addr_decoder(addr, MEM_SLOT_DEBUG_RESOLVER, MEM_SLOT_DEBUG_RESOLVER);
	CALL_MEMORY_WRITER(MEM_SLOT_DEBUG_RESOLVER, addr, data);
}

/* the same as above but for CPU addresses */
Uint8 memory_debug_read_cpu_addr   ( Uint16 addr )
{
	return CALL_MEMORY_READER(addr >> 8, addr);
}

void  memory_debug_write_cpu_addr  ( Uint16 addr, Uint8 data )
{
	CALL_MEMORY_WRITER(addr >> 8, addr, data);
}
