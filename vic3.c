/* Test-case for a very simple and inaccurate and even not working Commodore 65 emulator.
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
static Uint32 vic3_rom_palette[16];	// the "ROM" palette, for C64 colours
static Uint8 vic3_palette_nibbles[0x300];
Uint8 vic3_registers[0x40];
int vic_new_mode;		// VIC3 "newVic" IO mode is activated flag
int scanline;			// current scan line number
int clock_divider7_hack;
static int compare_raster;	// raster compare (9 bits width) data
static int interrupt_status;




void vic3_init ( void )
{
	int r, g, b, i;
	// *** Init 4096 element palette with RGB components for faster access later on palette register changes (avoid SDL calls to convert)
	for (r = 0, i = 0; r < 16; r++)
		for (g = 0; g < 16; g++)
			for (b = 0; b < 16; b++)
				rgb_palette[i++] = SDL_MapRGBA(sdl_pix_fmt, r * 17, g * 17, b * 17, 0xFF);
	SDL_FreeFormat(sdl_pix_fmt);	// thanks, we don't need this anymore.
	// *** Init VIC3 registers and palette
	vic_new_mode = 0;
	interrupt_status = 0;
	scanline = 0;
	compare_raster = 0;
	clock_divider7_hack = 7;
	for (i = 0; i < 0x100; i++) {
		if (i < sizeof vic3_registers)
			vic3_registers[i] = 0;
		vic3_palette[i] = rgb_palette[0];	// black
		vic3_palette_nibbles[i] = 0;
		vic3_palette_nibbles[i + 0x100] = 0;
		vic3_palette_nibbles[i + 0x200] = 0;
	}
	// *** the ROM palette
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
	if (
		(scanline == compare_raster)
//#if 0
		|| (compare_raster == 511 && scanline == 0)
//#endif
	) {
		interrupt_status |= 1;
	} else
		interrupt_status &= 0xFE;
	interrupt_status &= vic3_registers[0x1A];
	vic3_interrupt_checker();
}




void vic3_write_reg ( int addr, Uint8 data )
{
	addr &= 0x3F;
	printf("VIC3: write reg $%02X with data $%02X" NL, addr, data);
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
			puts("MEM: applying new memory configuration because of VIC3 $30 is written");
			apply_memory_config();
			break;
		case 0x31:
			clock_divider7_hack = (data & 64) ? 7 : 2;
			printf("VIC3: clock_divider7_hack = %d" NL, clock_divider7_hack);
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
		case 0x19:
			result = interrupt_status;
			break;
		default:
			result = vic3_registers[addr];
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
void vic3_render_screen ( void )
{
	int tail, charline = 0;
	Uint32 bg, *palette, *p = emu_start_pixel_buffer_access(&tail);
	Uint8 *vidp, *chrg, *colp = memory + 0x1F800;
	int x = 0, y = 0, xlim, ylim;
	// we use the H640 bit only to decide it is C65 or C64 mode render
	// Surely is very wrong!
	if (vic3_registers[0x31] & 128) {
		xlim = 79;
		ylim = 24;
		palette = vic3_palette;
		chrg = memory + 0x28000 + 0x1000;
		vidp = memory + 0x00800;
	} else {
		xlim = 39;
		ylim = 24;
		palette = vic3_rom_palette;
		chrg = memory + 0x2D000;
		vidp = memory + 0x00400;
	}
	bg = palette[vic3_registers[0x21]];
	for (;;) {
		Uint8 chrdata = chrg[((*(vidp++)) << 3) + charline];
		Uint8 coldata = *(colp++);
		Uint32 fg = palette[coldata];
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
