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
static int vic_new_mode;
static Uint8 cpu_port[2];
static int scanline;




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



static void c65_init ( void )
{
	int r, g, b, i;
	// *** Init memory space
	memset(memory, 0xFF, sizeof memory);
	// *** Load ROM image
	if (emu_load_file("c65-system.rom", memory + 0x20000, 0x20001) != 0x20000)
		FATAL("Cannot load C65 system ROM!");
	// *** Init 4510 "MAP-related" MMU ...
	cpu_mapping(0, 0, 0);	// C65 boots in C64 mode at power-up
	cpu_port[0] = 0xFF;	// the "CPU I/O port" on 6510/C64, implemented by VIC3 for real in C65!
	cpu_port[1] = 0xFF;
	// *** Init 4096 element palette with RGB components for faster access later on palette register changes
	for (r = 0, i = 0; r < 16; r++)
		for (g = 0; g < 16; g++)
			for (b = 0; b < 16; b++)
				rgb_palette[i++] = SDL_MapRGBA(sdl_pix_fmt, r * 17, g * 17, b * 17, 0xFF);
	SDL_FreeFormat(sdl_pix_fmt);	// thanks, we don't need this anymore.
	// *** Init VIC3 registers and palette
	vic_new_mode = 0;
	scanline = 0;
	for (i = 0; i < 0x100; i++) {
		if (i < sizeof vic3_registers)
			vic3_registers[i] = 0;
		vic3_palette[i] = rgb_palette[0];  // black
		vic3_palette_r[i] = 0;
		vic3_palette_g[i] = 0;
		vic3_palette_b[i] = 0;
	}
	// *** RESET CPU, also fetches the RESET vector into PC
	cpu_reset();
	puts("INIT: end of initialization!");
}



int cpu_trap ( Uint8 opcode )
{
	return 0;	// not used here
}



// *** Implements the MAP opcode of 4510
void cpu_do_aug ( void )
{
	cpu_inhibit_interrupts = 1;	// disable interrupts to the next "EOM" (ie: NOP) opcode
	printf("CPU: MAP opcode, input A=$%02X X=$%02X Y=$%02X Z=$%02X" NL, cpu_a, cpu_x, cpu_y, cpu_z);
	cpu_mapping(
		(cpu_a << 8) | ((cpu_x & 15) << 16),	// offset of lower half (blocks 0-3)
		(cpu_y << 8) | ((cpu_z & 15) << 16),	// offset of higher half (blocks 4-7)
		(cpu_z & 0xF0) | (cpu_x >> 4)		// "is mapped" mask for blocks
	);
}



// *** Implements the EOM opcode of 4510
void cpu_do_nop ( void )
{
	cpu_inhibit_interrupts = 0;
}



static void vic3_write_reg ( int addr, Uint8 data )
{
	addr &= 0x3F;
	printf("VIC3: write reg $%02X with data $%02X\n", addr, data);
	if (addr == 0x2F) {
		if (!vic_new_mode && data == 0x96 && vic3_registers[0x2F] == 0xA5) {
			vic_new_mode = 1;
			printf("VIC3: switched into NEW I/O access mode :)" NL);
		} else if (vic_new_mode) {
			vic_new_mode = 0;
			printf("VIC3: switched into OLD I/O access mode :(" NL);
		}
	}
	if (!vic_new_mode && addr > 0x2F) {
		printf("VIC3: ignoring writing register $%02X (with data $%02X) because of old I/O access mode selected" NL, addr, data);
		return;
	}
	vic3_registers[addr] = data;
}	



static Uint8 vic3_read_reg ( int addr )
{
	addr &= 0x3F;
	if (!vic_new_mode && addr > 0x2F) {
		printf("VIC3: ignoring reading register $%02X because of old I/O access mode selected, answer is $FF" NL, addr);
		return 0xFF;
	}
	if (addr == 0x12)
		vic3_registers[0x12] = scanline & 0xFF;
	else if (addr == 0x11)
		vic3_registers[0x11] = (vic3_registers[0x11] & 0x7F) | ((scanline & 256) ? 0x80 : 0);
	printf("VIC3: read reg $%02X with result $%02X\n", addr, vic3_registers[addr]);
	return vic3_registers[addr];
}


#define RETURN_ON_IO_READ_NOT_IMPLEMENTED(func, fb) \
	do { printf("IO: NOT IMPLEMENTED read (emulator lacks feature), %s $%04X fallback to answer $%02X" NL, func, addr, fb); \
	return fb; } while (0)
#define RETURN_ON_IO_READ_NO_NEW_VIC_MODE(func, fb) \
	do { printf("IO: ignored read (not new VIC mode), %s $%04X fallback to answer $%02X" NL, func, addr, fb); \
	return fb; } while (0)


// Call this ONLY with addresses between $D000-$DFFF
// Ranges marked with (*) needs "vic_new_mode"
static Uint8 io_read ( int addr )
{
	if (addr < 0xD080)	// $D000 - $D07F:	VIC3
		return vic3_read_reg(addr);
	if (addr < 0xD0A0) {	// $D080 - $D09F	DISK controller (*)
		if (vic_new_mode)
			RETURN_ON_IO_READ_NOT_IMPLEMENTED("DISK controller", 0x00);	// emulation stops here with 0xFF
		else
			RETURN_ON_IO_READ_NO_NEW_VIC_MODE("DISK controller", 0xFF);
	}
	if (addr < 0xD100) {	// $D0A0 - $D0FF	RAM expansion controller (*)
		if (vic_new_mode)
			RETURN_ON_IO_READ_NOT_IMPLEMENTED("RAM expansion controller", 0xFF);
		else
			RETURN_ON_IO_READ_NO_NEW_VIC_MODE("RAM expansion controller", 0xFF);
	}
	if (addr < 0xD200) {	// $D100 - $D100	palette red nibbles (*)
		if (vic_new_mode)
			return vic3_palette_r[addr & 0xFF];
		else
			RETURN_ON_IO_READ_NO_NEW_VIC_MODE("palette red nibbles", 0xFF);
	}
	if (addr < 0xD300) {	// $D200 - $D200	palette green nibbles (*)
		if (vic_new_mode)
			return vic3_palette_g[addr & 0xFF];
		else
			RETURN_ON_IO_READ_NO_NEW_VIC_MODE("palette green nibbles", 0xFF);
	}
	if (addr < 0xD400) {	// $D300 - $D300	palette blue nibbles (*)
		if (vic_new_mode)
			return vic3_palette_b[addr & 0xFF];
		else
			RETURN_ON_IO_READ_NO_NEW_VIC_MODE("palette blue nibbles", 0xFF);
	}
	if (addr < 0xD440) {	// $D400 - $D43F	SID, right
		RETURN_ON_IO_READ_NOT_IMPLEMENTED("right SID", 0xFF);
	}
	if (addr < 0xD600) {	// $D440 - $D5FF	SID, left
		RETURN_ON_IO_READ_NOT_IMPLEMENTED("left SID", 0xFF);
	}
	if (addr < 0xD700) {	// $D600 - $D6FF	UART (*)
		if (vic_new_mode)
			RETURN_ON_IO_READ_NOT_IMPLEMENTED("UART", 0xFF);
		else
			RETURN_ON_IO_READ_NO_NEW_VIC_MODE("UART", 0xFF);
	}
	if (addr < 0xD800) {	// $D700 - $D7FF	DMA (*)
		if (vic_new_mode)
			RETURN_ON_IO_READ_NOT_IMPLEMENTED("DMA controller", 0xFF);
		else
			RETURN_ON_IO_READ_NO_NEW_VIC_MODE("DMA controller", 0xFF);
	}
	if (addr < ((vic3_registers[0x30] & 1) ? 0xE000 : 0xDC00)) {	// $D800-$DC00/$E000	COLOUR NIBBLES, mapped to $1F800 in BANK1
		printf("IO: reading colour RAM at offset $%04X" NL, addr - 0xD800);
		return memory[0x1F800 + addr - 0xD800];
	}
	if (addr < 0xDD00) {	// $DC00 - $DCFF	CIA-1
		RETURN_ON_IO_READ_NOT_IMPLEMENTED("CIA-1", 0xFF);
	}
	if (addr < 0xDE00) {	// $DD00 - $DDFF	CIA-2
		RETURN_ON_IO_READ_NOT_IMPLEMENTED("CIA-2", 0xFF);
	}
	if (addr < 0xDF00) {	// $DE00 - $DEFF	IO-1 external
		RETURN_ON_IO_READ_NOT_IMPLEMENTED("IO-1 external select", 0xFF);
	}
	// The rest: IO-2 external
	RETURN_ON_IO_READ_NOT_IMPLEMENTED("IO-2 external select", 0xFF);
}


#define RETURN_ON_IO_WRITE_NOT_IMPLEMENTED(func) \
	do { printf("IO: NOT IMPLEMENTED write (emulator lacks feature), %s $%04X with data $%02X" NL, func, addr, data); \
	return; } while(0)
#define RETURN_ON_IO_WRITE_NO_NEW_VIC_MODE(func) \
	do { printf("IO: ignored write (not new VIC mode), %s $%04X with data $%02X" NL, func, addr, data); \
	return; } while(0)



// Call this ONLY with addresses between $D000-$DFFF
// Ranges marked with (*) needs "vic_new_mode"
static void io_write ( int addr, Uint8 data )
{
	if (addr < 0xD080)	// $D000 - $D07F:	VIC3
		return vic3_write_reg(addr, data);
	if (addr < 0xD0A0) {	// $D080 - $D09F	DISK controller (*)
		if (vic_new_mode)
			RETURN_ON_IO_WRITE_NOT_IMPLEMENTED("DISK controller");
		else
			RETURN_ON_IO_WRITE_NO_NEW_VIC_MODE("DISK controller");
	}
	if (addr < 0xD100) {	// $D0A0 - $D0FF	RAM expansion controller (*)
		if (vic_new_mode)
			RETURN_ON_IO_WRITE_NOT_IMPLEMENTED("RAM expansion controller");
		else
			RETURN_ON_IO_WRITE_NO_NEW_VIC_MODE("RAM expansion controller");
	}
	if (addr < 0xD200) {	// $D100 - $D100	palette red nibbles (*)
		if (vic_new_mode) {
			vic3_palette_r[addr & 0xFF] = data & 15;
			vic3_palette[addr & 0xFF] = RGB(vic3_palette_r[addr & 0xFF], vic3_palette_g[addr & 0xFF], vic3_palette_b[addr & 0xFF]);
			printf("VIC3: palette #$%02X is set to RGB nibbles $%X%X%X" NL, addr & 0xFF, vic3_palette_r[addr & 0xFF], vic3_palette_g[addr & 0xFF], vic3_palette_b[addr & 0xFF]);
			return;
		} else
			RETURN_ON_IO_WRITE_NO_NEW_VIC_MODE("palette red nibbles");
	}
	if (addr < 0xD300) {	// $D200 - $D200	palette green nibbles (*)
		if (vic_new_mode) {
			vic3_palette_g[addr & 0xFF] = data & 15;
			vic3_palette[addr & 0xFF] = RGB(vic3_palette_r[addr & 0xFF], vic3_palette_g[addr & 0xFF], vic3_palette_b[addr & 0xFF]);
			printf("VIC3: palette #$%02X is set to RGB nibbles $%X%X%X" NL, addr & 0xFF, vic3_palette_r[addr & 0xFF], vic3_palette_g[addr & 0xFF], vic3_palette_b[addr & 0xFF]);
			return;
		} else
			RETURN_ON_IO_WRITE_NO_NEW_VIC_MODE("palette green nibbles");
	}
	if (addr < 0xD400) {	// $D300 - $D300	palette blue nibbles (*)
		if (vic_new_mode) {
			vic3_palette_b[addr & 0xFF] = data & 15;
			vic3_palette[addr & 0xFF] = RGB(vic3_palette_r[addr & 0xFF], vic3_palette_g[addr & 0xFF], vic3_palette_b[addr & 0xFF]);
			printf("VIC3: palette #$%02X is set to RGB nibbles $%X%X%X" NL, addr & 0xFF, vic3_palette_r[addr & 0xFF], vic3_palette_g[addr & 0xFF], vic3_palette_b[addr & 0xFF]);
			return;
		} else
			RETURN_ON_IO_WRITE_NO_NEW_VIC_MODE("palette blue nibbles");
	}
	if (addr < 0xD440) {	// $D400 - $D43F	SID, right
		RETURN_ON_IO_WRITE_NOT_IMPLEMENTED("right SID");
	}
	if (addr < 0xD600) {	// $D440 - $D5FF	SID, left
		RETURN_ON_IO_WRITE_NOT_IMPLEMENTED("left SID");
	}
	if (addr < 0xD700) {	// $D600 - $D6FF	UART (*)
		if (vic_new_mode)
			RETURN_ON_IO_WRITE_NOT_IMPLEMENTED("UART");
		else
			RETURN_ON_IO_WRITE_NO_NEW_VIC_MODE("UART");
	}
	if (addr < 0xD800) {	// $D700 - $D7FF	DMA (*)
		if (vic_new_mode)
			RETURN_ON_IO_WRITE_NOT_IMPLEMENTED("DMA controller");
		else
			RETURN_ON_IO_WRITE_NO_NEW_VIC_MODE("DMA controller");
	}
	if (addr < ((vic3_registers[0x30] & 1) ? 0xE000 : 0xDC00)) {	// $D800-$DC00/$E000	COLOUR NIBBLES, mapped to $1F800 in BANK1
		memory[0x1F800 + addr - 0xD800] = data;
		printf("IO: writing colour RAM at offset $%04X" NL, addr - 0xD800);
		return;
	}
	if (addr < 0xDD00) {	// $DC00 - $DCFF	CIA-1
		RETURN_ON_IO_WRITE_NOT_IMPLEMENTED("CIA-1");
	}
	if (addr < 0xDE00) {	// $DD00 - $DDFF	CIA-2
		RETURN_ON_IO_WRITE_NOT_IMPLEMENTED("CIA-2");
	}
	if (addr < 0xDF00) {	// $DE00 - $DEFF	IO-1 external
		RETURN_ON_IO_WRITE_NOT_IMPLEMENTED("IO-1 external select");
	}
	// The rest: IO-2 external
	RETURN_ON_IO_WRITE_NOT_IMPLEMENTED("IO-2 external select");
}





Uint8 cpu_read ( Uint16 addr )
{
	int blk = addr >> 13;
	int real_addr = map_offset[blk] + addr;
	Uint8 result;
	if (!map_mapped[blk]) {	// it's only applies if block is marked as "not mapped"?
		int hinib = addr >> 12;
		if (
			((vic3_registers[0x30] & 0x80) && (hinib == 0xE || hinib == 0xF)) ||		// ROM at E000 (E000-EFFF? What about F000-FFFF?!)
			((vic3_registers[0x30] & 0x40) && hinib == 0x9)	||	// ROM at 9000
			((vic3_registers[0x30] & 0x20) && hinib == 0xC)	||	// ROM at C000 
			((vic3_registers[0x30] & 0x10) && (hinib == 0xA || hinib == 0xB)) || 		// ROM at A000
			((vic3_registers[0x30] & 0x08) && hinib == 0x8)		// ROM at 8000
		)
			real_addr += 0x20000;
		else if (hinib == 0xD) {	// I/O area
			return io_read(addr);
		}
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
		if (addr >= 0xD000 && addr < 0xE000) {
			io_write(addr, data);
			return;
		}
		if (addr < 2) {
			printf("PORT0: 0/1 port is written! %d data $%02X" NL, addr, data);
			cpu_port[addr] = data;
			return;
		}
	}
	if (real_addr < 0x20000)	// do not write ROM ....
		memory[real_addr] = data;
	printf("CPU write @ $%04X [PC=$%04X] (BLK=%d, REAL=$%05X, mapped=%d) with data $%02X" NL, addr, cpu_pc, blk, real_addr, map_mapped[blk], data);
	if (real_addr >= 0x20000)
		printf("CPU warning! writing into ROM?" NL);
}



void clear_emu_events ( void )
{
}


#define MEMDUMP_FILE	"dump.mem"


static void dump_on_shutdown ( void )
{
	FILE *f;
	int a;
	for (a = 0; a < 0x40; a++)
		printf("VIC-3 register $%02X is %02X" NL, a, vic3_registers[a]);

	f = fopen(MEMDUMP_FILE, "wb");
	if (f) {
		fwrite(memory, 1, sizeof memory, f);
		fclose(f);
		puts("Memory is dumped into " MEMDUMP_FILE);
	}
}




int main ( int argc, char **argv )
{
	int cycles;
	int a;
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
	// Initialize C65 ...
	c65_init();
#if 0
	init_palette();	// get our 4096 colours ...
	memset(memory, 0xFF, sizeof memory);	// initialize memory
	memset(memory + 0xD000, 0, 0x1000);
	memset(vic3_registers, 0xFF, sizeof vic3_registers);
	// load ROM (128K sized)
	if (emu_load_file("c65-system.rom", memory + 0x20000, 0x20001) != 0x20000)
		FATAL("Cannot load C65 system ROM!");
#if 0
	for (a = 0; a < 0x10000; a++) {
		Uint8 exchg = memory[0x20000 | a];
		memory[0x20000 | a] = memory[0x30000 | a];
		memory[0x30000 | a] = exchg;
	}
#endif
#endif
	// Temporary hack! C65 should be started on its own with zero for any MAP related stuff!!!!!
	cpu_mapping(0x20000, 0x20000, 0xBE); // Set initial internal MMU state of 4510 .. I have no idea what should it be, but probably stack / ZP is needed, and also the I/O area ...
	// Now we have ROM, initial memory mapping up, we can try a CPU reset to fetch the reset vector
	cpu_reset();
	cycles = 0;
	for (;;) {
		cycles += cpu_step();
		if (cycles >= 227) {
			scanline++;
			printf("VIC3: new scanline (%d)!" NL, scanline);
			cycles -= 227;
			if (scanline == 312) {
				SDL_Event e;
				puts("VIC3: new frame!");
				scanline = 0;
				while (SDL_PollEvent(&e) != 0) {
					if (e.type == SDL_KEYDOWN || e.type == SDL_QUIT)
						exit(0);
				}
			}
		}
	}
	puts("Goodbye!");
	return 0;
}
