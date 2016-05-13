/* Test-case for a very simple and inaccurate and even not working Commodore 65 emulator.
   Copyright (C)2016 LGB (Gábor Lénárt) <lgblgblgb@gmail.com>

   This is the Commodore 65 emulation. Note: the purpose of this emulator is merely to
   test some 65CE02 opcodes, not for being a *usable* Commodore 65 emulator too much!
   If it ever able to hit the C65 BASIC-10 usage, I'll be happy :)

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


static Uint8 memory[0x110000];		// 65CE02 MAP'able address space (now overflow is not handled, that is the reason of higher than 1MByte, just in case ...)
static int map_offset[8];		// 8K sized block of 65CE02 64K address space mapping offset
static int map_mapped[8];		// is a 8K sized block of 65CE02 64K address space is mapped or not
static Uint32 rgb_palette[4096];	// all the C65 palette, 4096 colours (SDL pixel format related form)
static Uint32 vic3_palette[0x100];	// VIC3 palette in SDL pixel format related form (can be written into the texture directly to be rendered)
static Uint8 vic3_registers[0x40];	// VIC3 registers
static Uint8 vic3_palette_r[0x100], vic3_palette_g[0x100], vic3_palette_b[0x100];	// RGB nibbles of palette (0-15)
static int vic_new_mode;		// VIC3 "newVic" IO mode is activated flag
static Uint8 cpu_port[2];		// CPU I/O port at 0/1 (implemented by the VIC3 for real, on C65)
static int scanline;			// current scan line number





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
	// *** Init 4096 element palette with RGB components for faster access later on palette register changes (avoid SDL calls to convert)
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



// *** Implements the MAP opcode of 4510, called by the 65CE02 emulator
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



// *** Implements the EOM opcode of 4510, called by the 65CE02 emulator
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
#define SET_VIC3_PALETTE_ENTRY(num) \
	do { vic3_palette[num] = RGB(vic3_palette_r[num], vic3_palette_g[num], vic3_palette_b[num]); \
	printf("VIC3: palette #$%02X is set to RGB nibbles $%X%X%X" NL, num, vic3_palette_r[num], vic3_palette_g[num], vic3_palette_b[num]); \
	} while (0)



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
			vic3_palette_r[addr & 0xFF] = data & 15;	// FIXME: bg/fg bit: what is that? (bit 4, but I ignore that now)
			SET_VIC3_PALETTE_ENTRY(addr & 0xFF);
			return;
		} else
			RETURN_ON_IO_WRITE_NO_NEW_VIC_MODE("palette red nibbles");
	}
	if (addr < 0xD300) {	// $D200 - $D200	palette green nibbles (*)
		if (vic_new_mode) {
			vic3_palette_g[addr & 0xFF] = data & 15;
			SET_VIC3_PALETTE_ENTRY(addr & 0xFF);
			return;
		} else
			RETURN_ON_IO_WRITE_NO_NEW_VIC_MODE("palette green nibbles");
	}
	if (addr < 0xD400) {	// $D300 - $D300	palette blue nibbles (*)
		if (vic_new_mode) {
			vic3_palette_b[addr & 0xFF] = data & 15;
			SET_VIC3_PALETTE_ENTRY(addr & 0xFF);
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



// Checks if given un-'MAP'-ped address is mapped by VIC3 reg $30 "ROM" stuffs, and to what offset (0 = not mapped)
static inline int check_vic3_reg30_mapped_rom ( int hinib )
{
	if (((vic3_registers[0x30] & 0x20) && hinib == 0xC))	// "interface" ROM, 'ROM@C000' bit of VIC3 reg $30 for $C000-$CFFF
		return 0x20000;
	if (
		((vic3_registers[0x30] & 0x80) && (hinib == 0xE || hinib == 0xF)) ||	// ROM@E000
		((vic3_registers[0x30] & 0x40) &&  hinib == 0x9                 ) ||	// CROM@9000 ???? Dunno!!! FIXME: C65 doc mention it as "CROM" and separated bit?!
		//((vic3_registers[0x30] & 0x20) &&  hinib == 0xC                 ) ||	// ROM@C000
		((vic3_registers[0x30] & 0x10) && (hinib == 0xA || hinib == 0xB)) ||	// ROM@A000
		((vic3_registers[0x30] & 0x08) &&  hinib == 0x8                 )	// ROM@8000
	)
		return 0x30000;
	return 0;
}


// This function is called by the 65CE02 emulator in case of reading a byte (regardless of data or code)
Uint8 cpu_read ( Uint16 addr )
{
	int blk = addr >> 13;
	int real_addr = map_offset[blk] + addr;	// in case of not mapped, offset will be zero, thus real_addr == addr
	Uint8 result;
	if (!map_mapped[blk]) {	// it's only applies if block is marked as "not mapped". Mapped blocks are not checked at all.
		int hinib = addr >> 12;
		int reg30_mapping = check_vic3_reg30_mapped_rom(hinib);
		// check if mapped via VIC3 reg $30
		if (reg30_mapping)
			real_addr += reg30_mapping;
		else if (addr < 2) {
			printf("PORT0: reading CPU port %d, data is $%02X" NL, addr, cpu_port[addr]);
			return cpu_port[addr];
		} else if (hinib >= 0xE) {	// read op at unmapped, not VIC3 reg30 mapped $E000 - $FFFF
			if (((cpu_port[1] & 3) > 1))	// low two bits of CPU port is binary '10 or '11
				real_addr += 0x20000;	// FIXME: read KERNAL ROM?
		} else if (hinib == 0xD) {		// read op at unmapped, not VIC3 reg30 mapped $D000 - $DFFF
			switch (cpu_port[1] & 7) {
				case 0: case 4:
					break;		// read RAM! (pass-through unmapped address, ie bank zero)
				case 5: case 6: case 7:
					return io_read(addr);
				default: // that is: cases 1, 2, 3
					real_addr += 0x20000; // FIXME: read character ROM!
					break;
			}
		}
		if (hinib == 0xA || hinib == 0xB0) {	// read op at unmapped, not VIC30 reg30 mapped $A000 - $BFFF
			if ((cpu_port[1] & 3) == 3)
				real_addr += 0x20000; // FIXME: read BASIC ROM
		}
	}
	result = memory[real_addr];
	printf("CPU: read @ $%04X [PC=$%04X] (BLK=%d, REAL=$%05X, mapped=%d) result is $%02X" NL, addr, cpu_pc, blk, real_addr, map_mapped[blk], result);
	return result;
}



// This function is called by the 65CE02 emulator in case of writing a byte
void cpu_write ( Uint16 addr, Uint8 data )
{
	int blk = addr >> 13;
	int real_addr = map_offset[blk] + addr; // in case of not mapped, offset will be zero, thus real_addr == addr
	if (!map_mapped[blk]) {
		int hinib = addr >> 12;
		if (check_vic3_reg30_mapped_rom(hinib)) {
			printf("CPU: ignoring write to VIC3 mapped ROM [??] FIXME: should we write RAM then?!" NL); // FIXME: ??? should we write RAM instead, of it's OK to ignore now?
			return;
		}
		// unmapped write accesses have only one special case, if I/O is targeted, since CPU port mapped ROMs cannot be written and RAM is written instead
		if ((cpu_port[1] & 7) > 4 && hinib == 0xD) {
			io_write(addr, data);
			return;
		}
		// if the CPU port (and its DDR) is written ...
		if (addr < 2) {
			printf("PORT0: 0/1 port is written! %d data $%02X" NL, addr, data);
			if (addr) {
				cpu_port[1] = (cpu_port[1] & (255 - cpu_port[0])) | (data & cpu_port[0]);
				printf("PORT0: current setting is b%d%d%d" NL,
					cpu_port[1] & 4 ? 1 : 0,
					cpu_port[1] & 2 ? 1 : 0,
					cpu_port[1] & 1
				);
			} else
				cpu_port[0] = data;
			return;
		}
		// otherwise pass through, real_addr will be the input addr in case of not mapped address
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
	// Dump memory, so some can inspect the result (especially only RAM is interesting of course in BANK 0 and 1)
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
		SCREEN_FORMAT,			// pixel format
		0,				// we have *NO* pre-defined colours (too many we need). we want to do this ourselves!
		NULL,				// -- "" --
		NULL,				// -- "" --
		RENDER_SCALE_QUALITY,		// render scaling quality
		USE_LOCKED_TEXTURE,		// 1 = locked texture access
		dump_on_shutdown		// registered shutdown function
	))
		return 1;
	// Initialize C65 ...
	c65_init();
	// Start!!
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
