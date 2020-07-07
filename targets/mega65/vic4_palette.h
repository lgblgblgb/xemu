/* A work-in-progess MEGA65 (Commodore 65 clone origins) emulator
   Part of the Xemu project, please visit: https://github.com/lgblgblgb/xemu
   Copyright (C)2016-2020 LGB (Gábor Lénárt) <lgblgblgb@gmail.com>

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

#ifndef XEMU_MEGA65_VIC4_PALETTE_H_INCLUDED
#define XEMU_MEGA65_VIC4_PALETTE_H_INCLUDED

// 256 entries of 4 banks + 256 entries of one extra bank for ROM palette emulation
#define NO_OF_PALETTE_REGS	0x500

extern Uint8  vic_palette_bytes_red  [NO_OF_PALETTE_REGS];
extern Uint8  vic_palette_bytes_green[NO_OF_PALETTE_REGS];
extern Uint8  vic_palette_bytes_blue [NO_OF_PALETTE_REGS];
extern Uint32 vic_palettes[NO_OF_PALETTE_REGS];

extern Uint32 *palette;		// the current used palette for video/text (points into vic_palettes)
extern Uint32 *spritepalette;	// the current used palette for sprites (points into vic_palettes)
extern Uint32 *altpalette;
extern unsigned int palregaccofs;

extern void  vic3_write_palette_reg_red   ( unsigned int num, Uint8 data );
extern void  vic3_write_palette_reg_green ( unsigned int num, Uint8 data );
extern void  vic3_write_palette_reg_blue  ( unsigned int num, Uint8 data );
extern void  vic4_write_palette_reg_red   ( unsigned int num, Uint8 data );
extern void  vic4_write_palette_reg_green ( unsigned int num, Uint8 data );
extern void  vic4_write_palette_reg_blue  ( unsigned int num, Uint8 data );
extern Uint8 vic4_read_palette_reg_red    ( unsigned int num );
extern Uint8 vic4_read_palette_reg_green  ( unsigned int num );
extern Uint8 vic4_read_palette_reg_blue   ( unsigned int num );

extern void  vic4_init_palette ( void );
extern void  vic4_revalidate_all_palette ( void );
extern void  check_if_rom_palette ( int rom_pal );

#endif
