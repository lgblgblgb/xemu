/* Test-case for a primitive PC emulator inside the Xemu project,
   currently using Fake86's x86 CPU emulation.
   Part of the Xemu project, please visit: https://github.com/lgblgblgb/xemu
   Copyright (C)2016-2022 LGB (Gábor Lénárt) <lgblgblgb@gmail.com>

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
#include "xemu/emutools_files.h"
#include "memory.h"

#include "bios.h"
#include "video.h"


uint32_t memtop;	// the byte AFTER the last available byte in the memory space
uint8_t *memory_rd_data_ptr_tab[MEMORY_64KPAGE_MAX];
uint8_t *memory_wr_data_ptr_tab[MEMORY_64KPAGE_MAX];
uint8_t (*memory_rd_func_ptr_tab[MEMORY_64KPAGE_MAX])(const uint16_t addr16);
void    (*memory_wr_func_ptr_tab[MEMORY_64KPAGE_MAX])(const uint16_t addr16, const uint8_t data);

static Uint8 *memory = NULL;

#define UNDECODED_MEMORY_PATTERN	0xFF
#define DECODED_MEMORY_PATTERN		0xFF
#define ROM_MEMORY_PATTERN		0x00


static uint8_t dummy_reader ( const uint16_t addr16 )
{
	return UNDECODED_MEMORY_PATTERN;
}

static void dummy_writer ( const uint16_t addr16, const uint8_t data )
{
}

static void fail_on_invalid_page ( const unsigned int slot64k )
{
	if (slot64k >= MEMORY_64KPAGE_MAX || slot64k >= (memtop >> 16))
		FATAL("Invalid 64K page %d (must be < %d) in %s", slot64k, MEMORY_64KPAGE_MAX, __FILE__);
}

static void assign_ram ( const unsigned int slot64k, uint8_t *source )
{
	fail_on_invalid_page(slot64k);
	memory_rd_data_ptr_tab[slot64k] = source - (slot64k << 16);
	memory_wr_data_ptr_tab[slot64k] = source - (slot64k << 16);
	memset(source, DECODED_MEMORY_PATTERN, 0x10000);
}

static void assign_rom ( const unsigned int slot64k, uint8_t *source )
{
	fail_on_invalid_page(slot64k);
	memory_rd_data_ptr_tab[slot64k] = source - (slot64k << 16);
	memory_wr_data_ptr_tab[slot64k] = NULL;
	memory_wr_func_ptr_tab[slot64k] = dummy_writer;
}

static void assign_callbacked ( const unsigned int slot64k, uint8_t (*rd_func)(const uint16_t), void (*wr_func)(const uint16_t, const uint8_t) )
{
	fail_on_invalid_page(slot64k);
	memory_rd_data_ptr_tab[slot64k] = NULL;
	memory_wr_data_ptr_tab[slot64k] = NULL;
	memory_rd_func_ptr_tab[slot64k] = rd_func;
	memory_wr_func_ptr_tab[slot64k] = wr_func;
}

static inline void assign_undecoded ( const unsigned int slot64k )
{
	assign_callbacked(slot64k, dummy_reader, dummy_writer);
}

void portout ( const uint16_t portnum, const uint8_t value )
{
	DEBUGPRINT("IO: port $%04X is written with $%02X" NL, portnum, value);
}


void portout16 ( const uint16_t portnum, const uint16_t value )
{
	DEBUGPRINT("IO: port $%04X is written with $%04X" NL, portnum, value);
}


uint8_t portin ( const uint16_t portnum)
{
	DEBUGPRINT("IO: port $%04X is byte-read" NL, portnum);
	return 0xFF;
}


uint16_t portin16 ( const uint16_t portnum)
{
	DEBUGPRINT("IO: port $%04X is word-read" NL, portnum);
	return 0xFFFF;
}


// Let's make it simple for now: fixed config: 640K base memory, 64K HMA accessible, 3 * 64K UMB, 64K of ROM as BIOS
void memory_init ( void )
{
	static int init_done = 0;
	if (init_done)
		FATAL("Cannot call %s more than once!", __func__);
	init_done = 1;
	memtop = MEMORY_MAX;
	memory = xemu_malloc(MEMORY_MAX);
	// Just to make sure every slot is initialized for something initially ...
	memset(memory, UNDECODED_MEMORY_PATTERN, MEMORY_MAX);
	for (int i = 0; i < MEMORY_64KPAGE_MAX; i++)
		assign_undecoded(i);
	// Later, this part can be run-time configurable somehow:
	for (int i = 0; i < 0xA; i++)
		assign_ram(i, memory + (i << 16));
	assign_callbacked(0xA, read_A0000, write_A0000);
	assign_callbacked(0xB, read_B0000, write_B0000);
	for (int i = 0xC; i < 0xF; i++)
		assign_ram(i, memory + (i << 16));
	assign_rom(0xF, memory + (0xF << 16));
	assign_ram(0x10, memory + (0x10 << 16));
	// Call BIOS init:
	bios_init(memory, memory + (0xF << 16));
}


void memory_save ( const char *fn )
{
	xemu_save_file(fn, memory, 640 * 1024, "Nope");
}
