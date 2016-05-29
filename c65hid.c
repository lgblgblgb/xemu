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

#include <stdio.h>

#include <SDL.h>

#include "commodore_65.h"
#include "c65hid.h"
#include "emutools.h"


/* Note: HID stands for "Human Input Devices" or something like that :)
   That is: keyboard, joystick, mouse. */


Uint8 kbd_matrix[8];		// keyboard matrix state, 8 * 8 bits

struct KeyMapping {
	SDL_Scancode	scan;		// SDL scancode for the given key we want to map
	Uint8		pos;		// BCD packed, high nibble / low nibble for col/row to map to.  0xFF means end of table!, high bit set on low nibble: press shift as well!
};

// Keyboard position of "shift" which is "virtually pressed" ie for cursor up/left
#define SHIFTED_CURSOR_SHIFT_POS	0x64

/* Notes:
	* This is _POSITIONAL_ mapping (not symbolic), assuming US keyboard layout for the host machine (ie: the machine you run this emulator)
	* Only 8*8 matrix is emulated currently, on C65 there is an "extra" line it seems
	* I was lazy to map some keys, see in the comments :)
*/
static const struct KeyMapping key_map[] = {
	{ SDL_SCANCODE_BACKSPACE,	0x00 },	// "backspace" for INS/DEL
	{ SDL_SCANCODE_RETURN,		0x01 }, // RETURN
	{ SDL_SCANCODE_RIGHT,		0x02 }, { SDL_SCANCODE_LEFT,	0x02 | 8 },	// Cursor Left / Right (Horizontal) [real key on C65 with the "auto-shift trick]
	{ SDL_SCANCODE_F7,		0x03 }, { SDL_SCANCODE_F8,	0x03 | 8 },	// Real C65 does not have "F8" (but DOES have cursor up...), these are just for fun :)
	{ SDL_SCANCODE_F1,		0x04 }, { SDL_SCANCODE_F2,	0x04 | 8 },
	{ SDL_SCANCODE_F3,		0x05 }, { SDL_SCANCODE_F4,	0x05 | 8 },
	{ SDL_SCANCODE_F5,		0x06 }, { SDL_SCANCODE_F6,	0x06 | 8 },
	{ SDL_SCANCODE_DOWN,		0x07 }, { SDL_SCANCODE_UP,	0x07 | 8 },	// Cursor Down / Up (Vertical) [real key on C65 with the "auto-shift" trick]
	{ SDL_SCANCODE_3,		0x10 },
	{ SDL_SCANCODE_W,		0x11 },
	{ SDL_SCANCODE_A,		0x12 },
	{ SDL_SCANCODE_4,		0x13 },
	{ SDL_SCANCODE_Z,		0x14 },
	{ SDL_SCANCODE_S,		0x15 },
	{ SDL_SCANCODE_E,		0x16 },
	{ SDL_SCANCODE_LSHIFT,		0x17 },
	{ SDL_SCANCODE_5,		0x20 },
	{ SDL_SCANCODE_R,		0x21 },
	{ SDL_SCANCODE_D,		0x22 },
	{ SDL_SCANCODE_6,		0x23 },
	{ SDL_SCANCODE_C,		0x24 },
	{ SDL_SCANCODE_F,		0x25 },
	{ SDL_SCANCODE_T,		0x26 },
	{ SDL_SCANCODE_X,		0x27 },
	{ SDL_SCANCODE_7,		0x30 },
	{ SDL_SCANCODE_Y,		0x31 },
	{ SDL_SCANCODE_G,		0x32 },
	{ SDL_SCANCODE_8,		0x33 },
	{ SDL_SCANCODE_B,		0x34 },
	{ SDL_SCANCODE_H,		0x35 },
	{ SDL_SCANCODE_U,		0x36 },
	{ SDL_SCANCODE_V,		0x37 },
	{ SDL_SCANCODE_9,		0x40 },
	{ SDL_SCANCODE_I,		0x41 },
	{ SDL_SCANCODE_J,		0x42 },
	{ SDL_SCANCODE_0,		0x43 },
	{ SDL_SCANCODE_M,		0x44 },
	{ SDL_SCANCODE_K,		0x45 },
	{ SDL_SCANCODE_O,		0x46 },
	{ SDL_SCANCODE_N,		0x47 },
	// FIXME: map something as +	0x50
	{ SDL_SCANCODE_P,		0x51 },
	{ SDL_SCANCODE_L,		0x52 },
	{ SDL_SCANCODE_MINUS,		0x53 },
	{ SDL_SCANCODE_PERIOD,		0x54 },
	{ SDL_SCANCODE_APOSTROPHE,	0x55 },	// mapped as ":"
	// FIXME: map something as @	0x56
	{ SDL_SCANCODE_COMMA,		0x57 },
	// FIXME: map something as pound0x60
	// FIXME: map something as *	0x61
	{ SDL_SCANCODE_SEMICOLON,	0x62 },
	{ SDL_SCANCODE_HOME,		0x63 },	// CLR/HOME
	{ SDL_SCANCODE_RSHIFT,		0x64 },
	{ SDL_SCANCODE_EQUALS,		0x65 },
	// FIXME: map something as Pi?	0x66
	{ SDL_SCANCODE_SLASH,		0x67 },
	{ SDL_SCANCODE_1,		0x70 },
	// FIXME: map sg. as <--	0x71
	{ SDL_SCANCODE_LCTRL,		0x72 },
	{ SDL_SCANCODE_2,		0x73 },
	{ SDL_SCANCODE_SPACE,		0x74 },
	{ SDL_SCANCODE_LALT,		0x75 },	// Commodore key, PC kbd sux, does not have C= key ... Mapping left ALT as the C= key
	{ SDL_SCANCODE_Q,		0x76 },
	{ SDL_SCANCODE_END,		0x77 },	// RUN STOP key, we map 'END' as this key
	// **** this must be the last line: end of mapping table ****
	{ 0, 0xFF }
};


#define KBD_PRESS_KEY(a)        kbd_matrix[(a) >> 4] &= 255 - (1 << ((a) & 0x7))
#define KBD_RELEASE_KEY(a)      kbd_matrix[(a) >> 4] |= 1 << ((a) & 0x7)
#define KBD_SET_KEY(a,state) do {	\
	if (state)			\
		KBD_PRESS_KEY(a);	\
	else				\
		KBD_RELEASE_KEY(a);	\
} while (0)


int hid_key_event ( SDL_Scancode key, int pressed )
{
	const struct KeyMapping *map = key_map;
	while (map->pos != 0xFF) {
		if (map->scan == key) {
			if (map->pos & 8)			// shifted key emu?
				KBD_SET_KEY(SHIFTED_CURSOR_SHIFT_POS, pressed);	// maintain the shift key
			KBD_SET_KEY(map->pos, pressed);
			return 0;
		}
		map++;
	}
	return 1;
}
