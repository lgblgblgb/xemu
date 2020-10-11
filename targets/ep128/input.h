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

#ifndef XEMU_EP128_INPUT_H_INCLUDED
#define XEMU_EP128_INPUT_H_INCLUDED

#include <SDL_keyboard.h>

#define JOY_SCAN_FIRE1  0
#define JOY_SCAN_UP     1
#define JOY_SCAN_DOWN   2
#define JOY_SCAN_LEFT   3
#define JOY_SCAN_RIGHT  4
#define JOY_SCAN_FIRE2  5
#define JOY_SCAN_FIRE3  6

extern int   mouse_grab, show_keys, mouse_mode;

extern int   mouse_mode_description ( int cfg, char *buffer );
extern void  mouse_reset_button ( void );
extern void  emu_mouse_button ( Uint8 sdl_button, int press );
extern void  emu_mouse_motion ( int dx, int dy );
extern void  emu_mouse_wheel ( int x, int y, int flipped );
extern void  mouse_reset ( void );
extern Uint8 read_control_port_bits ( void );
extern void  mouse_check_data_shift ( Uint8 val );
extern int   mouse_setup ( int cfg );
extern int   emu_kbd ( SDL_Keysym sym, int press );

#endif
