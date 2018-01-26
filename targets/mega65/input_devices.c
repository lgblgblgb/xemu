/* A work-in-progess Mega-65 (Commodore-65 clone origins) emulator
   Part of the Xemu project, please visit: https://github.com/lgblgblgb/xemu
   Copyright (C)2016-2018 LGB (Gábor Lénárt) <lgblgblgb@gmail.com>

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
#include "xemu/emutools_hid.h"
#include "xemu/c64_kbd_mapping.h"
#include "io_mapper.h"
#include "mega65.h"



void clear_emu_events ( void )
{
	hid_reset_events(1);
}


Uint8 cia1_in_b ( void )
{
	return c64_keyboard_read_on_CIA1_B(
		cia1.PRA | (~cia1.DDRA),
		cia1.PRB | (~cia1.DDRB),
		joystick_emu == 1 ? c64_get_joy_state() : 0xFF
	);
}


Uint8 cia1_in_a ( void )
{
	return c64_keyboard_read_on_CIA1_A(
		cia1.PRB | (~cia1.DDRB),
		cia1.PRA | (~cia1.DDRA),
		joystick_emu == 2 ? c64_get_joy_state() : 0xFF
	);
}


// Called by emutools_hid!!! to handle special private keys assigned to this emulator
int emu_callback_key ( int pos, SDL_Scancode key, int pressed, int handled )
{
	printf("HID: pos = %d sdl_key = %d, pressed = %d, handled = %d" NL, pos, key, pressed, handled);
	// Check for special, emulator-related hot-keys (not C65 key)
	if (pressed) {
		if (key == SDL_SCANCODE_F10)
			reset_mega65();
		else if (key == SDL_SCANCODE_KP_ENTER) {
			c64_toggle_joy_emu();
			OSD(-1, -1, "Joystick emulation on port #%d", joystick_emu);
		} else if (key == SDL_SCANCODE_ESCAPE)
			set_mouse_grab(SDL_FALSE);
	} else
		if (pos == -2 && key == 0) {	// special case pos = -2, key = 0, handled = mouse button (which?) and release event!
			if (handled == SDL_BUTTON_LEFT) {
				OSD(-1, -1, "Mouse grab activated.\nPress ESC to cancel.");
				set_mouse_grab(SDL_TRUE);
			}
		}
	return 0;
}
