/* Test-case for a very simple and inaccurate and even not working Commodore 65 emulator.
   Copyright (C)2016 LGB (Gábor Lénárt) <lgblgblgb@gmail.com>

   This is the Commodore 65 emulation. Note: the purpose of this emulator is merely to
   test some 65CE02 opcodes, not for being a *usable* Commodore 65 emulator too much!

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

#include <stdio.h>

#include <SDL.h>

#include "commodore_65.h"
#include "cpu65ce02.h"
#include "emutools.h"


static Uint8 memory[0x110000];
static int map_offset[8];
static int map_mapped[8];
static Uint32 rgb_palette[4096];
static Uint32 vic3_palette[0x100];
static Uint8 vic3_registers[0x40];
static Uint8 vic3_palette_r[0x100], vic3_palette_g[0x100], vic3_palette_b[0x100];




static void init_palette ( void )
{
	int r, g, b, i;
	for (r = 0, i = 0; r < 16; r++)
		for (g = 0; g < 16; g++)
			for (b = 0; b < 16; b++)
				rgb_palette[i++] = SDL_MapRGBA(sdl_pix_fmt, r * 17, g * 17, b * 17, 0xFF);
	SDL_FreeFormat(sdl_pix_fmt);	// thanks, we don't need this anymore.
}

#define RGB(r,g,b) rgb_palette[((r) << 8) | ((g) << 4) | (b)]




static void cpu_mapping ( int low_offset, int high_offset, int mapped_mask )
{
	int a, mask;
	for (a = 0, mask = 1; a < 8; a++, mask <<= 1) {
		if (mapped_mask & mask) {
			map_offset[a] = (a & 4) ? high_offset : low_offset;
			map_mapped[a] = 1;
		} else {
			map_offset[a] = 0;
			map_mapped[a] = 0;
		}
	}
	printf("MMU: low  offset is $%05X" NL, low_offset );
	printf("MMU: high offset is $%05X" NL, high_offset);
	printf("MMU: map mask is    b%d%d%d%d%d%d%d%d" NL,
		mapped_mask & 128 ? 1 : 0, mapped_mask & 64 ? 1 : 0, mapped_mask & 32 ? 1 : 0, mapped_mask & 16 ? 1 :0,
		mapped_mask & 8 ? 1 : 0, mapped_mask & 4 ? 1 : 0, mapped_mask & 2 ? 1 : 0, mapped_mask & 1
	);
}


int cpu_trap ( Uint8 opcode )
{
	return 0;	// not used
}


void cpu_do_aug ( void )
{
	cpu_inhibit_interrupts = 1;
	printf("CPU: MAP opcode, input A=$%02X X=$%02X Y=$%02X Z=$%02X" NL, cpu_a, cpu_x, cpu_y, cpu_z);
	cpu_mapping(
		(cpu_a << 8) | ((cpu_x & 15) << 16),	// offset of lower half (blocks 0-3)
		(cpu_y << 8) | ((cpu_z & 15) << 16),	// offset of higher half (blocks 4-7)
		(cpu_z & 0xF0) | (cpu_x >> 4)		// "is mapped" mask for blocks
	);
}


void cpu_do_nop ( void )
{
	cpu_inhibit_interrupts = 0;
}



static void vic3_write_reg ( int addr, Uint8 data )
{
	// No old mode/new mode KEYing implemented ...
	printf("VIC3: write reg $%02X with data $%02X\n", addr, data);
	vic3_registers[addr] = data;
}


static Uint8 vic3_read_reg ( int addr )
{
	printf("VIC3: read reg $%02X with result $%02X\n", addr, vic3_registers[addr]);
	return vic3_registers[addr];
}




Uint8 cpu_read ( Uint16 addr )
{
	int blk = addr >> 13;
	int real_addr = map_offset[blk] + addr;
	Uint8 result;
	if (!map_mapped[blk]) {	// it's only applies if block is marked as "not mapped"?
		int hinyb = addr >> 12;
		if (
			((vic3_registers[0x30] & 0x80) && hinyb == 0xE) ||		// ROM at E000 (E000-EFFF? What about F000-FFFF?!)
			((vic3_registers[0x30] & 0x40) && hinyb == 0x9)	||	// ROM at 9000 (??? ROML signal talks about from 8000, what is this then?!)
			((vic3_registers[0x30] & 0x20) && hinyb == 0xC)	||	// ROM at C000 
			((vic3_registers[0x30] & 0x10) && (hinyb == 0xA || hinyb == 0xB)) || 		// ROM at A000
			((vic3_registers[0x30] & 0x08) && hinyb == 0x8)		// ROM at 8000
		)
			real_addr += 0x20000;
		if (addr >= 0xD000 && addr < 0xD080)
			return vic3_read_reg(addr & 0x3F);
		if (addr >= 0xD100 && addr < 0xD200)
			return vic3_palette_r[addr & 0xFF];
		if (addr >= 0xD200 && addr < 0xD300)
			return vic3_palette_g[addr & 0xFF];
		if (addr >= 0xD300 && addr < 0xD400)
			return vic3_palette_b[addr & 0xFF];
	}
	result = memory[real_addr];
	printf("CPU: read @ $%04X [PC=$%04X] (BLK=%d, REAL=$%05X, mapped=%d) result is $%02X" NL, addr, cpu_pc, blk, real_addr, map_mapped[blk], result);
	return result;
}


void cpu_write ( Uint16 addr, Uint8 data )
{
	int blk = addr >> 13;
	int real_addr = map_offset[blk] + addr;
	if (!map_mapped[blk]) {
		if (addr >= 0xD000 && addr < 0xD080) {
			vic3_write_reg(addr & 0x3F, data);
			return;
		}
		if (addr >= 0xD100 && addr < 0xD200) {
			vic3_palette_r[addr & 0xFF] = data & 31;
			vic3_palette[addr & data] = RGB(vic3_palette_r[addr & 0xFF] & 15, vic3_palette_g[addr & 0xFF] & 15, vic3_palette_b[addr & 0xFF] & 15);
			return;
		}
		if (addr >= 0xD200 && addr < 0xD300) {
			vic3_palette_g[addr & 0xFF] = data & 15;
			vic3_palette[addr & data] = RGB(vic3_palette_r[addr & 0xFF] & 15, vic3_palette_g[addr & 0xFF] & 15, vic3_palette_b[addr & 0xFF] & 15);
			return;
		}
		if (addr >= 0xD300 && addr < 0xD400) {
			vic3_palette_b[addr & 0xFF] = data & 15;
			vic3_palette[addr & data] = RGB(vic3_palette_r[addr & 0xFF] & 15, vic3_palette_g[addr & 0xFF] & 15, vic3_palette_b[addr & 0xFF] & 15);
			return;
		}
	}
	if (real_addr < 0x20000)	// do not write ROM ....
		memory[real_addr] = data;
	printf("CPU write @ $%04X [PC=$%04X] (BLK=%d, REAL=$%05X, mapped=%d) with data $%02X" NL, addr, cpu_pc, blk, real_addr, map_mapped[blk], data);
}



void clear_emu_events ( void )
{
}


static void dump_on_shutdown ( void )
{
	FILE *f = fopen("dump.mem", "wb");
	if (f) {
		fwrite(memory, 1, sizeof memory, f);
		fclose(f);
	}
}




int main ( int argc, char **argv )
{
	int cycles;
	printf("**** The Unusable Commodore 65 emulator from LGB" NL
	"INFO: Texture resolution is %dx%d" NL "%s" NL,
		SCREEN_WIDTH, SCREEN_HEIGHT,
		emulators_disclaimer
	);
	/* Initiailize SDL - note, it must be before loading ROMs, as it depends on path info from SDL! */
        if (emu_init_sdl(
		"Commodore 65 / LGB",		// window title
		"nemesys.lgb", "xclcd-c65",	// app organization and name, used with SDL pref dir formation
		1,				// resizable window
		SCREEN_WIDTH, SCREEN_HEIGHT,	// texture sizes
		SCREEN_WIDTH, SCREEN_HEIGHT,	// logical size
		SCREEN_WIDTH, SCREEN_HEIGHT,	// window size
		SCREEN_FORMAT,		// pixel format
		0,			// we have *NO* pre-defined colours (too many we need). we want to do this ourselves!
		NULL,			// -- "" --
		NULL,			// -- "" --
		RENDER_SCALE_QUALITY,	// render scaling quality
		USE_LOCKED_TEXTURE,	// 1 = locked texture access
		dump_on_shutdown
	))
		return 1;
	init_palette();	// get our 4096 colours ...
	memset(memory, 0xFF, sizeof memory);	// initialize memory
	memset(memory + 0xD000, 0, 0x1000);
	memset(vic3_registers, 0xFF, sizeof vic3_registers);
	// load ROM (128K sized)
	if (emu_load_file("c65-system.rom", memory + 0x20000, 0x20001) != 0x20000)
		FATAL("Cannot load C65 system ROM!");
	cpu_mapping(0x20000, 0x20000, 0xBE); // Set initial internal MMU state of 4510 .. I have no idea what should it be, but probably stack / ZP is needed, and also the I/O area ...
	// Now we have ROM, initial memory mapping up, we can try a CPU reset to fetch the reset vector
	cpu_reset();
	cycles = 0;
	for (;;) {
		SDL_Event e;
		while (SDL_PollEvent(&e) != 0) {
			if (e.type == SDL_QUIT)
				exit(0);
		}
		cycles += cpu_step();
		if (cycles >= FULL_FRAME_CPU_CYCLES) {
			cycles -= FULL_FRAME_CPU_CYCLES;
		}
#if 0
		// emulate some crazy raster changes ...
		memory[0xD012]++;
		if (!memory[0xD012])
			memory[0xD011] ^= 128;
#endif
	}
	puts("Goodbye!");
	return 0;
}
