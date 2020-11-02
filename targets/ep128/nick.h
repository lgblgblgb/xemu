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

#ifndef XEMU_EP128_NICK_H_INCLUDED
#define XEMU_EP128_NICK_H_INCLUDED

#include <SDL_types.h>

#define NICK_SLOTS_PER_SEC 889846

extern int vsync;

extern int   nick_init ( void );
extern Uint8 nick_get_last_byte ( void );
extern void  nick_set_border ( Uint8 bcol );
extern void  nick_set_bias ( Uint8 value );
extern void  nick_set_lptl ( Uint8 value );
extern void  nick_set_lpth ( Uint8 value );
extern void  nick_set_frameskip ( int val );
extern char  *nick_dump_lpt ( const char *newline_seq );
extern void  nick_render_slot ( void );
extern void  screenshot ( void );

#endif
