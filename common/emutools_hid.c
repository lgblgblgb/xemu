/* Xemu - Somewhat lame emulation (running on Linux/Unix/Windows/OSX, utilizing
   SDL2) of some 8 bit machines, including the Commodore LCD and Commodore 65
   and some Mega-65 features as well.
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

#include "emutools.h"
#include "emutools_hid.h"


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


Uint8 kbd_matrix[16];		// keyboard matrix state, 8 * 8 bits
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



/* Notes:
	* This is _POSITIONAL_ mapping (not symbolic), assuming US keyboard layout for the host machine (ie: the machine you run this emulator)
	* Only 8*8 matrix is emulated currently, on C65 there is an "extra" line it seems
	* I was lazy to map some keys, see in the comments :)
*/
static const struct KeyMapping *key_map = NULL;
static Uint8 virtual_shift_pos = 0;

#define KBD_PRESS_KEY(a)        kbd_matrix[(a) >> 4] &= ~(1 << ((a) & 0x7))
#define KBD_RELEASE_KEY(a)      kbd_matrix[(a) >> 4] |=   1 << ((a) & 0x7)
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
				KBD_SET_KEY(virtual_shift_pos, pressed);	// maintain the shift key
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
		DEBUG("HID: %d event(s) ignored." NL, burn);
	}
}


void hid_init ( const struct KeyMapping *key_map_in, Uint8 virtual_shift_pos_in )
{
	int a;
	key_map = key_map_in;
	virtual_shift_pos = virtual_shift_pos_in;
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
	DEBUG("HID: mouse motion %d:%d, collected data is now %d:%d" NL, xrel, yrel, mouse_delta_x, mouse_delta_y);
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
			DEBUG("HID: joystick device #%d \"%s\" has been added." NL, which, SDL_JoystickName(joysticks[which]));
		else
			DEBUG("HID: joystick device #%d problem, cannot be opened on 'add' event: %s." NL, which, SDL_GetError());
	} else {
		if (joysticks[which]) {
			SDL_JoystickClose(joysticks[which]);
			joysticks[which] = NULL;
			DEBUG("HID: joystick device #%d has been removed." NL, which);
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
