/* Mega-65 emulator, memory handling part (sort of ...)
   Copyright (C)2017 LGB (Gábor Lénárt) <lgblgblgb@gmail.com>

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
#include "memory65.h"
#include "mega65.h"
#include "io65.h"
#include "xemu/cpu65c02.h"
#include "hypervisor.h"
#include "vic3.h"
#include "initial_charset.c"
#include <string.h>



// 128K of "chip-RAM". VIC-IV in M65 can see this, though the last 2K is also covered by the first 2K of the colour RAM.
// that area from chip-RAM cannot be modified by the CPU/DMA/etc though since the colour RAM is there. We emulate anyway
// 128K of chip-RAM so we don't need to check memory access limit all the time in VIC-IV emulation. But it's still true,
// that the last 2K of chip-RAM is a "static" content and not so much useful.
Uint8 chip_ram[0x20000];
// 128K of "fast-RAM". In English, this is C65 ROM, but on M65 you can actually write this area too, and you can use it
// as normal RAM. However VIC-IV cannot see this.
Uint8 fast_ram[0x20000];
// 32K of colour RAM. VIC-IV can see this as for colour information only. The first 2K can be seen at the last 2K of
// the chip-RAM. Also, the first 1 or 2K can be seen in the C64-style I/O area too, at $D800
Uint8 colour_ram[0x8000];
// 16K of hypervisor RAM, can be only seen in hypervisor mode.
Uint8 hypervisor_ram[0x4000];
// 4K of character generator "WOM" (Write-Only-Memory). VIC-IV can read it in the case when ROM based charset would
// be fetched with VIC-II for example. This area can be written in a high-address range, but never read (by CPU).
Uint8 char_wom[0x1000];
// Ethernet buffer, this works that the SAME memory is used, TX is only writable, RX is only readable
Uint8 ethernet_tx_buffer[0x800];
Uint8 ethernet_rx_buffer[0x800];
#ifdef SLOW_RAM_SUPPORT
// 127Mbytes of slow-RAM. Would be the DDR memory on M65/Nexys4
Uint8 slow_ram[127 << 20];
#endif



#define HANDLERS_ADDR_TYPE int
typedef Uint8 (*mem_page_rd_f_type)(HANDLERS_ADDR_TYPE);
typedef void  (*mem_page_wr_f_type)(HANDLERS_ADDR_TYPE, Uint8);

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
#define MEM_SLOT_C64_4KROM_D000 0x220
#define MEM_SLOT_C64_8KROM_E000 0x230
#define MEM_SLOT_C65_8KROM_8000 0x250
#define MEM_SLOT_C65_8KROM_A000 0x270
#define MEM_SLOT_C65_4KROM_C000 0x290
#define MEM_SLOT_C65_8KROM_E000 0x2A0
#define MEM_SLOT_OLD_4K_IO_D000 0x2C0
#define MEM_SLOT_DMA_RD_SRC	0x2D0
#define MEM_SLOT_DMA_WR_SRC	0x2D1
#define MEM_SLOT_DMA_RD_DST	0x2D2
#define MEM_SLOT_DMA_WR_DST	0x2D3
#define MEM_SLOT_DMA_RD_LST	0x2D4
#define MEM_SLOT_CPU_32BIT	0x2D5
#define MEM_SLOT_DEBUG_RESOLVER	0x2D6
#define MEM_SLOTS		0x2D7

static int mem_page_phys[MEM_SLOTS];
static int mem_page_rd_o[MEM_SLOTS];
static int mem_page_wr_o[MEM_SLOTS];
static const struct m65_memory_map_st *mem_page_refp[MEM_SLOTS];
static mem_page_rd_f_type mem_page_rd_f[MEM_SLOTS];
static mem_page_wr_f_type mem_page_wr_f[MEM_SLOTS];
static const struct m65_memory_map_st *mem_page_refp[MEM_SLOTS];

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

static int memcfg_vic3_rom_mapping_last, memcfg_cpu_io_port_last;
static int cpu_io_port[2];
int map_mask, map_offset_low, map_offset_high, map_megabyte_low, map_megabyte_high;
static int map_marker_low, map_marker_high;
int rom_protect;
int skip_unhandled_mem;


static Uint8 zero_physical_page_reader ( int ofs ) {
	return (likely(ofs > 1)) ? chip_ram[ofs] : cpu_io_port[ofs];
}
static void  zero_physical_page_writer ( int ofs, Uint8 data )
{
	if (likely(ofs > 1))
		chip_ram[ofs] = data;
	else
		memory_set_cpu_io_port(ofs, data);
}
static Uint8 chip_ram_reader ( int ofs ) {
	return chip_ram[ofs];
}
static void  chip_ram_writer ( int ofs, Uint8 data ) {
	chip_ram[ofs] = data;
}
static Uint8 fast_ram_reader ( int ofs ) {
	return fast_ram[ofs];
}
static void  fast_ram_writer ( int ofs, Uint8 data ) {
	if (likely(!rom_protect))
		fast_ram[ofs] = data;
}
static Uint8 colour_ram_reader ( int ofs ) {
	return colour_ram[ofs];
}
static void  colour_ram_writer ( int ofs, Uint8 data ) {
	colour_ram[ofs] = data;
}
static Uint8 dummy_reader ( int ofs ) {
	return 0xFF;
}
//static void  dummy_writer ( int ofs, Uint8 data ) {
//}
static Uint8 hypervisor_ram_reader ( int ofs ) {
	return (likely(in_hypervisor)) ? hypervisor_ram[ofs] : 0xFF;
}
static void  hypervisor_ram_writer ( int ofs, Uint8 data ) {
	if (likely(in_hypervisor))
		hypervisor_ram[ofs] = data;
}
static void  char_wom_writer ( int ofs, Uint8 data ) {	// Note: there is NO read for this, as it's write-only memory!
	char_wom[ofs] = data;
}
static Uint8 invalid_mem_reader ( int addr ) {
	if (skip_unhandled_mem)
		DEBUGPRINT("WARNING: Unhandled memory read operation for linear address $%X (PC=$%04X)" NL, addr, cpu_pc);
	else
		FATAL("Unhandled memory read operation for linear address $%X (PC=$%04X)" NL, addr, cpu_pc);
	return 0xFF;
}
static void  invalid_mem_writer ( int addr, Uint8 data ) {
	if (skip_unhandled_mem)
		DEBUGPRINT("WARNING: Unhandled memory write operation for linear address $%X data = $%02X (PC=$%04X)" NL, addr, data, cpu_pc);
	else
		FATAL("Unhandled memory write operation for linear address $%X data = $%02X (PC=$%04X)" NL, addr, data, cpu_pc);
}
static Uint8 fatal_mem_reader ( int addr ) {
	FATAL("Unhandled physical memory mapping on read map");
}
static void  fatal_mem_writer ( int addr, Uint8 data ) {
	FATAL("Unhandled physical memory mapping on write map");
}
static Uint8 unreferenced_mem_reader ( int addr ) {
	FATAL("Unreferenced physical memory mapping on read map");
}
static void  unreferenced_mem_writer ( int addr, Uint8 data ) {
	FATAL("Unreferenced physical memory mapping on write map");
}




// Memory layout table for Mega-65
// Please note, that for optimization considerations, it should be organized in a way
// to have most common entries first, for faster hit in most cases.
static const struct m65_memory_map_st m65_memory_map[] = {
	// 126K chip-RAM (last 2K is not availbale because it's colour RAM), with physical zero page excluded (this is because it needs the CPU port handled with different handler!)
	{ 0x100,	0x1F7FF, chip_ram_reader, chip_ram_writer },
	// the "physical" zero page because of CPU port ...
	{ 0, 0xFF, zero_physical_page_reader, zero_physical_page_writer },
	// 128K of fast-RAM, normally ROM for C65, but can be RAM too!
	{ 0x20000, 0x3FFFF, fast_ram_reader, fast_ram_writer },
	// the last 2K of the first 128K, being the first 2K of the colour RAM (quite nice sentence in my opinion)
	{ 0x1F800, 0x1FFFF, colour_ram_reader, colour_ram_writer },
	// As I/O can be handled quite uniformely, and needs other decoding later anyway, we handle the WHOLE I/O area for all modes in once!
	// This is 16K space, though one 4K is invalid for I/O modes ($FFD2000-$FFD2FFF), the sequence: C64,C65,INVALID,M65 of 4Ks
	// Note, that an "virtual" I/O mode is set after M65 mode in series, used internally to refer for the *current* video mode,
	// though it cannot be mapped or accessed used only in I/O decoder level!!!
	{ 0xFFD0000, 0xFFD3FFF, io_reader_internal_decoder, io_writer_internal_decoder },
	// full colour RAM
	{ 0xFF80000, 0xFF87FFF, colour_ram_reader, colour_ram_writer },		// full colour RAM (32K)
	{ 0xFFF8000, 0xFFFBFFF, hypervisor_ram_reader, hypervisor_ram_writer },	// 16KB Kickstart/hypervisor ROM
	{ 0xFF7E000, 0xFF7EFFF, dummy_reader, char_wom_writer },		// Character "WriteOnlyMemory"
#ifdef SLOW_RAM_SUPPORT
	{ 0x8000000, 0xFEFFFFF, slow_ram_reader, slow_ram_writer },		// 127Mbytes of "slow RAM" (Nexys4 DDR2 RAM)
#endif
	// the last entry *MUST* include the all possible addressing space to "catch" undecoded memory area accesses!!
	{ 0, 0xFFFFFFF, invalid_mem_reader, invalid_mem_writer },
	// even after the last entry :-) to filter out programming bugs, catch all possible even not valid M65 physical address space acceses ...
	{ INT_MIN, INT_MAX, fatal_mem_reader, fatal_mem_writer }
};
// a mapping item which NEVER matches (ie, starting address of region is higher then ending ...)
static const struct m65_memory_map_st impossible_mapping = {
	0x10000001, 0x10000000, unreferenced_mem_reader, unreferenced_mem_writer
};









static void phys_addr_decoder ( int phys, int slot, int prev_slot )
{
	const struct m65_memory_map_st *p;
	phys &= 0xFFFFF00;	// we map only at 256 bytes boundaries!!!! It also helps to wrap around 28 bit M65 addresses TODO/FIXME: is this correct behaviour?
	if (mem_page_phys[slot] == phys)	// kind of "mapping cache" for the given cache slot
		return;				// skip, if the slot already contains info on the current physical address
	mem_page_phys[slot] = phys;
	// tricky part: if prev_slot is non-negative, it's used for "contiunity" information related to this slot,
	// ie check, if the current map request can be fit into the region already mapped by prev_slot, then no
	// need for the search loop. prev_slot can be any slot, but logically it's sane to be used when the given
	// prev_slot is "likely" to have some contiunity with the slot given by "slot" otherwise it's just makes
	// thing worse. If not used, prev_slot should be negative to skip this feature. prev_slot can be even same
	// as "slot" if you need a "moving" mapping in a "caching" slot, ie DMA-aux access functions, etc.
	if (prev_slot >= 0) {
		p = mem_page_refp[prev_slot];
		if (phys >= p->start && phys <= p->end)
			goto found;
	}
	for (p = m65_memory_map; phys < p->start || phys > p->end; p++)
		;
found:
	mem_page_rd_o[slot] = mem_page_wr_o[slot] = phys - p->start;
	mem_page_rd_f[slot] = p->rd_f;
	mem_page_wr_f[slot] = p->wr_f;
	mem_page_refp[slot] = p;
}


static void INLINE phys_addr_decoder_array ( int megabyte_offset, int offset, int slot, int slots, int prev_slot )
{
	for (;;) {
		// we try to use the "prev_slot" feature, which tries to optimize table building with exploiting the
		// fact, that "likely" the next page table entry suits into the same physical decoding "entry" just
		// with different offset (so we don't need to re-walk the memory configuration table)
		phys_addr_decoder(megabyte_offset | (offset & 0xFFFFF), slot, prev_slot);
		if (!--slots)
			return;
		prev_slot = slot++;
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
	for (a = 0; a < MEM_SLOTS; a++) {
		// First of ALL! Initialize mem_page_phys for an impossible value! or otherwise bad crashes would happen ...
		mem_page_phys[a] = 1;	// this is cool enough, since phys addr for this func, can be only 256 byte aligned, so it won't find these ever as cached!
		phys_addr_decoder((a & 0xFF) << 8, a, -1);	// at least we have well defined defaults :) with 'real' and 'virtual' slots as well ...
	}
	// Generate "templates" for VIC-III ROM mapping entry points
	// FIXME: the theory, that VIC-III ROM mapping is not like C64, ie writing a mapped in ROM, would write the ROM, not something "under" as with C64
	// static void INLINE phys_addr_decoder_array ( int megabyte_offset, int offset, int slot, int slots, int prev_slot )
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
	phys_addr_decoder_array(0xFF, 0xD0000, MEM_SLOT_OLD_4K_IO_D000,  16, -1);
	init_helper_custom_memtab_policy(0x4000, NULL, 0x4000, NULL, MEM_SLOT_OLD_4K_IO_D000, 16);
	// Initialize some memory related "caching" stuffs and state etc ...
	memcfg_vic3_rom_mapping_last = 0;
	memcfg_cpu_io_port_last = 0;
	cpu_io_port[0] = 0;
	cpu_io_port[1] = 0;
	map_mask = 0;
	map_offset_low = 0;
	map_offset_high = 0;
	map_megabyte_low = 0;
	map_megabyte_high = 0;
	map_marker_low = 0;
	map_marker_high = 0;
	rom_protect = 0;
	skip_unhandled_mem = 0;
	for (a = 0; a < 9; a++)
		applied_memcfg[a] = 0;	// unmapped marker
	// Setting up the default memory configuration for M65 at least!
	// Note, the exact order is IMPORTANT as being the first use of memory subsystem, actually these will initialize some things ...
	memory_set_cpu_io_port(0, 7);	// must be before the other memory_set stuffs, here at least in init, I mean!
	memory_set_cpu_io_port(1, 7);	// -- "" --
	memory_set_vic3_rom_mapping(0);
	memory_set_do_map();
	// Initiailize memory content with something ...
	memset(chip_ram, 0xFF, sizeof chip_ram);
	memset(fast_ram, 0xFF, sizeof fast_ram);
	memset(colour_ram, 0xFF, sizeof colour_ram);
	memset(hypervisor_ram, 0xFF, sizeof hypervisor_ram);		// this will be overwritten with kickstart, but anyway ...
	memcpy(char_wom, initial_charset, sizeof initial_charset);	// pre-initialize charrom "WOM" with an initial charset
}





static void apply_memory_config_0000_to_7FFF ( void ) {
	int prev = -1;
	// 0000 - 1FFF
	if (map_mask & 0x01) {
		if (applied_memcfg[0] != map_marker_low) {
			phys_addr_decoder_array(map_megabyte_low, map_offset_low, 0x00, 0x20, -1);
			applied_memcfg[0] = map_marker_low;
		}
		prev = 0x1F;
	} else {
		if (applied_memcfg[0]) {
			MEM_TABLE_COPY(0x00, 0x100, 0x20);
			applied_memcfg[0] = 0;
		}
	}
	// 2000 - 3FFF
	if (map_mask & 0x02) {
		if (applied_memcfg[1] != map_marker_low) {
			phys_addr_decoder_array(map_megabyte_low, map_offset_low + 0x2000, 0x20, 0x20, prev);
			applied_memcfg[1] = map_marker_low;
		}
		prev = 0x3F;
	} else {
		if (applied_memcfg[1]) {
			MEM_TABLE_COPY(0x20, 0x120, 0x20);
			applied_memcfg[1] = 0;
		}
	}
	// 4000 - 5FFF
	if (map_mask & 0x04) {
		if (applied_memcfg[2] != map_marker_low) {
			phys_addr_decoder_array(map_megabyte_low, map_offset_low + 0x4000, 0x40, 0x20, prev);
			applied_memcfg[2] = map_marker_low;
		}
		prev = 0x5F;
	} else {
		if (applied_memcfg[2]) {
			MEM_TABLE_COPY(0x40, 0x140, 0x20);
			applied_memcfg[2] = 0;
		}
	}
	// 6000 - 7FFF
	if (map_mask & 0x08) {
		if (applied_memcfg[3] != map_marker_low) {
			phys_addr_decoder_array(map_megabyte_low, map_offset_low + 0x6000, 0x60, 0x20, prev);
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
	if (memcfg_vic3_rom_mapping_last & 8) {
		if (applied_memcfg[4] >= 0) {
			MEM_TABLE_COPY(0x80, MEM_SLOT_C65_8KROM_8000, 0x20);
			applied_memcfg[4] = -1;
		}
	} else if (map_mask & 0x10) {
		if (applied_memcfg[4] != map_marker_high) {
			phys_addr_decoder_array(map_megabyte_high, map_offset_high + 0x8000, 0x80, 0x20, 0x80);
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
	if (memcfg_vic3_rom_mapping_last & 16) {
		if (applied_memcfg[5] >= 0) {
			MEM_TABLE_COPY(0xA0, MEM_SLOT_C65_8KROM_A000, 0x20);
			applied_memcfg[5] = -1;
		}
	} else if (map_mask & 0x20) {
		if (applied_memcfg[5] != map_marker_high) {
			phys_addr_decoder_array(map_megabyte_high, map_offset_high + 0xA000, 0xA0, 0x20, 0xA0);
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
	if (memcfg_vic3_rom_mapping_last & 32) {
		if (applied_memcfg[6] >= 0) {
			MEM_TABLE_COPY(0xC0, MEM_SLOT_C65_8KROM_A000, 0x10);
			applied_memcfg[6] = -1;
		}
	} else if (map_mask & 0x40) {
		if (applied_memcfg[6] != map_marker_high) {
			phys_addr_decoder_array(map_megabyte_high, map_offset_high + 0xC000, 0xC0, 0x10, 0xC0);
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
		if (applied_memcfg[7] >= 0) {
			phys_addr_decoder_array(map_megabyte_high, map_offset_high + 0xD000, 0xD0, 0x10, 0xD0);
			applied_memcfg[7] = -1;
		}
	} else {
		if (applied_memcfg[7] != memcfg_cpu_io_port_policy_D000_to_DFFF) {
			MEM_TABLE_COPY(0xC0, 0x1C0, 0x10);
			applied_memcfg[7] = memcfg_cpu_io_port_policy_D000_to_DFFF;
		}
	}
}
static void apply_memory_config_E000_to_FFFF ( void ) {
	if (memcfg_vic3_rom_mapping_last & 128) {
		if (applied_memcfg[8] >= 0) {
			MEM_TABLE_COPY(0xE0, MEM_SLOT_C65_8KROM_E000, 0x20);
			applied_memcfg[8] = -1;
		}
	} else if (map_mask & 0x40) {
		if (applied_memcfg[8] != map_marker_high) {
			phys_addr_decoder_array(map_megabyte_high, map_offset_high + 0xE000, 0xE0, 0x20, 0xE0);
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
	value &= 0xB8;	// only keep bits we're interested in
	if (value != memcfg_vic3_rom_mapping_last) {	// only do, if there was a change
		Uint8 change = memcfg_vic3_rom_mapping_last ^ value;	// change mask, bits have 1 only if there was a change
		memcfg_vic3_rom_mapping_last = value;	// don't forget to store the current state for next check!
		// now check bits changed in ROM mapping
		if (change &   8)
			apply_memory_config_8000_to_9FFF();
		if (change &  16)
			apply_memory_config_A000_to_BFFF();
		if (change &  32)
			apply_memory_config_C000_to_CFFF();
		if (change & 128)
			apply_memory_config_E000_to_FFFF();
	}
}


static void apply_cpu_io_port ( Uint8 value )
{
	if (value != memcfg_cpu_io_port_last) {
		DEBUG("MEM: CPU I/O port composite value (new one) is %d" NL, value);
		memcfg_cpu_io_port_last = value;
		memcfg_cpu_io_port_policy_A000_to_BFFF = memcfg_cpu_io_port_policies_A000_to_BFFF[value];
		memcfg_cpu_io_port_policy_D000_to_DFFF = memcfg_cpu_io_port_policies_D000_to_DFFF[value];
		memcfg_cpu_io_port_policy_E000_to_FFFF = memcfg_cpu_io_port_policies_E000_to_FFFF[value];
		// check only regions to apply, where CPU I/O port can change anything
		apply_memory_config_A000_to_BFFF();
		apply_memory_config_D000_to_DFFF();
		apply_memory_config_E000_to_FFFF();
	}
}



// must be called on CPU I/O port write, addr=0/1 for DDR/DATA
// do not call with other addr than 0/1!
void memory_set_cpu_io_port ( int addr, Uint8 value )
{
	if (unlikely((addr == 0) && ((value & 0xFE) == 64))) {	// M65-specific speed control stuff!
		value &= 1;
		if (force_fast != value) {
			force_fast = value;
			machine_set_speed(0);
		}
	} else {
		cpu_io_port[addr] = value;
		apply_cpu_io_port((cpu_io_port[1] | (~cpu_io_port[0])) & 7);
	}
}


void memory_set_cpu_io_port_ddr_and_data ( Uint8 p0, Uint8 p1 )
{
	cpu_io_port[0] = p0;
	cpu_io_port[1] = p1;
	apply_cpu_io_port((cpu_io_port[1] | (~cpu_io_port[0])) & 7);
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
	map_marker_low  = (map_megabyte_low  | map_offset_low ) + 0x2000;
	map_marker_high = (map_megabyte_high | map_offset_high) + 0x2000;
	// We need to check every possible memory regions for the effect caused by MAPping ...
	apply_memory_config_0000_to_7FFF();
	apply_memory_config_8000_to_9FFF();
	apply_memory_config_A000_to_BFFF();
	apply_memory_config_C000_to_CFFF();
	apply_memory_config_D000_to_DFFF();
	apply_memory_config_E000_to_FFFF();
}


// This implements the MAP opcode, ie "AUG" in case of 65CE02, which was re-defined to "MAP" in C65's CPU
void cpu_do_aug ( void )
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
	cpu_inhibit_interrupts = 1;	// disable interrupts till the next "EOM" (ie: NOP) opcode
	DEBUG("CPU: MAP opcode, input A=$%02X X=$%02X Y=$%02X Z=$%02X" NL, cpu_a, cpu_x, cpu_y, cpu_z);
	map_offset_low	= (cpu_a << 8) | ((cpu_x & 15) << 16);	// offset of lower half (blocks 0-3)
	map_offset_high	= (cpu_y << 8) | ((cpu_z & 15) << 16);	// offset of higher half (blocks 4-7)
	map_mask	= (cpu_z & 0xF0) | (cpu_x >> 4);	// "is mapped" mask for blocks (1 bit for each)
	// M65 specific "MB" (megabyte) selector "mode":
	if (cpu_x == 0x0F)
		map_megabyte_low  = (int)cpu_a << 20;
	if (cpu_z == 0x0F)
		map_megabyte_high = (int)cpu_y << 20;
	DEBUG("MEM: applying new memory configuration because of MAP CPU opcode" NL);
	DEBUG("LOW -OFFSET = $%03X, MB = $%02X" NL, map_offset_low , map_megabyte_low  >> 20);
	DEBUG("HIGH-OFFSET = $%03X, MB = $%02X" NL, map_offset_high, map_megabyte_high >> 20);
	DEBUG("MASK        = $%02X" NL, map_mask);
	memory_set_do_map();
}



// *** Implements the EOM opcode of 4510, called by the 65CE02 emulator
void cpu_do_nop ( void )
{
	if (cpu_inhibit_interrupts) {
		cpu_inhibit_interrupts = 0;
		DEBUG("CPU: EOM, interrupts were disabled because of MAP till the EOM" NL);
	} else
		DEBUG("CPU: NOP not treated as EOM (no MAP before)" NL);
}


Uint8 cpu_read ( Uint16 addr )
{
	return mem_page_rd_f[addr >> 8](mem_page_rd_o[addr >> 8] + (addr & 0xFF));
}


void  cpu_write ( Uint16 addr, Uint8 data )
{
	mem_page_wr_f[addr >> 8](mem_page_wr_o[addr >> 8] + (addr & 0xFF), data);
}


// Called in case of an RMW (read-modify-write) opcode write access.
// Original NMOS 6502 would write the old_data first, then new_data.
// It has no inpact in case of normal RAM, but it *does* with an I/O register in some cases!
// CMOS line of 65xx (probably 65CE02 as well?) seems not write twice, but read twice.
// However this leads to incompatibilities, as some software used the RMW behavour by intent.
// Thus Mega65 fixed the problem to "restore" the old way of RMW behaviour.
// I also follow this path here, even if it's *NOT* what 65CE02 would do actually!
void cpu_write_rmw ( Uint16 addr, Uint8 old_data, Uint8 new_data )
{
	// TODO; optimize this, enough for I/O range to do this behaviour ...
	register mem_page_wr_f_type f = mem_page_wr_f[addr >> 8];
	register int ofs = mem_page_wr_o[addr >> 8] + (addr & 0xFF);
	f(ofs, old_data);
	f(ofs, new_data);
}


static INLINE int cpu_get_flat_addressing_mode_address ( void )
{
	register int addr = cpu_read(cpu_pc++);	// fetch base page address
	// FIXME: really, BP/ZP is wrapped around in case of linear addressing and eg BP addr of $FF got??????
	// TODO: optimize this maybe to do direct mem_page_* references ...
	return ((
		 cpu_read(cpu_bphi |   addr             )        |
		(cpu_read(cpu_bphi | ((addr + 1) & 0xFF)) <<  8) |
		(cpu_read(cpu_bphi | ((addr + 2) & 0xFF)) << 16) |
		(cpu_read(cpu_bphi | ((addr + 3) & 0xFF)) << 24)
	) + cpu_z) & 0xFFFFFFF;	// FIXME: check if it's really apply here: warps around at 256Mbyte, for address bus of Mega65
}


Uint8 cpu_read_linear_opcode ( void )
{
	int addr = cpu_get_flat_addressing_mode_address();
	Uint8 data;
	phys_addr_decoder(addr, MEM_SLOT_CPU_32BIT, MEM_SLOT_CPU_32BIT);
	data = mem_page_rd_f[MEM_SLOT_CPU_32BIT](mem_page_rd_o[MEM_SLOT_CPU_32BIT] + (addr & 0xFF));
	DEBUG("MEGA65: reading LINEAR memory [PC=$%04X/OPC=$%02X] @ $%X with result $%02X" NL, cpu_old_pc, cpu_op, addr, data);
	return data;
}



void cpu_write_linear_opcode ( Uint8 data )
{
	int addr = cpu_get_flat_addressing_mode_address();
	DEBUG("MEGA65: writing LINEAR memory [PC=$%04X/OPC=$%02X] @ $%X with data $%02X" NL, cpu_old_pc, cpu_op, addr, data);
	phys_addr_decoder(addr, MEM_SLOT_CPU_32BIT, MEM_SLOT_CPU_32BIT);
	mem_page_wr_f[MEM_SLOT_CPU_32BIT](mem_page_wr_o[MEM_SLOT_CPU_32BIT] + (addr & 0xFF), data);
}



Uint8 memory_dma_source_mreader ( int addr )
{
	phys_addr_decoder(addr, MEM_SLOT_DMA_RD_SRC, MEM_SLOT_DMA_RD_SRC);
	return mem_page_rd_f[MEM_SLOT_DMA_RD_SRC](mem_page_rd_o[MEM_SLOT_DMA_RD_SRC] + (addr & 0xFF));
}

void  memory_dma_source_mwriter ( int addr, Uint8 data )
{
	phys_addr_decoder(addr, MEM_SLOT_DMA_WR_SRC, MEM_SLOT_DMA_WR_SRC);
	mem_page_wr_f[MEM_SLOT_DMA_WR_SRC](mem_page_wr_o[MEM_SLOT_DMA_WR_SRC] + (addr & 0xFF), data);
}

Uint8 memory_dma_target_mreader ( int addr )
{
	phys_addr_decoder(addr, MEM_SLOT_DMA_RD_DST, MEM_SLOT_DMA_RD_DST);
	return mem_page_rd_f[MEM_SLOT_DMA_RD_DST](mem_page_rd_o[MEM_SLOT_DMA_RD_DST] + (addr & 0xFF));
}

void  memory_dma_target_mwriter ( int addr, Uint8 data )
{
	phys_addr_decoder(addr, MEM_SLOT_DMA_WR_DST, MEM_SLOT_DMA_WR_DST);
	mem_page_wr_f[MEM_SLOT_DMA_WR_DST](mem_page_wr_o[MEM_SLOT_DMA_WR_DST] + (addr & 0xFF), data);
}

Uint8 memory_dma_list_reader    ( int addr )
{
	phys_addr_decoder(addr, MEM_SLOT_DMA_RD_LST, MEM_SLOT_DMA_RD_LST);
	return mem_page_rd_f[MEM_SLOT_DMA_RD_LST](mem_page_rd_o[MEM_SLOT_DMA_RD_LST] + (addr & 0xFF));
}
