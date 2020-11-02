/* Minimalistic Enterprise-128 emulator with focus on "exotic" hardware
   Part of the Xemu project, please visit: https://github.com/lgblgblgb/xemu
   Copyright (C)2015-2016,2020 LGB (Gábor Lénárt) <lgblgblgb@gmail.com>

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

#ifndef XEMU_EP128_PRIMOEMU_H_INCLUDED
#define XEMU_EP128_PRIMOEMU_H_INCLUDED

#define PRIMO_LPT_SEG	0xFF
#define PRIMO_VID_SEG	0xFC
#define PRIMO_MEM2_SEG	0xFD
#define PRIMO_MEM1_SEG	0xFE
#define PRIMO_ROM_SEG	primo_rom_seg

extern int primo_nmi_enabled, primo_on, primo_rom_seg;

extern void  primo_write_io ( Uint8 port, Uint8 data );
extern Uint8 primo_read_io ( Uint8 port );
extern void  primo_switch ( Uint8 data );
extern void  primo_emulator_execute ( void );
extern void  primo_emulator_exit ( void );
extern int   primo_search_rom ( void );

#endif
