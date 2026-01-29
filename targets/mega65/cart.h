/* A work-in-progess MEGA65 (Commodore 65 clone origins) emulator
   Part of the Xemu project, please visit: https://github.com/lgblgblgb/xemu
   Copyright (C)2016-2025 LGB (Gábor Lénárt) <lgblgblgb@gmail.com>

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

#ifndef XEMU_MEGA65_CART_H_INCLUDED
#define XEMU_MEGA65_CART_H_INCLUDED

extern void  cart_init        ( void );
extern Uint8 cart_read_byte   ( unsigned int addr );
extern void  cart_write_byte  ( unsigned int addr, Uint8 data );
extern int   cart_attach      ( const char *fn );
extern void  cart_detach      ( void );
extern bool  cart_is_attached ( void );
extern int   cart_info        ( char *p, size_t size );
extern const char *cart_get_fn( void );

#endif
