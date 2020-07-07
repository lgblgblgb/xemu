/* Xemu - Somewhat lame emulation (running on Linux/Unix/Windows/OSX, utilizing
   SDL2) of some 8 bit machines, including the Commodore LCD and Commodore 65
   and the MEGA65 as well.
   Copyright (C)2016-2020 LGB (Gábor Lénárt) <lgblgblgb@gmail.com>

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

#ifndef XEMU_COMMON_EMUTOOLS_HID_H_INCLUDED
#define XEMU_COMMON_EMUTOOLS_HID_H_INCLUDED

/* Note: HID stands for "Human Input Devices" or something like that :)
   That is: keyboard, joystick, mouse. */

struct KeyMappingDefault {
	SDL_Scancode	scan;	// SDL scancode for the given key we want to map
	int		pos;	// BCD packed, high nibble / low nibble (x <= 7) for col/row to map to, -1 end of table, > 0xFF is special key event
	const char*	name;	// name of the emulated key. It's just for "comment" or layout config load/save whatever, the emulation itself does not use it
};
struct KeyMappingUsed {
	SDL_Scancode	scan;
	int		pos;
	int		set;
};

#ifdef HID_KBD_MAP_CFG_SUPPORT
#define KEYMAP_DEFAULT_FILENAME	"@keymap-default.cfg"
#define KEYMAP_USER_FILENAME	"@keymap.cfg"
extern void hid_keymap_from_config_file ( const char *fn );
#endif

extern Uint8 kbd_matrix[16];

#define KBD_CLEAR_MATRIX()      memset(kbd_matrix, 0xFF, sizeof kbd_matrix)
#define KBD_PRESS_KEY(a)        kbd_matrix[(a) >> 4] &= ~(1 << ((a) & 0x7))
#define KBD_RELEASE_KEY(a)      kbd_matrix[(a) >> 4] |=   1 << ((a) & 0x7)
#define KBD_SET_KEY(a,state) do {	\
	if (state)			\
		KBD_PRESS_KEY(a);	\
	else				\
		KBD_RELEASE_KEY(a);	\
} while (0)

extern int  hid_mouse_enabled;

// This one must be defined by the emulator!
extern int  emu_callback_key		( int pos, SDL_Scancode key, int pressed, int handled ) ;
// Optinally can be defined by the emulator:
extern void emu_dropfile_callback	( const char *fn );
extern void emu_quit_callback		( void );
extern void emu_callback_key_raw_sdl	( SDL_KeyboardEvent *ev );
extern void emu_callback_key_texteditng_sdl ( SDL_TextEditingEvent *ev );
extern void emu_callback_key_textinput_sdl  ( SDL_TextInputEvent   *ev );

// Provided HID functions:
extern void hid_set_autoreleased_key    ( int key );
extern int  hid_key_event		( SDL_Scancode key, int pressed ) ;
extern void hid_reset_events		( int burn ) ;
extern void hid_init			( const struct KeyMappingDefault *key_map_in, Uint8 virtual_shift_pos_in, int joy_enable ) ;
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
extern int  hid_handle_one_sdl_event    ( SDL_Event *event ) ;
extern void hid_handle_all_sdl_events   ( void ) ;

#define XEMU_EVENT_EXIT			0x100
#define XEMU_EVENT_FAKE_JOY_UP		0x101
#define XEMU_EVENT_FAKE_JOY_DOWN	0x102
#define XEMU_EVENT_FAKE_JOY_LEFT	0x103
#define XEMU_EVENT_FAKE_JOY_RIGHT	0x104
#define XEMU_EVENT_FAKE_JOY_FIRE	0x105
#define XEMU_EVENT_TOGGLE_FULLSCREEN	0x106

#define STD_XEMU_SPECIAL_KEYS	\
	{ SDL_SCANCODE_F9,	XEMU_EVENT_EXIT,		"XEMU-EXIT" }, \
	{ SDL_SCANCODE_F11,	XEMU_EVENT_TOGGLE_FULLSCREEN,	"XEMU-FULLSCREEN" }, \
	{ SDL_SCANCODE_KP_5,	XEMU_EVENT_FAKE_JOY_FIRE,	"XEMU-JOY-FIRE" },	/* for joy FIRE  we map PC num keypad 5 */ \
	{ SDL_SCANCODE_KP_0,	XEMU_EVENT_FAKE_JOY_FIRE,	"XEMU-JOY-FIRE" },	/* PC num keypad 0 is also the FIRE ... */ \
	{ SDL_SCANCODE_RCTRL,	XEMU_EVENT_FAKE_JOY_FIRE,	"XEMU-JOY-FIRE" },	/* and RIGHT controll is also the FIRE ... to make Sven happy :) */ \
	{ SDL_SCANCODE_KP_8,	XEMU_EVENT_FAKE_JOY_UP,		"XEMU-JOY-UP" },	/* for joy UP    we map PC num keypad 8 */ \
	{ SDL_SCANCODE_KP_2,	XEMU_EVENT_FAKE_JOY_DOWN,	"XEMU-JOY-DOWN" },	/* for joy DOWN  we map PC num keypad 2 */ \
	{ SDL_SCANCODE_KP_4,	XEMU_EVENT_FAKE_JOY_LEFT,	"XEMU-JOY-LEFT" },	/* for joy LEFT  we map PC num keypad 4 */ \
	{ SDL_SCANCODE_KP_6,	XEMU_EVENT_FAKE_JOY_RIGHT,	"XEMU-JOY-RIGHT" }	/* for joy RIGHT we map PC num keypad 6 */

#endif
