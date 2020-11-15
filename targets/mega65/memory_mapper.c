/* A work-in-progess MEGA65 (Commodore 65 clone origins) emulator
   Part of the Xemu project, please visit: https://github.com/lgblgblgb/xemu
   Copyright (C)2017-2020 LGB (Gábor Lénárt) <lgblgblgb@gmail.com>

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


#include "xemu/emutools.h"
#include "mega65.h"
#include "vic4.h"
#include "xemu/cpu65.h"
#include "hypervisor.h"

#define VIC3_ROM_MASK_8000	0x08
#define VIC3_ROM_MASK_A000	0x10
#define VIC3_ROM_MASK_C000	0x20
#define VIC3_ROM_MASK_E000	0x80



// if defined, C64 style unmapped slots are filled for the whole area, not just the on-demand reference
// in theory it can cause better performance (fewer resolve events) at the cost though to have
// more time to do it, even if it's not needed. So it's question of balance which performs better in practice.
#define DO_FULL_LEGACY_MAPPINGS
// If defined, scanning the decoder table is bi-directional. If undefined, only upwards scan is done,
// and if the intial linear address is below the current supplied one, the upwards-scan is done from
// beginning of the table, rather than downwards step-by-step one (opposite of the upwards process)
#define MEMDEC_BIDIRECTIONAL_TABLE_SCAN

#define MAIN_RAM_SIZE		((256+128)<<10)
#define SLOW_RAM_SIZE		(8<<20)
#define COLOUR_RAM_SIZE		0x8000
#define HYPERVISOR_RAM_SIZE	0x4000

#define BRAM_INIT_PATTERN		0x00
#define CRAM_INIT_PATTERN		0x00
#define SLOWRAM_INIT_PATTERN		0x00
#define MEMORY_UNDECODED_PATTERN	0xFF
//#define MEMORY_IGNORED_PATTERN		0xFF

int skip_unhandled_mem = 0;
int rom_protect = 0;
int legacy_io_is_mapped = 0;
unsigned int map_mask, map_offset_low, map_offset_high, map_megabyte_low, map_megabyte_high;

Uint8 main_ram[MAIN_RAM_SIZE];
Uint8 slow_ram[SLOW_RAM_SIZE];
Uint8 hypervisor_ram[HYPERVISOR_RAM_SIZE];
Uint8 colour_ram[COLOUR_RAM_SIZE];
Uint8 c64_colour_ram[2048];	// some trick to speed up C64-style "4 bit colour RAM" access. For more information check comments at colour_ram_head_writer() [inc. the fact why 2K when it's C64 ...]
Uint8 black_hole[0x100];	// memory area functions as "no write here" (just swallows everything)
Uint8 white_hole[0x100];	// memory area functions as "no read here" (source some fixed value)
Uint8 cpu_io_port[2];		// C64-style "CPU I/O port" @ addr 0 and 1 (which is part of VIC-III on C65, but this fact does not matter too much for us now)


// Memory channels are special things, not for CPU access, but for subsystems needs "linear address"
// access to the full address space. Like: DMA, CPU (but linear addressing opcodes!), and debugger.
#define DMA_LIST_MEMORY_CHANNEL		0
#define DMA_SOURCE_MEMORY_CHANNEL	1
#define DMA_TARGET_MEMORY_CHANNEL	2
#define CPU_LINADDR_MEMORY_CHANNEL	3
#define DEBUGGER_MEMORY_CHANNEL		4
#define MAX_MEMORY_CHANNELS		5

enum memdec_pol_en {
	MEMDEC_NORMAL_POLICY,
	MEMDEC_ROM_POLICY,
	MEMDEC_HYPERVISOR_POLICY,
	MEMDEC_IOREGION_POLICY
};

typedef Uint8 rd_f_t(unsigned int slot, unsigned int addr);
typedef void  wr_f_t(unsigned int slot, unsigned int addr, Uint8 data);

struct linear_access_decoder_st {
	Uint32	begin, end;		// begin and end of memory region, begin should be 256 byte page algined, end must have 0xFF as the two last hex digits
	Uint8	*rd_data;		// data pointer to the memory region for reading, if direct access is possible, OR must be NULL
	rd_f_t	*rd_func;		// if rd_data is NULL, this function pointer is used instead
	Uint8	*wr_data;		// the same as rd_data, just for writing
	wr_f_t	*wr_func;		// the same as rd_func, just for writing
	enum	memdec_pol_en policy;	// memory page setup policy
};

#define INVALIDATED_MEMORY_CHANNEL	1	// since info must be 256 byte aligned, thus having 1 (not aligned) signals that it's invalid
Uint32 memory_channel_last_usage[MAX_MEMORY_CHANNELS];
const struct linear_access_decoder_st *memory_channel_last_dectab_p[MAX_MEMORY_CHANNELS];

Uint8  *mem_rd_data_p[MAX_MEMORY_CHANNELS + 0x100];
Uint8  *mem_wr_data_p[MAX_MEMORY_CHANNELS + 0x100];
rd_f_t *mem_rd_func_p[MAX_MEMORY_CHANNELS + 0x100];
wr_f_t *mem_wr_func_p[MAX_MEMORY_CHANNELS + 0x100];
unsigned int mem_rd_ofs[MAX_MEMORY_CHANNELS + 0x100];
unsigned int mem_wr_ofs[MAX_MEMORY_CHANNELS + 0x100];

static const struct linear_access_decoder_st *linear_memory_access_decoder ( const unsigned int lin, const unsigned int slot, const struct linear_access_decoder_st *p );

// NOTE: these callbacks wants to be AS FAST AS POSSIBLE.
// Since some memory locations are simpel RAMs other I/O
// we still need to check what's the situation by having
// tables of direct memory access pointers and function
// pointers as well for every 256 bytes of CPU address space,
// since that's the minimal amount of chunk cannot altered
// by any address mapping. Direct memory pointers are tricky,
// since their values are crafted specially we can add cpu
// address directly without adding the lower 8 bits only,
// thus saving some time. THIS IS THOUGH ONLY TRUE FOR
// THE FIRST 256 SLOTS (normal CPU slots, not the "memory
// channels" for linear addressing stuff like DMA).

static inline Uint8 cpu_read ( const Uint16 addr )
{
	const unsigned int slot = addr >> 8;
	if (XEMU_LIKELY(mem_rd_data_p[slot]))
		return *(mem_rd_data_p[slot] + addr);
	else
		return mem_rd_func_p[slot](slot, addr);
}

static inline void cpu_write ( const Uint16 addr, const Uint8 data )
{
	const unsigned int slot = addr >> 8;
	if (XEMU_LIKELY(mem_wr_data_p[slot]))
		*(mem_wr_data_p[slot] + addr) = data;
	else
		mem_wr_func_p[slot](slot, addr, data);
}

int cpu_rmw_old_data = -1;

static inline void cpu_write_rmw ( Uint16 addr, Uint8 old_data, Uint8 new_data )
{
	const unsigned int slot = addr >> 8;
	if (XEMU_LIKELY(mem_wr_data_p[slot]))
		*(mem_wr_data_p[slot] + addr) = new_data;
	else {
		// RMW functions exploits the fact that original MOS 65xx CPUs did write old
		// data first and then new data. C65 does not do this, but C64 does. To
		// avoid one major incompatibility between C64 and C65, MEGA65 reintroduced
		// this feature. We emulate this behaviour with this function. However on memory
		// accesses it does not matter (see the 'if' case above), with callbacks it may
		// be important, thus we set cpu_rmw_old_data variable (so it's not -1!), the
		// give target callback can use this information to realize the desired behaviour!
		cpu_rmw_old_data = old_data;
		mem_wr_func_p[slot](slot, addr, new_data);
		cpu_rmw_old_data = -1;
	}
}


void memory_invalidate_mapper ( unsigned int start_slot, unsigned int slots );


// The first three must shared the first two bits of values, and no other stuff can be within! (that's the reason of "jump" in sequence)
// do NOT change these! The code _DO_ use direct values without these macros as well!!!! [to address all the bits at once, etc]
#define C64_D000_RAM_VISIBLE		0
#define C64_D000_CHARGEN_VISIBLE	1
#define C64_D000_IO_VISIBLE		2
#define C64_D000_MASK			(C64_D000_RAM_VISIBLE|C64_D000_CHARGEN_VISIBLE|C64_D000_IO_VISIBLE)
#define C64_KERNAL_VISIBLE		4
#define C64_BASIC_VISIBLE		5

#define IS_LEGACY_IO_VISIBLE()		((c64_memlayout & C64_D000_IO_VISIBLE) && (!(map_mask & 0x40)))

// Holds the current C64-style memory configuration.
// Set from one of the elements of c64_memlayout_table above, indexed by the
// effective CPU I/O port value.
static Uint8 c64_memlayout = -1;

// This is C64-style memory configurations, indexed by the effective CPU I/O
// port value ("effective" = CPU IO data and DDR port, both changes the result).
// Note about C64 memory management:
// All WRITE accesses writes the RAM, _BUT_ the C64_D000_IO_VISIBLE case!!
// This fact though is not here in the table of course, just important to mention.
static const Uint8 c64_memlayout_table_by_memcfgreg[] = {
	C64_D000_RAM_VISIBLE,
	C64_D000_CHARGEN_VISIBLE,
	C64_D000_CHARGEN_VISIBLE | C64_KERNAL_VISIBLE,
	C64_D000_CHARGEN_VISIBLE | C64_KERNAL_VISIBLE | C64_BASIC_VISIBLE,
	C64_D000_RAM_VISIBLE,
	C64_D000_IO_VISIBLE,
	C64_D000_IO_VISIBLE      | C64_KERNAL_VISIBLE,
	C64_D000_IO_VISIBLE      | C64_KERNAL_VISIBLE | C64_BASIC_VISIBLE,
};


static void update_cpu_io_port ( int update_mapper )
{
	Uint8 desired = c64_memlayout_table_by_memcfgreg[(cpu_io_port[1] | (~cpu_io_port[0])) & 7];
	main_ram[0] = cpu_io_port[0];	// FIXME: maybe this is not such a simple stuff?? ie what happens to read back data/ddr of cpu port, EXACTLY?
	main_ram[1] = cpu_io_port[1];	// FIXME: -- "" --
	if (desired != c64_memlayout) {
		if (update_mapper) {
			const Uint8 changed = desired ^ c64_memlayout;
			// Actually, the invalidation process can be imagined to be conditional even on based the MAP block allows it at all ...
			// However I'm unsure if it worths the check, or I'll have so many checks, that's it more simple just invalidate anyway ...
			// After all the reason to write CPU I/O port to CHANGE memory layout which is uneffective if MAP'ed, so programmers may won't do it anyway?
			// Not even mentioning the VIC-III ROM mapping checking and then the complexity of checking if it's not in hypervisor mode when it does not apply ... :-O
			// OK, enough talking, let's try to use at least some logic here, even if it's not the full picture ...
			if ((changed & C64_BASIC_VISIBLE)  && !(map_mask & 0x20)) {
				memory_invalidate_mapper(0xA0, 0xBF);
				//dectab_last_p[0xA] = decoder_table;
				//dectab_last_p[0xB] = decoder_table;
			}
			if ((changed & C64_D000_MASK)      && !(map_mask & 0x40)) {
				memory_invalidate_mapper(0xD0, 0xDF);
				// since we've just invalidated $DXXX area, no legacy I/O is mapped (yet)
				// later it maybe can be refined
				legacy_io_is_mapped = 0;
				//dectab_last_p[0xD] = decoder_table;
			}
			if ((changed & C64_KERNAL_VISIBLE) && !(map_mask & 0x80)) {
				memory_invalidate_mapper(0xE0, 0xFF);
				//dectab_last_p[0xE] = decoder_table;
				//dectab_last_p[0xF] = decoder_table;
			}
		}
		c64_memlayout = desired;
	}
}


// The first two bytes of the memory is special, since it's the "CPU I/O port"
// (on C65, part of VIC-III for real, but does not matter)
// Since memory mapping is 256 bytes long page based, the first page (called "zero page"
// here, do not confuse it with the zero page of the CPU ... better saying "base page")
// must be handled in a separated handled, at least for writing.
static void zero_page_writer ( unsigned int slot, unsigned int addr, Uint8 data )
{
	if (XEMU_LIKELY(addr & 0xFE)) {	// if any bit set other than bit 0, it's not the CPU I/O port. Also we must see only the low byte.
		// the most likely case: not touching the the CPU data/ddr I/O port, normal memory access
		main_ram[addr & 0xFF] = data;
	} else {
		addr &= 1;
		if (!addr && (data & 0xFE) == 64) {
			// special meaning: turn on/off force-fast mode (for values 64 and 65)
			data &= 1;	// the lowest bit makes the difference between 64 and 65
			if (XEMU_LIKELY(force_fast != data)) {
				force_fast = data;
				machine_set_speed(0);
			}
		} else {
			cpu_io_port[addr] = data;
			update_cpu_io_port(1);
		}
	}
}


// C65 herritage that colour RAM (compared to C64, 2K) is mapped to the main memory, unlike C64.
// Since we want a separated colour RAM area (which is the case of MEGA65 as well), it means,
// either we need to have a fragemented main RAM, which are slower to emulate, OR at least
// the are of mapped main RAM must be "double write" to also update the main RAM and colour RAM.
// The advantage of this: it affects only writes, read operations are fast and can be done
// only on the colour RAM or main RAM, unbothered!
static void colour_ram_head_writer ( unsigned int slot, unsigned int addr, Uint8 data )
{
	const unsigned int ofs = mem_wr_ofs[slot] + (addr & 0xFF);
	main_ram[0x1F800 + ofs] = data;
	colour_ram[ofs] = data;
	// Nasty trick to allow C64 (VIC-II) I/O mode to have 4 bit only colour RAM ;-P
	// So we maintain a copy of the colour RAM with the correct 'high 4 bits always set'.
	// so we don't need to worry about reading be correct
	// Note: c64_colour_ram is 2K. Yes it's only 1K for real, but not to overflow the write
	// here, and avoid a condition, we just use 2K.
	c64_colour_ram[ofs] = (data & 0x0F) | 0xF0;
}


static void undecoded_interaction ( const Uint32 addr, const char *op_type )
{
#ifdef	DISABLE_DEBUG
	if (XEMU_LIKELY(skip_unhandled_mem == 3))
		return;
#endif
	char msg[128];
	sprintf(msg, "Unhandled memory %s operation for linear address $%X (PC=$%04X)", op_type, addr, cpu65.pc);
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


static Uint8 undecoded_reader ( const unsigned int slot, const unsigned int addr )
{
	undecoded_interaction(mem_rd_ofs[slot] + (addr & 0xFF), "READ");
	return MEMORY_UNDECODED_PATTERN;
}


static void undecoded_writer ( const unsigned int slot, const unsigned int addr, const Uint8 data )
{
	undecoded_interaction(mem_wr_ofs[slot] + (addr & 0xFF), "WRITE");
}


// WARNING!
// This table must be COMPLETE, every address ranges of the 28 bit addressing space.
// Also, it must be ordered, and cannot have "gaps", those must be marked undecoded.
// A range must be on 256 bytes boundary, with start having 0x00 as the last byte,
// and end with 0xff as the last byte.
#define DEFINE_UNDECODED_AREA(begin,end) \
	{ begin, end, NULL, undecoded_reader, NULL, undecoded_writer }
#define DEFINE_IGNORED_AREA(begin, end) \
	{ begin, end, white_hole, NULL, black_hole, NULL }
static const struct linear_access_decoder_st decoder_table[] = {
	// the first two bytes of the memory space if CPU I/O port (sadly! as it means complications and performance loss to emulate), which
	// needs special care! That's the reason having a separated entry for this 256-byte page ...
	{ 0,	0xFF,	main_ram, NULL, NULL, zero_page_writer, MEMDEC_NORMAL_POLICY },
	// the rest of the main RAM till the start of C65-style colour RAM is RAM
	{ 0x100, 0x1F7FF, main_ram + 0x100, NULL, main_ram + 0x100, NULL, MEMDEC_NORMAL_POLICY },
	// the last 2K of the first 128K is the C65 colour RAM. In case of MEGA65
	// it's also the first 2K of the colour RAM, so special care needed as
	// for writing this area, at least!
	{ 0x1F800, 0x1FFFF, main_ram + 0x1F800, NULL, NULL, colour_ram_head_writer, MEMDEC_NORMAL_POLICY },
	// 128K "ROM" which is in fact part of the main RAM area. However this
	// area can be write protected to really mimic being ROM, thus need to
	// be defined separately
	{ 0x20000, 0x3FFFF, main_ram + 0x20000, NULL, main_ram + 0x20000, NULL, MEMDEC_ROM_POLICY },
	// the rest of main RAM ....
	{ 0x40000, MAIN_RAM_SIZE - 0x40001, main_ram + 0x40000, NULL, main_ram + 0x40000, NULL, MEMDEC_NORMAL_POLICY },
	DEFINE_UNDECODED_AREA(MAIN_RAM_SIZE, 0x3FFFFFF),
	DEFINE_IGNORED_AREA(0x4000000, 0x7FFFFFF),
	{ 0x8000000, 0x8000000 + SLOW_RAM_SIZE - 1, slow_ram, NULL, slow_ram, NULL },
	// last memory entry, must complete the 28 bit address space
//	{ ..., 0xFFFFFFF, NULL, undecoded_read, NULL, undecoded_write },
};
#undef DEFINE_IGNORED_AREA
#undef DEFINE_UNDECODED_AREA


#ifndef XEMU_RELEASE_BUILD
// Sanity check to catch errors in the table construction
// Will be disabled in release builds, assuming it was run once before for testing in non-release builds :)
static inline const char *check_decoder_table ( void )
{
	const struct linear_access_decoder_st *p = decoder_table;
	const struct linear_access_decoder_st *e = (struct linear_access_decoder_st*)((Uint8*)decoder_table + sizeof(decoder_table)) - 1;
	if (p->begin != 0)
		return "list does not start with 0x0";
	for (; p != e; p++) {
		if (p->begin >= p->end)
			return "a region has zero or negative size?!";
		if ((p->begin & 0xFF) != 0)
			return "a region starts with non-0x00 byte!";
		if ((p->end & 0xFF) != 0xFF)
			return "a region ends with non-0xFF byte!";
		if (p != decoder_table && p->begin != (p - 1)->end + 1)
			return "hole or overlap between entries maybe out of address order";
	}
	// FIXME: if I want more "pseudo" elements in the table at the end, this test should be modified!!
	if (p->end != 0xFFFFFFF)
		return "list does not end with 0xFFFFFFF";
	return NULL;
}
#endif


static inline Uint8 memory_channel_read ( const unsigned int channel, const Uint32 linaddr )
{
	const Uint32 linaddr_aligned = linaddr & 0xfffff00;
	// Basically, we want to check, if the we use the same 256 byte page as previously.
	// This is really not the same logic as with regular CPU memory access with 256 slots,
	// however this must be done, since memory_channel functions are more for purposes of
	// access the whole 28 bit addressing space (ie "linear addressing") like for DMA,
	// debugger, and CPU linear addressing mode opcodes.
	// If yes, no prob, just reuse the previous decoded area ...
	if (XEMU_UNLIKELY(linaddr_aligned != memory_channel_last_usage[channel])) {
		// ... however, if it's not the same, we must decode the access first
		memory_channel_last_usage[channel] = linaddr_aligned;
		memory_channel_last_dectab_p[channel] = linear_memory_access_decoder(linaddr_aligned, channel + 0x100, memory_channel_last_dectab_p[channel]);
	}
	if (XEMU_LIKELY(mem_rd_data_p[channel + 0x100]))
		return *(mem_rd_data_p[channel + 0x100] + (linaddr & 0xFF));	// unlike regular CPU addresses, we use only the in-slot offset (0-0xFF) to add!
	else
		return mem_rd_func_p[channel + 0x100](channel + 0x100, linaddr);
}
static XEMU_INLINE Uint8 dma_list_read ( const Uint32 linaddr ) { return memory_channel_read(DMA_LIST_MEMORY_CHANNEL, linaddr); }



// See comments of memory_channel_read about the details ...
static inline void memory_channel_write ( const unsigned int channel, const Uint32 linaddr, const Uint8 data )
{
	const Uint32 linaddr_aligned = linaddr & 0xfffff00;
	if (XEMU_UNLIKELY(linaddr_aligned != memory_channel_last_usage[channel])) {
		memory_channel_last_usage[channel] = linaddr_aligned;
		memory_channel_last_dectab_p[channel] = linear_memory_access_decoder(linaddr_aligned, channel + 0x100, memory_channel_last_dectab_p[channel]);
	}
	if (XEMU_LIKELY(mem_wr_data_p[channel + 0x100]))
		*(mem_wr_data_p[channel + 0x100] + (linaddr & 0xFF)) = data;
	else
		mem_wr_func_p[channel + 0x100](channel + 0x100, linaddr, data);
}



// VERY important point: "lin" parameter must be 256 aligned, and max 28 bit!
// Which means, any "arbitary" one must be AND'ed with 0xfffff00 before calling this function!
// passing parameter "p" has the intent to give some "hints" based on previous decoding, where should scan the decoder table
// the return value is intended to store the position of the scan-table to re-use for further decoding (ie, the "p" parameter, see the previous comment line)
static const struct linear_access_decoder_st *linear_memory_access_decoder ( const unsigned int lin, const unsigned int slot, const struct linear_access_decoder_st *p )
{
	const unsigned int slot_ofs = slot < 0x100 ? slot << 8 : 0;
	// Find the decoder entry for the given linear address, starting with the previous 'current' entry (p).
	// It's possible we are at the one wanted to be used anyway and no need to search upwards/downwards at all.
	// Note, that decondig table is strictly continogous, and ordered and fill the full addressing space!
	// So we even don't need to check for end of table, etc etc.
	// MEMDEC_BIDIRECTIONAL_TABLE_SCAN: read the comment near the beginning of this file
#ifdef MEMDEC_BIDIRECTIONAL_TABLE_SCAN
	while (lin < p->begin)
		p--;
#else
	if (lin < p->begin)
		p = decoder_table;
#endif
	while (lin > p->end)
		p++;
	//mem_p_p[slot] = p;
	mem_rd_ofs[slot] = lin - p->begin;
	mem_wr_ofs[slot] = lin - p->begin;
	// At this point we found or desired table entry at pointer "p".
	// Now, we want to see, if there is some special policy for that entry
	// (like, ROM protection can be turned on/off, memory from hypervisor / "user space", ...)
	switch (p->policy) {
		case MEMDEC_NORMAL_POLICY:	// No special policy for the given memory region
			mem_rd_data_p[slot] = p->rd_data ? p->rd_data - slot_ofs + lin - p->begin : NULL;
			mem_rd_func_p[slot] = p->rd_func;
			mem_wr_data_p[slot] = p->wr_data ? p->wr_data - slot_ofs + lin - p->begin : NULL;
			mem_wr_func_p[slot] = p->wr_func;
			break;
		case MEMDEC_ROM_POLICY:	// 128K-256K is the C65 ROM, which can be write protected (or can be R/W)
			mem_rd_data_p[slot] = p->rd_data ? p->rd_data - slot_ofs + lin - p->begin : NULL;
			mem_rd_func_p[slot] = p->rd_func;
			if (rom_protect) {
				mem_wr_data_p[slot] = p->rd_data ? black_hole - slot_ofs + lin : NULL;
				mem_wr_func_p[slot] = p->wr_func;
			} else {
				mem_wr_data_p[slot] = p->wr_data ? p->wr_data - slot_ofs + lin - p->begin : NULL;
				mem_wr_func_p[slot] = p->wr_func;
			}
			break;
		case MEMDEC_HYPERVISOR_POLICY:	// Only in hypervisor mode, otherwise, "seems to be undecoded" memory area
			if (XEMU_LIKELY(in_hypervisor)) {
				mem_rd_data_p[slot] = p->rd_data ? p->rd_data - slot_ofs + lin - p->begin : NULL;
				mem_rd_func_p[slot] = p->rd_func;
				mem_wr_data_p[slot] = p->wr_data ? p->wr_data - slot_ofs + lin - p->begin : NULL;
				mem_wr_func_p[slot] = p->wr_func;
			} else {
				// FIXME: maybe better approach to use the CB method, thus having information about
				// the try to access hypervisor memory from non-hypervisor mode.
				mem_rd_data_p[slot] = white_hole - slot_ofs;
				mem_wr_data_p[slot] = black_hole - slot_ofs;
			}
			break;
		case MEMDEC_IOREGION_POLICY:
			// Special policy to map I/O. It would took too much space in the decoding table slowing down its scanning
			// So it's better to do this way, also it helps to have common things with the legacy based I/O mapping at $D000
			// to some extent at least.
			TODO();
			break;
		default:
			XEMU_UNREACHABLE();
	}
	return p;
}

// Helper function for CPU memory mapper: MAP a C64-style ROM stuff
// It's clear from "slot" which ROM (chargen/basic/kernal) so no need to specify. Also for RAM which is always the logical slot.
// Also note, that CPU based slots are used a way that CPU address is ADDED to the mapping.
// Together with linear_memory_access_decoder() these helpers are the only ones used by CPU mappings at all (which is logical,
// since these are CPU based memory mapping controls).
static XEMU_INLINE void c64_map_rom_XXXX ( unsigned int slot )
{
	mem_rd_data_p[slot] = main_ram + 0x20000;	// offset in C65 ROM for the C64 stuff, and cpu_addr is also added, so it can be this fixed!!
	mem_wr_data_p[slot] = main_ram;			// writing a ROM in case of C64-mapped ROM situation will result on writing RAM instead!	
}
#define c64_map_rom_A000 c64_map_rom_XXXX
#define c64_map_rom_D000 c64_map_rom_XXXX
#define c64_map_rom_E000 c64_map_rom_XXXX
static XEMU_INLINE void c65_map_rom_C000 ( unsigned int slot )
{
	mem_rd_data_p[slot] = main_ram + 0x20000;
	mem_wr_data_p[slot] = rom_protect ? black_hole - (slot << 8) : main_ram + 0x20000;
}
static XEMU_INLINE void c65_map_rom_XXXX ( unsigned int slot )
{
	mem_rd_data_p[slot] = main_ram + 0x30000;
	mem_wr_data_p[slot] = rom_protect ? black_hole - (slot << 8) : main_ram + 0x30000;
}
#define c65_map_rom_8000 c65_map_rom_XXXX
#define c65_map_rom_A000 c65_map_rom_XXXX
#define c65_map_rom_C000 c65_map_rom_C000
#define c65_map_rom_E000 c65_map_rom_XXXX
static XEMU_INLINE void c6x_map_ram ( unsigned int slot )
{
	mem_rd_data_p[slot] = main_ram;
	// We must be aware of slot 0 "problem" (CPU port needs special attention)
	if (XEMU_LIKELY(slot)) {
		mem_wr_data_p[slot] = main_ram;
	} else {
		mem_wr_data_p[slot] = NULL;
		mem_wr_func_p[slot] = zero_page_writer;
	}
}
static XEMU_INLINE void map_legacy_io ( unsigned slot )
{
	mem_rd_data_p[slot] = NULL;
	mem_wr_data_p[slot] = NULL;
	mem_rd_func_p[slot] = legacy_io_slot_rd[io_mode][slot - 0xD0];
	mem_wr_func_p[slot] = legacy_io_slot_wr[io_mode][slot - 0xD0];
}


// Decoding memory access issued by the CPU.
// MAP must be taken account, also "CPU I/O port" and VIC-III ROM banking.
// Also special care about hypervisor mode must be taken account (for example
// VIC-III ROM banking has no effect in hypervisor mode!)
// These functions are used by the "lazy" resolvers (ie doing on-demand!).
// Note: this is "slot" based (256 byte pages) as any memory decoding has the finest granulity of this unit.
// This function only valid in the first 256 slots, additional slots are memory (special) channels and no
// scope of this function! [they are for linear memory accessing, done by DMA, linear CPU addressing and debugger]
static void cpu_memory_access_decoder ( unsigned int slot )
{
#	ifdef DO_FULL_LEGACY_MAPPINGS
#		define DO_LEGACY_MAPPING(first_slot,last_slot,helper)	do { for (int i = first_slot; i <= last_slot; i++) helper(i); } while (0)
#	else
#		define DO_LEGACY_MAPPING(first_slot,last_slot,helper)	helper(slot)
#	endif
#	define DO_MAP_LO() dectab_last_p[page4k] = linear_memory_access_decoder(map_megabyte_low  + ((map_offset_low  + (slot << 8)) & 0xFFF00), slot, dectab_last_p[page4k])
#	define DO_MAP_HI() dectab_last_p[page4k] = linear_memory_access_decoder(map_megabyte_high + ((map_offset_high + (slot << 8)) & 0xFFF00), slot, dectab_last_p[page4k])
	static const struct linear_access_decoder_st *dectab_last_p[16] = {
		decoder_table, decoder_table, decoder_table, decoder_table,
		decoder_table, decoder_table, decoder_table, decoder_table,
		decoder_table, decoder_table, decoder_table, decoder_table,
		decoder_table, decoder_table, decoder_table, decoder_table
	};
	// We want to see mappings per 4K basis.
	// Yes, MAP opcode works by 8K, but some memory areas are 4K only (like I/O space, or some VIC-III mapping)
	// As input argument is "slot" (256 bytes long pages), we need to shift right by 4 (ie, divide further by 16)
	const unsigned int page4k = slot >> 4;
	switch (page4k) {
		case 0x0:	// $0000-$0FFF of low region
		case 0x1:	// $1000-$1FFF of low region
			if ((map_mask & 0x01)) {
				DO_MAP_LO();
			} else {
				DO_LEGACY_MAPPING(0x00, 0x1F, c6x_map_ram);
			}
			break;
		case 0x2:	// $2000-$2FFF of low region
		case 0x3:	// $3000-$3FFF of low region
			if ((map_mask & 0x02)) {
				DO_MAP_LO();
			} else {
				DO_LEGACY_MAPPING(0x20, 0x3F, c6x_map_ram);
			}
			break;
		case 0x4:	// $4000-$4FFF of low region
		case 0x5:	// $5000-$5FFF of low region
			if ((map_mask & 0x04)) {
				DO_MAP_LO();
			} else {
				DO_LEGACY_MAPPING(0x40, 0x5F, c6x_map_ram);
			}
			break;
		case 0x6:	// $6000-$6FFF of low region
		case 0x7:	// $7000-$7FFF of low region
			if ((map_mask & 0x08)) {
				DO_MAP_LO();
			} else {
				DO_LEGACY_MAPPING(0x60, 0x7F, c6x_map_ram);
			}
			break;
		case 0x8:	// $8000-$8FFF of high region
		case 0x9:	// $9000-$9FFF of high region
			if ((vic_registers[0x30] & VIC3_ROM_MASK_8000) && (!in_hypervisor)) {
				DO_LEGACY_MAPPING(0x80, 0x9F, c65_map_rom_8000);
			} else	if ((map_mask & 0x10)) {
				DO_MAP_HI();
			} else {
				DO_LEGACY_MAPPING(0x80, 0x9F, c6x_map_ram);
			}
			break;
		case 0xA:	// $A000-$AFFF of high region
		case 0xB:	// $B000-$BFFF of high region
			if ((vic_registers[0x30] & VIC3_ROM_MASK_A000) && (!in_hypervisor)) {
				DO_LEGACY_MAPPING(0xA0, 0xBF, c65_map_rom_A000);
			} else if ((map_mask & 0x20)) {
				DO_MAP_HI();
			} else if ((c64_memlayout & C64_BASIC_VISIBLE)) {
				DO_LEGACY_MAPPING(0xA0, 0xBF, c64_map_rom_A000);
			} else {
				DO_LEGACY_MAPPING(0xA0, 0xBF, c6x_map_ram);
			}
			break;
		case 0xC:	// $C000-$CFFF of high region
			if ((vic_registers[0x30] & VIC3_ROM_MASK_C000) && (!in_hypervisor)) {
				// Beware, VIC3 C000 ROM mapping is the only one which is 4K in length and not 8K
				DO_LEGACY_MAPPING(0xC0, 0xCF, c65_map_rom_C000);
			} else if ((map_mask & 0x40)) {
				DO_MAP_HI();
			} else {
				DO_LEGACY_MAPPING(0xC0, 0xCF, c6x_map_ram);
			}
			break;
		case 0xD:	// $D000-$DFFF of high region
			if ((map_mask & 0x40)) {
				DO_MAP_HI();
				legacy_io_is_mapped = 0;
			} else if ((c64_memlayout & C64_D000_CHARGEN_VISIBLE)) {
				DO_LEGACY_MAPPING(0xD0, 0xDF, c64_map_rom_D000);
				legacy_io_is_mapped = 0;
			} else if ((c64_memlayout & C64_D000_IO_VISIBLE)) {
				DO_LEGACY_MAPPING(0xD0, 0xDF, map_legacy_io);
				legacy_io_is_mapped = 1;
			} else {
				DO_LEGACY_MAPPING(0xD0, 0xDF, c6x_map_ram);
				legacy_io_is_mapped = 0;
			}
			break;
		case 0xE:	// $E000-$EFFF of high region
		case 0xF:	// $F000-$FFFF of high region
			if ((vic_registers[0x30] & VIC3_ROM_MASK_E000) && (!in_hypervisor)) {
				DO_LEGACY_MAPPING(0xE0, 0xFF, c65_map_rom_E000);
			} else if ((map_mask & 0x80)) {
				DO_MAP_HI();
			} else if ((c64_memlayout & C64_KERNAL_VISIBLE)) {
				DO_LEGACY_MAPPING(0xE0, 0xFF, c64_map_rom_E000);
			} else {
				DO_LEGACY_MAPPING(0xE0, 0xFF, c6x_map_ram);
			}
			break;
		default:
			XEMU_UNREACHABLE();
	}
#undef	DO_MAP_LO
#undef	DO_MAP_HI
#undef	DO_LEGACY_MAPPING
}



// Special "tricky" functions made default as read/write funcs which can
// resolve the address for the cache AND do the op as well!
// This is kind of "lazy binding", ie there is no need to build the whole table
// of decoding and will be done on-demand. However this also means, it's
// extermely important to invalidate mappings when sensitive stuff changes,
// like CPU MAP, I/O port, etc.
// This also means, that default handlers should be these ones (done by
// the invalidation function) which if hit, first it fills the particular
// slot (for later reference into the same slot) and also serve the memory
// request.
static Uint8 memory_resolver_reader ( unsigned int slot, Uint16 addr )
{
	cpu_memory_access_decoder(slot);
	return cpu_read(addr);
}

static void memory_resolver_writer ( unsigned int slot, Uint16 addr, Uint8 data )
{
	cpu_memory_access_decoder(slot);
	cpu_write(addr, data);
}


// This is the invalidate function, which invalidates certain slots.
// Must be called every time memory mapping, memory area handling, etc changes.
// See comment at memory_resolver functions above for further comments.
void memory_invalidate_mapper ( unsigned int start_slot, unsigned int last_slot )
{
	while (start_slot <= last_slot) {
		mem_rd_data_p[start_slot] = NULL;
		mem_rd_func_p[start_slot] = memory_resolver_reader;
		mem_wr_data_p[start_slot] = NULL;
		mem_wr_func_p[start_slot] = memory_resolver_writer;
		start_slot++;
	}
}


void memory_invalidate_channels ( void )
{
	for (int a = 0; a < MAX_MEMORY_CHANNELS; a++) {
		memory_channel_last_usage[a] = INVALIDATED_MEMORY_CHANNEL;
		memory_channel_last_dectab_p[a] = decoder_table;
	}
}


void memory_invalidate_mapper_all ( void )
{
	memory_invalidate_mapper(0, 0xFF);
	memory_invalidate_channels();
	legacy_io_is_mapped = 0;
}




void memory_init ( void )
{
#ifndef XEMU_RELEASE_BUILD
	const char *p = check_decoder_table();
	if (p)
		FATAL("MEMDEC table sanity check failure: %s", p);
#endif
	memory_invalidate_mapper_all();
	memset(white_hole, sizeof white_hole, MEMORY_UNDECODED_PATTERN);
	memset(main_ram, MAIN_RAM_SIZE, MEMORY_INIT_PATTERN);
	// Ensure that all the colou-RAM tricks (to be faster to be emulated) have consistent state
	// FIXME: this should be done on snapshot loading as well!
	for (int a = 0; a < 2048; a++) {
		main_ram[0x1F800 + a] = colour_ram[a];
		c64_colour_ram[a] = (colour_ram[a] & 0x0F) | 0xF0;
	}
}
