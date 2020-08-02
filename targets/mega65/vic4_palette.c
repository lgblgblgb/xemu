/* A work-in-progess MEGA65 (Commodore 65 clone origins) emulator
   Part of the Xemu project, please visit: https://github.com/lgblgblgb/xemu
   Copyright (C)2016-2020 LGB (Gábor Lénárt) <lgblgblgb@gmail.com>

   MEGA65 palette handling for VIC-IV with compatibility for C65 style
   VIC-III palette.

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
#include "vic4_palette.h"
#include "vic4.h"


// C65/M65 compatible layout of the palette registers, the SDL centric one is the same, but in SDL pixel format: vic_palettes
Uint8  vic_palette_bytes_red  [NO_OF_PALETTE_REGS];
Uint8  vic_palette_bytes_green[NO_OF_PALETTE_REGS];
Uint8  vic_palette_bytes_blue [NO_OF_PALETTE_REGS];
Uint32 vic_palettes[NO_OF_PALETTE_REGS];	// SDL texture format compatible entries of all the four palettes of the MEGA65
Uint32 *palette = vic_palettes;	// the current used palette for video/text (points into vic_palettes)
Uint32 *spritepalette = vic_palettes;	// the current used palette for sprites (points into vic_palettes)
Uint32 *altpalette = vic_palettes;
static struct {
	Uint32 red_shift,   red_mask,   red_revmask;
	Uint32 green_shift, green_mask, green_revmask;
	Uint32 blue_shift,  blue_mask,  blue_revmask;
	Uint32 alpha_shift, alpha_mask, alpha_revmask;
} sdlpalinfo;
unsigned int palregaccofs;


static XEMU_INLINE Uint8 swap_nibbles ( Uint8 i )
{
	return ((i & 0x0F) << 4) | ((i & 0xF0) >> 4);
}


void vic4_revalidate_all_palette ( void )
{
	for (int i = 0; i < NO_OF_PALETTE_REGS; i++)
		vic_palettes[i] =
			sdlpalinfo.alpha_mask |
			((swap_nibbles(vic_palette_bytes_red  [i] & 0xEF)) << sdlpalinfo.red_shift  ) |
			( swap_nibbles(vic_palette_bytes_green[i])         << sdlpalinfo.green_shift) |
			( swap_nibbles(vic_palette_bytes_blue [i])         << sdlpalinfo.blue_shift ) ;
}


void vic4_init_palette ( void )
{
	sdlpalinfo.red_shift     = sdl_pix_fmt->Rshift;
	sdlpalinfo.red_mask      = 0xFFU << sdlpalinfo.red_shift;
	sdlpalinfo.red_revmask   = ~sdlpalinfo.red_mask;
	sdlpalinfo.green_shift   = sdl_pix_fmt->Gshift;
	sdlpalinfo.green_mask    = 0xFFU << sdlpalinfo.green_shift;
	sdlpalinfo.green_revmask = ~sdlpalinfo.green_mask;
	sdlpalinfo.blue_shift    = sdl_pix_fmt->Bshift;
	sdlpalinfo.blue_mask     = 0xFFU << sdlpalinfo.blue_shift;
	sdlpalinfo.blue_revmask  = ~sdlpalinfo.blue_mask;
	sdlpalinfo.alpha_shift   = sdl_pix_fmt->Ashift;
	sdlpalinfo.alpha_mask    = 0xFFU << sdlpalinfo.alpha_shift;
	sdlpalinfo.alpha_revmask = ~sdlpalinfo.alpha_mask;
	// Only for checking, this should never happen:
	if (
		sdlpalinfo.red_mask   != sdl_pix_fmt->Rmask ||
		sdlpalinfo.green_mask != sdl_pix_fmt->Gmask ||
		sdlpalinfo.blue_mask  != sdl_pix_fmt->Bmask ||
		sdlpalinfo.alpha_mask != sdl_pix_fmt->Amask
	)
		FATAL("SDL palette problem!");
#ifdef DO_INIT_PALETTE
	// DO_INIT_PALETTE: I would say, we don't need to initialize palette,
	// since Hyppo will do it. So DO_INIT_PALETTE is currently not defined.
	static const Uint8 def_pal[] = {
		 0,  0,  0,	// black
		15, 15, 15,	// white
		15,  0,  0,	// red
		 0, 15, 15,	// cyan
		15,  0, 15,	// magenta
		 0, 15,  0,	// green
		 0,  0, 15,	// blue
		15, 15,  0,	// yellow
		15,  6,  0,	// orange
		10,  4,  0,	// brown
		15,  7,  7,	// pink
		 5,  5,  5,	// dark grey
		 8,  8,  8,	// medium grey
		 9, 15,  9,	// light green
		 9,  9, 15,	// light blue
		11, 11, 11	// light grey
	};
	for (int i = 0; i < NO_OF_PALETTE_REGS; i++) {
		vic_palette_bytes_red[i]   = (def_pal[(i & 0xF) * 3 + 0] * 17) & 0xEF;
		vic_palette_bytes_green[i] =  def_pal[(i & 0xF) * 3 + 1] * 17;
		vic_palette_bytes_blue[i]  =  def_pal[(i & 0xF) * 3 + 2] * 17;
	}
#else
	for (int i = 0; i < NO_OF_PALETTE_REGS; i++) {
		vic_palette_bytes_red  [i] = 0;
		vic_palette_bytes_green[i] = 0;
		vic_palette_bytes_blue [i] = 0;
	}
#endif
	vic4_revalidate_all_palette();
	palette = vic_palettes;	// the current used palette for video/text (points into vic_palettes)
	spritepalette = vic_palettes;	// the current used palette for sprites (points into vic_palettes)
	altpalette = vic_palettes;
	palregaccofs = 0;
}



void vic4_write_palette_reg_red ( unsigned int num, Uint8 data )
{
	num = (num & 0xFF) + palregaccofs;
	vic_palette_bytes_red[num] = data;
	vic_palettes[num] = (vic_palettes[num] & sdlpalinfo.red_revmask) | ((swap_nibbles(data & 0xEF)) << sdlpalinfo.red_shift);
	if (num >= 0x300 && num <= 0x30F) 	// first 16 entries of bank #3 forms the "ROM palette" of C65
		vic_palettes[num + 0x100] = vic_palettes[num];
	if (num >= 0x10 && num <= 0xFF)		// rest of the entires of bank #0, also the "ROM palette" emulation stuff
		vic_palettes[num + 0x400] = vic_palettes[num];
}

void vic4_write_palette_reg_green ( unsigned int num, Uint8 data )
{
	num = (num & 0xFF) + palregaccofs;
	vic_palette_bytes_green[num] = data;
	vic_palettes[num] = (vic_palettes[num] & sdlpalinfo.green_revmask) | (swap_nibbles(data) << sdlpalinfo.green_shift);
	if (num >= 0x300 && num <= 0x30F)
		vic_palettes[num + 0x100] = vic_palettes[num];
	if (num >= 0x10 && num <= 0xFF)
		vic_palettes[num + 0x400] = vic_palettes[num];
}

void vic4_write_palette_reg_blue  ( unsigned int num, Uint8 data )
{
	num = (num & 0xFF) + palregaccofs;
	vic_palette_bytes_blue[num] = data;
	vic_palettes[num] = (vic_palettes[num] & sdlpalinfo.blue_revmask) | (swap_nibbles(data) << sdlpalinfo.blue_shift);
	if (num >= 0x300 && num <= 0x30F)
		vic_palettes[num + 0x100] = vic_palettes[num];
	if (num >= 0x10 && num <= 0xFF)
		vic_palettes[num + 0x400] = vic_palettes[num];
}

void vic3_write_palette_reg_red   ( unsigned int num, Uint8 data )
{
	vic4_write_palette_reg_red  (num, (((data & 0xF) * 17) & 0xEF) | (data & 0x10));
}

void vic3_write_palette_reg_green ( unsigned int num, Uint8 data )
{
	vic4_write_palette_reg_green(num, (data & 0xF) * 17);
}

void vic3_write_palette_reg_blue  ( unsigned int num, Uint8 data )
{
	vic4_write_palette_reg_blue (num, (data & 0xF) * 17);
}

Uint8 vic4_read_palette_reg_red ( unsigned int num )
{
	return vic_palette_bytes_red[(num & 0xFF) + palregaccofs];
}

Uint8 vic4_read_palette_reg_green ( unsigned int num )
{
	return vic_palette_bytes_green[(num & 0xFF) + palregaccofs];
}

Uint8 vic4_read_palette_reg_blue ( unsigned int num )
{
	return vic_palette_bytes_blue[(num & 0xFF) + palregaccofs];
}

void check_if_rom_palette ( int rom_pal )
{
	if (rom_pal) {	// ROM palette turned on ...
		// ... but some pointers points to bank#0: then set to our emulated rom bank
		if (palette == vic_palettes)
			palette = vic_palettes + 0x400;
		if (spritepalette == vic_palettes)
			spritepalette = vic_palettes + 0x400;
		if (altpalette == vic_palettes)
			altpalette = vic_palettes + 0x400;
	} else {	// ROM palette turned off
		// the opposite as the previous things ...
		if (palette == vic_palettes + 0x400)
			palette = vic_palettes;
		if (spritepalette == vic_palettes + 0x400)
			spritepalette = vic_palettes;
		if (altpalette == vic_palettes + 0x400)
			altpalette = vic_palettes;
	}
}
