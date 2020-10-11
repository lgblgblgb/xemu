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

#ifndef XEMU_EP128_SDEXT_H_INCLUDED
#define XEMU_EP128_SDEXT_H_INCLUDED
#ifdef CONFIG_SDEXT_SUPPORT

#define SDEXT_CART_ENABLER_ON	0x10000
#define SDEXT_CART_ENABLER_OFF	1

extern void  sdext_init ( void );
extern Uint8 sdext_read_cart ( Uint16 addr );
extern void  sdext_write_cart ( Uint16 addr, Uint8 data );
extern void  sdext_clear_ram ( void );

extern int   sdext_cart_enabler;
extern char  sdimg_path[PATH_MAX + 1];
extern off_t sd_card_size;

#endif
#endif
