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

//#include "commodore_65.h"
#include "c65hid.h"
#include "emutools.h"


/* Note: HID stands for "Human Input Devices" or something like that :)
   That is: keyboard, joystick, mouse.

   TODO: unify this and move to emutools.c, and allow all emulators to share
   on the common code (with keyboard map set by the emulator though!)

   TODO: also include SDL event loop handling, hot-key "sensing" etc,
   so if no special requirement from the emulator, then it does not even
   deal with SDL events at all!

   TODO: also the keyboard matrix default state should be configurable
   (ie: Commodore LCD uses '0' as unpressed ...)

   TODO: there is no configuration for multiple joysticks, axes, whatever :(
*/


Uint8 kbd_matrix[8];		// keyboard matrix state, 8 * 8 bits
static int mouse_delta_x;
static int mouse_delta_y;
static unsigned int hid_state;

#define MAX_JOYSTICKS			16

static SDL_Joystick *joysticks[MAX_JOYSTICKS];

#define JOYSTATE_UP			 1
#define JOYSTATE_DOWN			 2
#define JOYSTATE_LEFT			 4
#define JOYSTATE_RIGHT			 8
#define JOYSTATE_BUTTON			16
#define MOUSESTATE_BUTTON_LEFT		32
#define MOUSESTATE_BUTTON_RIGHT		64



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


// Reset all HID events.
// Ie: it's usefull for initialization, and in the case when the emulator pops a window,
// in this case SDL may detect the event used to ack the window causing problems. So those
// functions should call this as well to reset events. It also uses some "burning SDL
// events" scheme to skip the possible received stuffs.
void hid_reset_events ( int burn )
{
	memset(kbd_matrix, 0xFF, sizeof kbd_matrix);	// set keyboard matrix to default state (unpressed for all positions)
	mouse_delta_x = 0;
	mouse_delta_y = 0;
	hid_state = 0;
	if (burn) {
		SDL_Event e;
		burn = 0;
		while (SDL_PollEvent(&e) != 0)
			burn++;
		printf("HID: %d event(s) ignored." NL, burn);
	}
}


void hid_init ( void )
{
	int a;
	SDL_GameControllerEventState(SDL_DISABLE);
	SDL_JoystickEventState(SDL_ENABLE);
	hid_reset_events(0);
	for (a = 0; a < MAX_JOYSTICKS; a++)
		joysticks[a] = NULL;
}



void hid_mouse_motion_event ( int xrel, int yrel )
{
	mouse_delta_x += xrel;
	mouse_delta_y += yrel;
	printf("HID: mouse motion %d:%d, collected data is now %d:%d" NL, xrel, yrel, mouse_delta_x, mouse_delta_y);
}


void hid_mouse_button_event ( int button, int pressed )
{
	int mask;
	if (button == SDL_BUTTON_LEFT)
		mask = MOUSESTATE_BUTTON_LEFT;
	else if (button == SDL_BUTTON_RIGHT)
		mask = MOUSESTATE_BUTTON_RIGHT;
	else
		return;
	if (pressed)
		hid_state |= mask;
	else
		hid_state &= ~mask;
}


void hid_joystick_device_event ( int which , int is_attach )
{
	if (which >= MAX_JOYSTICKS)
		return;
	if (is_attach) {
		if (joysticks[which])
			hid_joystick_device_event(which, 0);
		joysticks[which] = SDL_JoystickOpen(which);
		if (joysticks[which])
			printf("HID: joystick device #%d \"%s\" has been added." NL, which, SDL_JoystickName(joysticks[which]));
		else
			printf("HID: joystick device #%d problem, cannot be opened on 'add' event: %s." NL, which, SDL_GetError());
	} else {
		if (joysticks[which]) {
			SDL_JoystickClose(joysticks[which]);
			joysticks[which] = NULL;
			printf("HID: joystick device #%d has been removed." NL, which);
			// This is needed to avoid "stuck" joystick state if removed in that state ...
			hid_state &= ~(JOYSTATE_UP | JOYSTATE_DOWN | JOYSTATE_LEFT | JOYSTATE_RIGHT | JOYSTATE_BUTTON);
		}
	}
}


void hid_joystick_motion_event ( int is_vertical, int value )
{
	if (is_vertical) {
		hid_state &= ~(JOYSTATE_UP | JOYSTATE_DOWN);
		if (value < -10000)
			hid_state |= JOYSTATE_UP;
		else if (value > 10000)
			hid_state |= JOYSTATE_DOWN;
	} else {
		hid_state &= ~(JOYSTATE_LEFT | JOYSTATE_RIGHT);
		if (value < -10000)
			hid_state |= JOYSTATE_LEFT;
		else if (value > 10000)
			hid_state |= JOYSTATE_RIGHT;
	}
}


void hid_joystick_button_event ( int pressed )
{
	if (pressed)
		hid_state |=  JOYSTATE_BUTTON;
	else
		hid_state &= ~JOYSTATE_BUTTON;
}


void hid_joystick_hat_event ( int value )
{
	hid_state &= ~(JOYSTATE_UP | JOYSTATE_DOWN | JOYSTATE_LEFT | JOYSTATE_RIGHT);
	if (value & SDL_HAT_UP)
		hid_state |= JOYSTATE_UP;
	if (value & SDL_HAT_DOWN)
		hid_state |= JOYSTATE_DOWN;
	if (value & SDL_HAT_LEFT)
		hid_state |= JOYSTATE_LEFT;
	if (value & SDL_HAT_RIGHT)
		hid_state |= JOYSTATE_RIGHT;
}


int hid_read_joystick_up ( int on, int off )
{
	return (hid_state & JOYSTATE_UP) ? on : off;
}


int hid_read_joystick_down ( int on, int off )
{
	return (hid_state & JOYSTATE_DOWN) ? on : off;
}


int hid_read_joystick_left ( int on, int off )
{
	return (hid_state & JOYSTATE_LEFT) ? on : off;
}


int hid_read_joystick_right ( int on, int off )
{
	return (hid_state & JOYSTATE_RIGHT) ? on : off;
}


int hid_read_joystick_button ( int on, int off )
{
	return (hid_state & JOYSTATE_BUTTON) ? on : off;
}


int hid_read_mouse_rel_x ( int min, int max )
{
	int result = mouse_delta_x;
	mouse_delta_x = 0;
	if (result < min)
		result = min;
	else if (result > max)
		result = max;
	return result;
}


int hid_read_mouse_rel_y ( int min, int max )
{
	int result = mouse_delta_y;
	mouse_delta_y = 0;
	if (result < min)
		result = min;
	else if (result > max)
		result = max;
	return result;
}


int hid_read_mouse_button_left ( int on, int off )
{
	return (hid_state & MOUSESTATE_BUTTON_LEFT) ? on : off;
}


int hid_read_mouse_button_right ( int on, int off )
{
	return (hid_state & MOUSESTATE_BUTTON_RIGHT) ? on : off;
}
