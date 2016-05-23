/* Test-case for a very simple, inaccurate, work-in-progress Commodore 65 emulator.
   Copyright (C)2016 LGB (Gábor Lénárt) <lgblgblgb@gmail.com>

   This is the VIC3 "emulation". Currently it only does some "fixed" stuff
   ignoring vast majority of the VIC3 registers and doing its work based
   on the assumption what VIC3 in C65 should do otherwise :)

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
#include "vic3.h"
#include "emutools.h"

#define RGB(r,g,b) rgb_palette[((r) << 8) | ((g) << 4) | (b)]



static Uint32 rgb_palette[4096];	// all the C65 palette, 4096 colours (SDL pixel format related form)
static Uint32 vic3_palette[0x100];	// VIC3 palette in SDL pixel format related form (can be written into the texture directly to be rendered)
static Uint32 vic3_rom_palette[0x100];	// the "ROM" palette, for C64 colours (with some ticks, ie colours above 15 are the same as the "normal" programmable palette)
static Uint8 vic3_palette_nibbles[0x300];
Uint8 vic3_registers[0x40];
int vic_new_mode;		// VIC3 "newVic" IO mode is activated flag
int scanline;			// current scan line number
int clock_divider7_hack;
static int compare_raster;	// raster compare (9 bits width) data
static int interrupt_status;
int vic2_16k_bank;

static int warn_sprites = 1, warn_bitplanes = 1, warn_attr = 1, warn_ctrl_b_lo = 1;



void vic3_init ( void )
{
	int r, g, b, i;
	// *** Init 4096 element palette with RGB components for faster access later on palette register changes (avoid SDL calls to convert)
	for (r = 0, i = 0; r < 16; r++)
		for (g = 0; g < 16; g++)
			for (b = 0; b < 16; b++)
				rgb_palette[i++] = SDL_MapRGBA(sdl_pix_fmt, r * 17, g * 17, b * 17, 0xFF); // 15*17=255, last arg 0xFF: alpha channel for SDL
	SDL_FreeFormat(sdl_pix_fmt);	// thanks, we don't need this anymore from SDL
	// *** Init VIC3 registers and palette
	vic2_16k_bank = 0;
	vic_new_mode = 0;
	interrupt_status = 0;
	scanline = 0;
	compare_raster = 0;
	clock_divider7_hack = 7;
	for (i = 0; i < 0x100; i++) {	// Initiailize all palette registers to zero, initially, to have something ...
		if (i < sizeof vic3_registers)
			vic3_registers[i] = 0;	// Also the VIC3 registers ...
		vic3_rom_palette[i] = vic3_palette[i] = rgb_palette[0];
		vic3_palette_nibbles[i] = 0;
		vic3_palette_nibbles[i + 0x100] = 0;
		vic3_palette_nibbles[i + 0x200] = 0;
	}
	// *** the ROM palette "fixed" colours
	vic3_rom_palette[ 0] = RGB( 0,  0,  0);	// black
	vic3_rom_palette[ 1] = RGB(15, 15, 15);	// white
	vic3_rom_palette[ 2] = RGB(15,  0,  0);	// red
	vic3_rom_palette[ 3] = RGB( 0, 15, 15);	// cyan
	vic3_rom_palette[ 4] = RGB(15,  0, 15);	// magenta
	vic3_rom_palette[ 5] = RGB( 0, 15,  0);	// green
	vic3_rom_palette[ 6] = RGB( 0,  0, 15);	// blue
	vic3_rom_palette[ 7] = RGB(15, 15,  0);	// yellow
	vic3_rom_palette[ 8] = RGB(15,  6,  0);	// orange
	vic3_rom_palette[ 9] = RGB(10,  4,  0);	// brown
	vic3_rom_palette[10] = RGB(15,  7,  7);	// pink
	vic3_rom_palette[11] = RGB( 5,  5,  5);	// dark grey
	vic3_rom_palette[12] = RGB( 8,  8,  8);	// medium grey
	vic3_rom_palette[13] = RGB( 9, 15,  9);	// light green
	vic3_rom_palette[14] = RGB( 9,  9, 15);	// light blue
	vic3_rom_palette[15] = RGB(11, 11, 11);	// light grey
	puts("VIC3: has been initialized.");
}



static void vic3_interrupt_checker ( void )
{
	int vic_irq_old = cpu_irqLevel & 2;
	int vic_irq_new;
	if (interrupt_status) {
		interrupt_status |= 128;
		vic_irq_new = 2;
	} else {
		vic_irq_new = 0;
	}
	if (vic_irq_old != vic_irq_new) {
		printf("VIC3: interrupt change %s -> %s" NL, vic_irq_old ? "active" : "inactive", vic_irq_new ? "active" : "inactive");
		if (vic_irq_new)
			cpu_irqLevel |= 2;
		else
			cpu_irqLevel &= 255 - 2;
	}
}



void vic3_check_raster_interrupt ( void )
{
	// I'm lame even with VIC2 knowledge it seems
	// C65 seems to use raster interrupt to generate the usual periodic IRQ
	// (which was done with CIA on C64) in raster line 511. However as
	// raster line 511 can never be true, I really don't know what to do.
	// To be able C65 ROM to work, I assume that raster 511 is raster 0.
	// It's possible that this is an NTSC/PAL issue, as raster can be "negative"
	// according to the specification in case of NTSC. I really don't know ...
	if (
		(scanline == compare_raster)
		|| (compare_raster == 511 && scanline == 0)
	) {
		interrupt_status |= 1;
	} else
		interrupt_status &= 0xFE;
	interrupt_status &= vic3_registers[0x1A];
	vic3_interrupt_checker();
}




void vic3_write_reg ( int addr, Uint8 data )
{
	Uint8 old_data;
	addr &= 0x3F;
	old_data = vic3_registers[addr];
	printf("VIC3: write reg $%02X with data $%02X" NL, addr, data);
	if (addr == 0x2F) {
		if (!vic_new_mode && data == 0x96 && old_data == 0xA5) {
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
	switch (addr) {
		case 0x11:
			compare_raster = (compare_raster & 0xFF) | ((data & 1) ? 0x100 : 0);
			printf("VIC3: compare raster is now %d" NL, compare_raster);
			break;
		case 0x12:
			compare_raster = (compare_raster & 0xFF00) | data;
			printf("VIC3: compare raster is now %d" NL, compare_raster);
			break;
		case 0x19:
			interrupt_status = interrupt_status & data & 15 & vic3_registers[0x1A];
			vic3_interrupt_checker();
			break;
		case 0x1A:
			vic3_registers[0x1A] &= 15;
			break;
		case 0x30:
			// Save some un-needed memory translating table rebuilds, if there is no important bits (of us) changed.
			// CRAM@DC00 is not handled by the translator directly, so bit0 does not apply here!
			if (
				(data & 0xF8) != (old_data & 0xF8)
			) {
				puts("MEM: applying new memory configuration because of VIC3 $30 is written");
				apply_memory_config();
			} else
				puts("MEM: no need for new memory configuration (because of VIC3 $30 is written): same ROM bit values are set");
			break;
		case 0x31:
			clock_divider7_hack = (data & 64) ? 7 : 2;
			printf("VIC3: clock_divider7_hack = %d" NL, clock_divider7_hack);
			if ((data & 32) && warn_attr) {
				INFO_WINDOW("VIC3 extended attributes are not emulated yet!");
				warn_attr = 0;
			}
			if ((data & 16) && warn_bitplanes) {
				INFO_WINDOW("VIC3 bitplanes are not emulated yet!");
				warn_bitplanes = 0;
			}
			if ((data & 15) && warn_ctrl_b_lo) {
				INFO_WINDOW("VIC3 control-B register V400, H1280, MONO and INT features are not emulated yet!");
				warn_ctrl_b_lo = 0;
			}
			break;
		case 0x15:
			if (data && warn_sprites) {
				INFO_WINDOW("VIC2 sprites are not emulated yet!");
				warn_sprites = 0;
			}
			break;
	}
}	




Uint8 vic3_read_reg ( int addr )
{
	Uint8 result;
	addr &= 0x3F;
	if (!vic_new_mode && addr > 0x2F) {
		printf("VIC3: ignoring reading register $%02X because of old I/O access mode selected, answer is $FF" NL, addr);
		return 0xFF;
	}
	switch (addr) {
		case 0x11:
			result =  (vic3_registers[0x11] & 0x7F) | ((scanline & 256) ? 0x80 : 0);
			break;
		case 0x12:
			result = scanline & 0xFF;
			break;
		case 0x16:
			result = vic3_registers[addr] | (128 + 64);	// unused bits [TODO: also on VIC3?]
			break;
		case 0x19:
			result = interrupt_status | (64 + 32 + 16);	// unused bits [TODO: also on VIC3?]
			break;
		case 0x1A:
			result = vic3_registers[addr] | 0xF0;		// unused bits [TODO: also on VIC3?]
			break;
		case 0x18:
			result = vic3_registers[addr] | 1;		// unused bit [TODO: also on VIC3?]
			break;
		default:
			result = vic3_registers[addr];
			if (addr >= 0x20 && addr < 0x2F)
				result |= 0xF0;				// unused bits [TODO: also on VIC3?]
			break;
	}
	printf("VIC3: read reg $%02X with result $%02X" NL, addr, result);
	return result;
}





// "num" is 0-$ff for red, $100-$1ff for green and $200-$2ff for blue nibbles
void vic3_write_palette_reg ( int num, Uint8 data )
{
	vic3_palette_nibbles[num] = data & 15;
	// recalculate the given RGB entry based on the new data as well
	vic3_palette[num & 0xFF] = RGB(
		vic3_palette_nibbles[ num & 0xFF],
		vic3_palette_nibbles[(num & 0xFF) | 0x100],
		vic3_palette_nibbles[(num & 0xFF) | 0x200]
	);
	// Also, update the "ROM based" palette struct, BUT only colours above 15,
	// since the lower 16 are "ROM based"! This is only a trick to be able
	// to have full 256 colours for ROMPAL sel and without that too!
	// The low 16 colours are the one which are ROM based for real, that's why
	// we don't want to update them here!
	if ((num & 0xF0))
		vic3_rom_palette[num & 0xFF] = vic3_palette[num & 0xFF];
}



/* FIXME
Primitive "full screen at once" renderer! Only knows the "official" text
mode of C65 native mode and 64 "by default" mode, with standard locations etc.
In C65 mode, not even the hw attributes are supported! Of course, currently
forget about sprites, graphics, bitplanes, everything :)
"ROM palette" is also ignored for now (makes C64 mode somewhat odd)
Memory addresses etc are ignored, and the "default" values are assumed!
Surely, it's for nothing, just to be able to see something initially before
further developments :) VIC3 "FAST" bit is ignored, thus C64 mode will drive
the CPU at C64 speed still, which is also incorrect (C64 mode is too fast).
*/
static void vic2_render_screen_text ( void )
{
	int tail, charline = 0;
	Uint32 bg, *palette, *p = emu_start_pixel_buffer_access(&tail);
	Uint8 *vidp, *chrg, *colp = memory + 0x1F800;
	int x = 0, y = 0, xlim, ylim;
	// TODO: if BPM bit is set in ctrl reg B then bitplane mode is set,
	// which ignores ALL the VIC-2 mode settings. This is not emulated
	// yet though.
	// ---
	// Currently, only text (no MCM, ECM) is supported, H640 bit on/off,
	// and fixed chargen address.
	if (vic3_registers[0x31] & 128) { // check H640 bit: 80 column mode?
		xlim = 79;
		ylim = 24;
		// Fixed character info, heh ... FIXME
		chrg = memory + 0x28000 + 0x1000;
		// Note: according to the specification bit 4 has no effect in 80 columns mode!
		// Note: VIC2 sees ROM at some addresses thing is not emulated yet!
		vidp = memory + ((vic3_registers[0x18] & 0xE0) << 6) + vic2_16k_bank;
	} else {
		xlim = 39;
		ylim = 24;
		// Fixed character info, heh ... FIXME
		chrg = memory + 0x2D000;
		// Note: VIC2 sees ROM at some addresses thing is not emulated yet!
		vidp = memory + ((vic3_registers[0x18] & 0xF0) << 6) + vic2_16k_bank;
	}
	// Palette selection between ROM palette and programmable one
	// FIXME: is it allowed VIC2 modes at all?
	palette = (vic3_registers[0x30] & 4) ? vic3_palette : vic3_rom_palette;
	// Target SDL pixel related format for the background colour
	bg = palette[vic3_registers[0x21] & 15];
	for (;;) {
		Uint8 chrdata = chrg[((*(vidp++)) << 3) + charline];
		Uint8 coldata = *(colp++);
		Uint32 fg = palette[coldata & 15];
		// FIXME: no ECM, MCM stuff ...
		if (xlim == 79) {
			*(p++) = chrdata & 128 ? fg : bg;
			*(p++) = chrdata &  64 ? fg : bg;
			*(p++) = chrdata &  32 ? fg : bg;
			*(p++) = chrdata &  16 ? fg : bg;
			*(p++) = chrdata &   8 ? fg : bg;
			*(p++) = chrdata &   4 ? fg : bg;
			*(p++) = chrdata &   2 ? fg : bg;
			*(p++) = chrdata &   1 ? fg : bg;
		} else {
			p[ 0] = p[ 1] = chrdata & 128 ? fg : bg;
			p[ 2] = p[ 3] = chrdata &  64 ? fg : bg;
			p[ 4] = p[ 5] = chrdata &  32 ? fg : bg;
			p[ 6] = p[ 7] = chrdata &  16 ? fg : bg;
			p[ 8] = p[ 9] = chrdata &   8 ? fg : bg;
			p[10] = p[11] = chrdata &   4 ? fg : bg;
			p[12] = p[13] = chrdata &   2 ? fg : bg;
			p[14] = p[15] = chrdata &   1 ? fg : bg;
			p += 16;
		}
		if (x == xlim) {
			p += tail;
			x = 0;
			if (charline == 7) {
				if (y == ylim)
					break;
				y++;
				charline = 0;
			} else {
				charline++;
				vidp -= xlim + 1;
				colp -= xlim + 1;
			}
		} else
			x++;
	}
	emu_update_screen();
}


// VIC2 bitmap mode, now only HIRES mode (no MCM yet), without H640 VIC3 feature!!
// I am not even sure if H640 would work here, as it needs almost all the 16K of area what VIC-II can see.
// Note: VIC2 sees ROM at some addresses thing is not emulated yet!
static void vic2_render_screen_bmm ( void )
{
	int tail, x = 0, y = 0, charline = 0;
	Uint32 *palette, *p = emu_start_pixel_buffer_access(&tail);
	Uint8 *vidp, *chrp;
	vidp = memory + ((vic3_registers[0x18] & 0xF0) << 6) + vic2_16k_bank;
	chrp = memory + ((vic3_registers[0x18] & 8) ? 8192 : 0) + vic2_16k_bank;
	palette = (vic3_registers[0x30] & 4) ? vic3_palette : vic3_rom_palette;
	for (;;) {
		Uint8  data = *(vidp++);
		Uint32 fg = palette[data & 15];
		Uint32 bg = palette[data >> 4];
		data = *chrp;
		chrp += 8;
		p[ 0] = p[ 1] = data & 128 ? fg : bg;
		p[ 2] = p[ 3] = data &  64 ? fg : bg;
		p[ 4] = p[ 5] = data &  32 ? fg : bg;
		p[ 6] = p[ 7] = data &  16 ? fg : bg;
		p[ 8] = p[ 9] = data &   8 ? fg : bg;
		p[10] = p[11] = data &   4 ? fg : bg;
		p[12] = p[13] = data &   2 ? fg : bg;
		p[14] = p[15] = data &   1 ? fg : bg;
		p += 16;
		if (x == 39) {
			p += tail;
			x = 0;
			if (charline == 7) {
				if (y == 24)
					break;
				y++;
				charline = 0;
				chrp -= 7;
			} else {
				charline++;
				vidp -= 40;
				chrp -= 319;
			}
		} else
			x++;
	}
	emu_update_screen();
}


// Renderer for bit-planes mode
static void vic3_render_screen_bpm ( void )
{
	int tail;
	Uint32 *p = emu_start_pixel_buffer_access(&tail);
	int y;
	for (y = 0; y < 199; y++) {
		int x;
		for (x = 0; x < 639; x++)
			*(p++) = RGB(y & 15, x & 15, (x * y) & 15);
		p += tail;
	}		
	emu_update_screen();
}



void vic3_render_screen ( void )
{
	if (vic3_registers[0x31] & 16)
		vic3_render_screen_bpm();
	else {
		if (vic3_registers[0x11] & 32)
			vic2_render_screen_bmm();
		else
			vic2_render_screen_text();
	}
}

