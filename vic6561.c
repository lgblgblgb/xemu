/* Test-case for a very simple and inaccurate Commodore VIC-20 emulator using SDL2 library.
   Copyright (C)2016 LGB (Gábor Lénárt) <lgblgblgb@gmail.com>

   This is the VIC-20 emulation. Note: the source is overcrowded with comments by intent :)
   That it can useful for other people as well, or someone wants to contribute, etc ...

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

#include "emutools.h"
#include "commodore_vic20.h"
#include "vic6561.h"


#define SCREEN_COLOUR	vic_cpal[0]
#define BORDER_COLOUR	vic_cpal[1]
#define SRAM_COLOUR	vic_cpal[2]
#define AUX_COLOUR	vic_cpal[3]

Uint32 vic_palette[16];				// VIC palette with native SCREEN_FORMAT aware way. Must be initialized by the emulator
int scanline;					// scanline counter, must be maintained by the emulator, as increment, checking for all scanlines done, etc, BUT it is zeroed here in vic_vsync()!
static int charline;				// current character line (0-7 for 8 pixels, 0-15 for 16 pixels height characters)
static Uint32 *pixels;				// pointer to our work-texture (ie: the visible emulated VIC-20 screen, DWORD / pixel)
static int pixels_tail;				// "tail" (in DWORDs) after each texture line (must be added to pixels pointer after rendering one scanline)
static int first_active_scanline;		// first "active" (ie not top border) scanline
static int first_bottom_border_scanline;	// first scanline of the bottom border (after the one of last actie display scanline)
static int vic_vertical_area;			// vertical "area" of VIC (1 = upper border, 0 = active display, 2 = bottom border)
static Uint32 vic_cpal[4];			// SDL pixel format related colours of multicolour colours, reverse mode swaps the colours here!! Also used to render everything on the screen!
static int char_height_minus_one;		// 7 or 15 (for 8 and 16 pixels height characters)
static int char_height_shift;			// 3 for 8 pixels height characters, 4 for 16
static int first_active_dotpos;			// first dot within a scanline which belongs to the "active" (not top and bottom border) area
static int text_columns;			// screen text columns
static int sram_colour_index;			// index in the "multicolour" table for data read from colour SRAM (depends on reverse mode!)
static Uint16 vic_p_scr;			// memory address of screen
static Uint16 vic_p_col;			// memory address of colour
static Uint16 vic_p_chr;			// memory address of characer data




/* To be honest, I am lame with VIC-I addressing ...
   I got these five "one-liners" from Sven's shadowVIC emulator (with his permission), thanks a lot!!! */
static inline Uint16 vic_get_address (Uint8 bits10to12) {
	return ((bits10to12 & 7) | ((bits10to12 & 8) ? 0 : 32)) << 10;
}
static inline Uint16 vic_get_chrgen_address ( void ) {
	return vic_get_address(memory[0x9005] & 0xF);
}
static inline Uint16 vic_get_address_bit9 ( void ) {
	return (memory[0x9002] & 0x80) << 2;
}
static inline Uint16 vic_get_screen_address ( void ) {
	return vic_get_address(memory[0x9005] >> 4) | vic_get_address_bit9();
}
static inline Uint16 vic_get_colour_address ( void ) {
	return 0x9400 | vic_get_address_bit9();
}



// Read VIC-I register, used by cpu_read()
Uint8 cpu_vic_reg_read ( int addr )
{
	if (addr == 4)
		return scanline >> 1;	// scanline counter is read (bits 8-1)
	else if (addr == 3)
		return (memory[0x9003] & 0x7F) | ((scanline & 1) ? 0x80 : 0);	// high byte of reg 3 is the bit0 of scanline counter!
	else
		return memory[0x9000 + addr];	// otherwise, just read the "backend" (see cpu_vic_reg_write() for more information)
}



// Write VIC-I register, used by cpu_write()
void cpu_vic_reg_write ( int addr, Uint8 data )
{
	// first, we store value in the "RAM" but it's not a real VIC-20 RAM, only for our emulation purposes ("backend" RAM)
	// so it's OK to even write the scanline counter, as the vic_read() won't use this backend in this case!
	memory[0x9000 + addr] = data;
	// Also update our variables, so access is faster/easier this way in our renderer.
	switch (addr) {
		case 0: // screen X origin (in 4 pixel units) on lower 7 bits, bit 7: interlace (only for NTSC, so it does not apply here, we emulate PAL)
			first_active_dotpos = (data & 0x7F) * 4 + SCREEN_ORIGIN_DOTPOS;
			break;
		case 1:	// screen Y orgin (in 2 lines units) for all the 8 bits
			first_active_scanline = (data << 1) + SCREEN_ORIGIN_SCANLINE;
			first_bottom_border_scanline = (((memory[0x9003] >> 1) & 0x3F) << char_height_shift) + first_active_scanline;
			if (first_bottom_border_scanline > 311)
				first_bottom_border_scanline = 311;
			break;
		case 2:	// number of video columns (lower 7 bits), bit 7: bit 9 of screen memory
			text_columns = data & 0x7F;
			if (text_columns > 32)
				text_columns = 32;
			break;
		case 3: // 
			char_height_minus_one = (data & 1) ? 15 : 7;
			char_height_shift = (data & 1) ? 4 : 3;
			first_bottom_border_scanline = (((data >> 1) & 0x3F) << char_height_shift) + first_active_scanline;
			if (first_bottom_border_scanline > 311)
				first_bottom_border_scanline = 311;
			break;
		case 14:
			AUX_COLOUR = vic_palette[data >> 4];
			break;
		case 15:
			BORDER_COLOUR = vic_palette[data & 7];
			if (data & 8) {	// normal mode
				SCREEN_COLOUR = vic_palette[data >> 4];
				sram_colour_index = 2;
			} else {	// reverse mode
				SRAM_COLOUR = vic_palette[data >> 4];
				sram_colour_index = 0;
			}
			break;
	}
}


// Should be called before each new (half) frame
void vic_vsync ( int relock_texture )
{
	if (relock_texture)
		pixels = emu_start_pixel_buffer_access(&pixels_tail);	// get texture access stuffs for the new frame
	scanline = 0;
	charline = 0;
	vic_vertical_area = 1;
	// maybe this is incorrect, and these can change within a frame too by writing the corresponding VIC-I registers
	vic_p_scr = vic_get_screen_address();
	vic_p_col = vic_get_colour_address();
	vic_p_chr = vic_get_chrgen_address();
	printf("VIC-I addrs: scr=$%04X col=$%04X chr=$%04X" NL, vic_p_scr, vic_p_col, vic_p_chr);
}



void vic_init ( void )
{
	int a;
	vic_vsync(1);	// we need once, since this is the first time, this will be called in update_emulator() later ....
	for (a = 0; a < 16; a++)
		cpu_vic_reg_write(a, 0);	// initialize VIC-I registers
}



// Render a single scanline of VIC-I screen.
// It's not a correct solution to render a line in once, however it's only a sily emulator try from me, not an accurate one :-D
void vic_render_line ( void )
{
	Uint8 bitp, chr;
	int v_columns, v_scr, v_col, dotpos, visible_scanline, mcm;
	// Check for start the active display (end of top border) and end of active display (start of bottom border)
	if (scanline == first_bottom_border_scanline && vic_vertical_area == 0)
		vic_vertical_area = 2;	// this will be the first scanline of bottom border
	else if (scanline == first_active_scanline && vic_vertical_area == 1)
		vic_vertical_area = 0;	// this scanline will be the first non-border scanline
	// Check if we're inside the top or bottom area, so full border colour lines should be rendered
	visible_scanline = (scanline >= SCREEN_FIRST_VISIBLE_SCANLINE && scanline <= SCREEN_LAST_VISIBLE_SCANLINE);
	if (vic_vertical_area) {
		if (visible_scanline) {
			v_columns = SCREEN_WIDTH;
			while (v_columns--)
				*(pixels++) = BORDER_COLOUR;
			pixels += pixels_tail;		// add texture "tail" (that is, pitch - text_width, in 4 bytes uints, ie Uint32 pointer ...)
		}
		return;
	}
	// So, we are at the "active" display area. But still, there are left and right borders ...
	bitp = 128;
	v_columns = text_columns;
	v_scr = vic_p_scr;
	v_col = vic_p_col;
	for (dotpos = 0; dotpos < 284; dotpos++) {
		int visible_dotpos = (dotpos >= SCREEN_FIRST_VISIBLE_DOTPOS && dotpos <= SCREEN_LAST_VISIBLE_DOTPOS && visible_scanline);
		if (dotpos < first_active_dotpos) {
			if (visible_dotpos)
				*(pixels++) = BORDER_COLOUR;
		} else {
			if (v_columns) {
				if (bitp == 128) {
					chr = memory[(memory[v_scr++] << char_height_shift) + vic_p_chr + charline];
					mcm = memory[v_col++];
					vic_cpal[sram_colour_index] = vic_palette[mcm & 7];
					mcm &= 8;	// mcm: multi colour mode flag
					if (mcm)
						bitp = 6;	// in case of mcm, bitp is a shift counter needed to move bits to bitpos 1,0
				}
				if (visible_dotpos) {
					if (mcm) {
						// Ugly enough! I have the assumption that visible dotpos condition only changes on even number in dotpos only ... :-/
						// That is, you MUST define texture dimensions for width of even number!!
						*pixels = *(pixels + 1) = vic_cpal[(chr >> bitp) & 3]; // "double width" pixel ...
						pixels += 2;
						dotpos++; // trick our "for" loop a bit here ...
					} else
						*(pixels++) = (chr & bitp) ? SRAM_COLOUR : SCREEN_COLOUR;
				}
				if (bitp <= 1) {
					v_columns--;
					bitp = 128;
				} else {
					if (mcm)
						bitp -= 2;
					else
						bitp >>= 1;
				}
			} else if (visible_dotpos)
				*(pixels++) = BORDER_COLOUR;
		}
	}
	if (charline >= char_height_minus_one) {
		charline = 0;
		vic_p_scr += text_columns;
		vic_p_col += text_columns;
	} else {
		charline++;
	}
	pixels += pixels_tail;		// add texture "tail" (that is, pitch - text_width, in 4 bytes uints, ie Uint32 pointer ...)
}
