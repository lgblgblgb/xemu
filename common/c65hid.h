/* Test-case for a very simple, inaccurate, work-in-progress Commodore 65 emulator.
   Copyright (C)2016 LGB (Gábor Lénárt) <lgblgblgb@gmail.com>

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

#ifndef __LGB_C65HID_H_INCLUDED
#define __LGB_C65HID_H_INCLUDED

/* Note: HID stands for "Human Input Devices" or something like that :)
   That is: keyboard, joystick, mouse. */

extern Uint8 kbd_matrix[8];	// keyboard matrix state, 8 * 8 bits

extern int hid_key_event ( SDL_Scancode key, int pressed ) ;

extern void hid_reset_events ( int burn ) ;
extern void hid_init ( void ) ;
extern void hid_mouse_motion_event      ( int xrel, int yrel ) ;
extern void hid_mouse_button_event      ( int button, int pressed ) ;
extern void hid_joystick_device_event   ( int which , int is_attach ) ;
extern void hid_joystick_motion_event   ( int is_vertical, int value ) ;
extern void hid_joystick_button_event   ( int pressed ) ;
extern void hid_joystick_hat_event      ( int value ) ;
extern int  hid_read_joystick_up        ( int on, int off ) ;
extern int  hid_read_joystick_down      ( int on, int off ) ;
extern int  hid_read_joystick_left      ( int on, int off ) ;
extern int  hid_read_joystick_right     ( int on, int off ) ;
extern int  hid_read_joystick_button    ( int on, int off ) ;
extern int  hid_read_mouse_button_left  ( int on, int off ) ;
extern int  hid_read_mouse_button_right ( int on, int off ) ;
extern int  hid_read_mouse_rel_x        ( int min, int max ) ;
extern int  hid_read_mouse_rel_y        ( int min, int max ) ;

#endif
