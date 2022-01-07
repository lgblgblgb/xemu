/* A work-in-progess MEGA65 (Commodore-65 clone origins) emulator
   Part of the Xemu project, please visit: https://github.com/lgblgblgb/xemu
   Copyright (C)2016-2022 LGB (Gábor Lénárt) <lgblgblgb@gmail.com>

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

#ifndef XEMU_MEGA65_INPUT_DEVICES_H_INCLUDED
#define XEMU_MEGA65_INPUT_DEVICES_H_INCLUDED

extern void  clear_emu_events  ( void );
extern Uint8 cia1_in_b         ( void );
extern Uint8 cia1_in_a         ( void );
extern void  input_init        ( void );
extern int   emu_callback_key  ( int pos, SDL_Scancode key, int pressed, int handled );

extern void  input_toggle_joy_emu  ( void );

extern Uint8 hwa_kbd_get_last      ( void );
extern Uint8 hwa_kbd_get_modifiers ( void );
extern void  hwa_kbd_move_next     ( void );
extern void  hwa_kbd_fake_key      ( Uint8 k );

extern void  virtkey               ( Uint8 rno, Uint8 scancode );

extern Uint8 kbd_directscan_query  ( Uint8 row );

extern void  kbd_trigger_restore_trap ( void );

extern Uint8 get_mouse_x_via_sid ( void );
extern Uint8 get_mouse_y_via_sid ( void );

#endif
