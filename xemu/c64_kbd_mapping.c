/* Xemu - Somewhat lame emulation (running on Linux/Unix/Windows/OSX, utilizing
   SDL2) of some 8 bit machines, including the Commodore LCD and Commodore 65
   and MEGA65 as well.
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

#include "xemu/emutools.h"
#include "xemu/emutools_hid.h"
#include "xemu/c64_kbd_mapping.h"

/* Definitions for "C64-like" systems (ie: C64, C65, M65).
	* I was lazy to map some keys, see in the comments :)
	* The mapping should be revised at some point, this was only a quick setup without too much work since then ...
	* ... but anyway it's only a default map anyway, since HID can load custom keymaps now!
*/

// Comments on this table: for uncommented lines, it's a kinda trivial mapping, like '3' on PC is for sure, '3' on C64 ...
// Since, it's positional mapping, there is no need to talk about key '3' shifted, it simply means the result of shifted '3' on C64, that's all.
// For 'PC key', we assume the scancode, thus about the US PC keyboard layout, in general! [but this can be vary if OS remaps scancodes, who knows how SDL handles this ...]
// NOTE: The term "C65", "C65 keyboard" means C65 or M65 emulators. You can ask wow, then what left in Xemu not C65 kbd? Well, there is C64/GEOS emulataion "leftover", which is only C64 :)
// "SDL_SCANCODE_UNKNOWN" does not map a key, but user can still give a custom keymap config file to map those.
const struct KeyMappingDefault c64_key_map[] = {
	// SDL SCANCODE                 POS.   EMU-KEY-NAME
	// ---------------------------- ----   ------------
	{ SDL_SCANCODE_BACKSPACE,	0x00, "DEL" },	// PC backspace -> C64 "INST/DEL"
	{ SDL_SCANCODE_RETURN,		0x01, "RETURN" }, // PC enter -> C64 return
	// PC's cursor right is mapped as C64's cursor right. For PC's cursor LEFT, it's mapped as C64's cursor right AND shift pressed.
	{ SDL_SCANCODE_RIGHT,		0x02, "RIGHT" }, { SDL_SCANCODE_LEFT,	0x02 | 8, "LEFT*" },	// Cursor Left / Right (Horizontal) [real key on C65 with the "auto-shift" trick]
	// Function keys. For some convience F1,F3,F5,F7 from PC is mapped to the same C64 key, and PC's F2,F4,F6,F8 is mapped as F1,F3,F5,F7 but with shift pressed as well
	{ SDL_SCANCODE_F7,		0x03, "F7" }, { SDL_SCANCODE_F8,	0x03 | 8, "F8*" },	// Real C65 does not have "F8" (but DOES have cursor up...), these are just for fun :)
	{ SDL_SCANCODE_F1,		0x04, "F1" }, { SDL_SCANCODE_F2,	0x04 | 8, "F2*" },
	{ SDL_SCANCODE_F3,		0x05, "F3" }, { SDL_SCANCODE_F4,	0x05 | 8, "F4*" },
	{ SDL_SCANCODE_F5,		0x06, "F5" }, { SDL_SCANCODE_F6,	0x06 | 8, "F6*" },
	// PC's cursor down is mapped as C64's cursor down. For PC's cursor UP, it's mapped as C64's cursor down AND shift pressed.
	{ SDL_SCANCODE_DOWN,		0x07, "DOWN" }, { SDL_SCANCODE_UP,	0x07 | 8, "UP*" },	// Cursor Down / Up (Vertical) [real key on C65 with the "auto-shift" trick]
	{ SDL_SCANCODE_3,		0x10, "3" },
	{ SDL_SCANCODE_W,		0x11, "W" },
	{ SDL_SCANCODE_A,		0x12, "A" },
	{ SDL_SCANCODE_4,		0x13, "4" },
	{ SDL_SCANCODE_Z,		0x14, "Z" },
	{ SDL_SCANCODE_S,		0x15, "S" },
	{ SDL_SCANCODE_E,		0x16, "E" },
	{ SDL_SCANCODE_LSHIFT,		0x17, "LSHIFT"  },	// PC Left shift -> C64 left shift
	{ SDL_SCANCODE_5,		0x20, "5" },
	{ SDL_SCANCODE_R,		0x21, "R" },
	{ SDL_SCANCODE_D,		0x22, "D" },
	{ SDL_SCANCODE_6,		0x23, "6" },
	{ SDL_SCANCODE_C,		0x24, "C" },
	{ SDL_SCANCODE_F,		0x25, "F" },
	{ SDL_SCANCODE_T,		0x26, "T" },
	{ SDL_SCANCODE_X,		0x27, "X" },
	{ SDL_SCANCODE_7,		0x30, "7" },
	{ SDL_SCANCODE_Y,		0x31, "Y" },
	{ SDL_SCANCODE_G,		0x32, "G" },
	{ SDL_SCANCODE_8,		0x33, "8" },
	{ SDL_SCANCODE_B,		0x34, "B" },
	{ SDL_SCANCODE_H,		0x35, "H" },
	{ SDL_SCANCODE_U,		0x36, "U" },
	{ SDL_SCANCODE_V,		0x37, "V" },
	{ SDL_SCANCODE_9,		0x40, "9" },
	{ SDL_SCANCODE_I,		0x41, "I" },
	{ SDL_SCANCODE_J,		0x42, "J" },
	{ SDL_SCANCODE_0,		0x43, "0" },
	{ SDL_SCANCODE_M,		0x44, "M" },
	{ SDL_SCANCODE_K,		0x45, "K" },
	{ SDL_SCANCODE_O,		0x46, "O" },
	{ SDL_SCANCODE_N,		0x47, "N" },
	{ SDL_SCANCODE_INSERT,		0x50, "PLUS" },		// PC "Insert" key -> C64 '+' (plus) key [FIXME: map something more sane as '+' ?]
	{ SDL_SCANCODE_P,		0x51, "P" },
	{ SDL_SCANCODE_L,		0x52, "L" },
	{ SDL_SCANCODE_MINUS,		0x53, "MINUS" },
	{ SDL_SCANCODE_PERIOD,		0x54, "PERIOD" },
	{ SDL_SCANCODE_APOSTROPHE,	0x55, "COLON" },	// mapped as ":"
	{ SDL_SCANCODE_LEFTBRACKET,	0x56, "AT" },		// FIXME: map something sane as @ ?
	{ SDL_SCANCODE_COMMA,		0x57, "COMMA" },
	{ SDL_SCANCODE_DELETE,		0x60, "POUND" },	// FIXME: map something sane as £ ?
	{ SDL_SCANCODE_RIGHTBRACKET,	0x61, "ASTERISK" },	// FIXME: map something sane as * ?
	{ SDL_SCANCODE_SEMICOLON,	0x62, "SEMICOLON" },
	{ SDL_SCANCODE_HOME,		0x63, "CLR" },		// PC "home" -> C64 "CLR/HOME"
	{ SDL_SCANCODE_RSHIFT,		0x64, "RSHIFT" }, 	// PC right shift -> C64 right shift
	{ SDL_SCANCODE_EQUALS,		0x65, "EQUALS" },
	{ SDL_SCANCODE_BACKSLASH,	0x66, "UARROW" },	// PC \ (backslash) -> C64 "up-arrow" symbol [which is the PI, when shifted ...]
	{ SDL_SCANCODE_SLASH,		0x67, "SLASH" },
	{ SDL_SCANCODE_1,		0x70, "1" },
	{ SDL_SCANCODE_GRAVE,		0x71, "LARROW" },	// FIXME: map something sane as <-- ?
	{ SDL_SCANCODE_LCTRL,		0x72, "CTRL" },
	{ SDL_SCANCODE_2,		0x73, "2" },
	{ SDL_SCANCODE_SPACE,		0x74, "SPACE" },
	{ SDL_SCANCODE_LALT,		0x75, "COMMODORE" },	// Commodore key, PC kbd sux, does not have C= key ... Mapping left ALT as the C= key
#ifndef C65_KEYBOARD
	{ SDL_SCANCODE_RALT,		0x75, "COMMODORE" },	// RALT is used as the real ALT on C65 kbd ... if no C65 keyboard is used, still, map RALT as Commodore key as well.
#endif
	{ SDL_SCANCODE_Q,		0x76, "Q" },
	{ SDL_SCANCODE_END,		0x77, "RUNSTOP" },	// C64 "RUN STOP" key, we map PC 'END' as this key
#ifndef C65_KEYBOARD
	{ SDL_SCANCODE_ESCAPE,		0x77, "RUNSTOP" },	// C64 "RUN STOP" can be also accessed as PC 'ESC' ... On C65 kbd, this is not done, and leaving ESC as ESC, you can still use END key as RUN/STOP there
#endif
	// Not a real kbd-matrix key
#ifndef C65_KEYBOARD
	{ SDL_SCANCODE_TAB,		RESTORE_KEY_POS, "RESTORE" },	// PC TAB -> C64 "restore", but NOT on C65 as it has a TAB key, which needs it!
#endif
	{ SDL_SCANCODE_PAGEDOWN,	RESTORE_KEY_POS, "RESTORE" },	// PC PageDown as Restore
#ifdef C65_KEYBOARD
	// Not a real kbd-matrix key
	// currently not used (not just here as UNKNOWN but not emulated either in C65/M65 emu part), since it's not clear for me, if this key is hardware/mechanical-latching, or not (only done in software) ...
	{ SDL_SCANCODE_UNKNOWN,		CAPSLOCK_KEY_POS, "CAPSLOCK" },
#endif
#ifdef C65_KEYBOARD
	// C65 (and thus M65 too) keyboard is basically the same as C64, however there are extra keys which are handled differently than the "C64-compatible" keys.
	// Thus, if requested with macro C65_KEYBOARD defined, we have some extra "virtual" matrix positions for those keys here.
	{ SDL_SCANCODE_UNKNOWN,		SCRL_KEY_POS, "NOSCROLL" },		// NO SCROLL: FIXME: where should we map this key to?
	{ SDL_SCANCODE_TAB,		TAB_KEY_POS, "TAB" },			// TAB
	{ SDL_SCANCODE_RALT,		ALT_KEY_POS, "ALT" },			// ALT on C65: right alt (AltGr) on PC [left ALT on PC is used as the commodore key]
	{ SDL_SCANCODE_PAGEUP,		C65_KEYBOARD_EXTRA_POS + 3, "HELP" },	// HELP: FIXME: where should we map this key to?
	{ SDL_SCANCODE_UNKNOWN,		C65_KEYBOARD_EXTRA_POS + 4, "F9" },	// F9/F10: FIXME: where should we map this key to?
	{ SDL_SCANCODE_UNKNOWN,		C65_KEYBOARD_EXTRA_POS + 5, "F11" },	// F11/F12: FIXME: where should we map this key to?
	{ SDL_SCANCODE_UNKNOWN,		C65_KEYBOARD_EXTRA_POS + 6, "F13" },	// F13/F14: FIXME: where should we map this key to?
	{ SDL_SCANCODE_ESCAPE,		C65_KEYBOARD_EXTRA_POS + 7, "ESC" },	// ESC
#endif
	// **** Emulates joystick with keypad
	STD_XEMU_SPECIAL_KEYS,
	// **** this must be the last line: end of mapping table ****
	{ 0, -1 }
};

int joystick_emu = 1;


void c64_toggle_joy_emu ( void )
{
	if (joystick_emu == 1)
		joystick_emu = 2;
	else if (joystick_emu == 2)
		joystick_emu = 1;
	if (joystick_emu)
		DEBUGPRINT("Joystick emulation for Joy#%d" NL, joystick_emu);
}


Uint8 c64_get_joy_state ( void )
{
	return
			0xE0 |
			(hid_read_joystick_up    ( 0,  1 ) & (is_mouse_grab() ? hid_read_mouse_button_right(0,  1) :  1)) |
			hid_read_joystick_down   ( 0,  2 ) |
			hid_read_joystick_left   ( 0,  4 ) |
			hid_read_joystick_right  ( 0,  8 ) |
			(hid_read_joystick_button( 0, 16 ) & (is_mouse_grab() ? hid_read_mouse_button_left (0, 16) : 16))
	;
}


#ifdef FAKE_TYPING_SUPPORT

#ifdef C65_FAKE_TYPING_LOAD_SEQS
const Uint8 fake_typing_for_go64[] = {
	0x32,0x46,0x23,0x13,0x01,0x31,0x01,  0xFF		// GO64 <RETURN> Y <RETURN> <END_MARKER>
};
const Uint8 fake_typing_for_load64[] = {
	0x32,0x46,0x23,0x13,0x01,0x31,0x01,			// GO64 <RETURN> Y <RETURN>
#ifdef MEGA65
	0x51,0xFE,0x46,0xFE,0x43,0x57,0x23,0x20,0x01,0x01,	// P <TOGGLE_SHIFT> O <TOGGGLE_SHIFT> 0,65 <RETURN>
#endif
	0x52,0xFE,0x46,0x73,0xFE,0x61,0xFE,0x73,0xFE,0x01,	// L <TOGGLE_SHIFT> O 2 <TOGGLE_SHIFT> * <TOGGLE_SHIFT> 2 <RETURN>
#ifdef MEGA65
	0x51,0xFE,0x46,0xFE,0x43,0x57,0x23,0x13,0x01,0x01,	// P <TOGGLE_SHIFT> O <TOGGGLE_SHIFT> 0,64 <RETURN>
#endif
	0x21,0x36,0x47,0x01,					// RUN <RETURN>
	0xFF							// <END_MARKER>
};
const Uint8 fake_typing_for_load65[] = {
	//0x21,0x36,0x47,0xFE,0x73,0xFE,0x61,0xFE,0x73,0xFE,0x01,	// RUN"*"
	//0xFF
#ifdef MEGA65
	0x51,0x46,0x45,0x16,0x43,0x57,0x23,0x20,0x01,0x01,	// POKE 0,65 <RETURN>
#endif
	0x52,0xFE,0x46,0x73,0xFE,0x61,0xFE,0x73,0xFE,0x01,	// L <TOGGLE_SHIFT> O 2 <TOGGLE_SHIFT> * <TOGGLE_SHIFT> 2 <RETURN>
#ifdef MEGA65
	0x51,0x46,0x45,0x16,0x43,0x57,0x23,0x13,0x01,0x01,	// POKE 0,64 <RETURN>
#endif
	0x21,0x36,0x47,0x01,					// RUN <RETURN>
	0xFF							// <END_MARKER>
};
#endif

static struct {
	int attention;
	const Uint8 *typing;
	int virtual_shift;
} kbd_inject;
int c64_fake_typing_enabled = 0;


void c64_register_fake_typing ( const Uint8 *keys )
{
	c64_fake_typing_enabled = 1;
	kbd_inject.attention = 0;
	kbd_inject.typing = keys;
	kbd_inject.virtual_shift = 0;
}


void c64_stop_fake_typing ( void )
{
	if (XEMU_UNLIKELY(c64_fake_typing_enabled)) {
		c64_fake_typing_enabled = 0;
		KBD_CLEAR_MATRIX();
	}
}


void c64_handle_fake_typing_internals ( Uint8 keysel )
{
	// It seems, both of C64 and C65 ROM uses this for keyboard scanning.
	// Now, we have to check if we need to "inject" keypresses (eg for auto-load) but also,
	// that if the machine itself is in the state of watching the keyboard at this point or eg not yet.
	// For the second task, we want to use a routine in c64_keyboard_mapping.c (in xemu common code)
	// so it's shared with emulators based on the C64-scheme on keyboard handling, ie C65 emulator in Xemu
	// (or later even eg C128, or C64). Probably that can be generalized even more later, ie with different
	// matrix based scenarios as well.
	//printf("SCAN: on B, sel=%02X effect=%02X" NL, (cia1.PRA | (~cia1.DDRA)) & 0xFF, (cia1.PRB | (~cia1.DDRB)) & 0xFF);
	kbd_inject.attention++;
	if (kbd_inject.attention > 25) {
		kbd_inject.attention = 0;
		KBD_CLEAR_MATRIX();
		if (kbd_inject.virtual_shift)
			KBD_PRESS_KEY(VIRTUAL_SHIFT_POS);
		if (*kbd_inject.typing != 0xFF) {
			if (*kbd_inject.typing == 0xFE) {
				kbd_inject.virtual_shift = !kbd_inject.virtual_shift;
				KBD_SET_KEY(VIRTUAL_SHIFT_POS, kbd_inject.virtual_shift);
			} else
				KBD_PRESS_KEY(*kbd_inject.typing);
			kbd_inject.typing++;
		} else {
			c64_fake_typing_enabled = 0;
			KBD_CLEAR_MATRIX();
		}
	}
#if 0
	if (kbd_inject.next_ok) {
		kbd_inject.next_ok = 0;
	}
	Uint8 mask = (cia1.PRA | (~cia1.DDRA)) & 0xFF;
	switch (mask) {
		case 0xFF:
			break;
		case 0x00:
			kbd_inject.attention++;
			break;
		case 0xFE: case 0xFD: case 0xFB: case 0xF7:
		case 0xEF: case 0xDF: case 0xBF: case 0x7F:
			kbd_inject.checked_lines &= mask;
			kbd_inject.attention++;
			break;
		default:
			printf("UNKNOWN SCAN SELECTOR: %02X" NL, mask);
			break;
	}
	if (!kbd_inject.checked_lines) {
		kbd_inject.next_ok = 1;
		kbd_inject.checked_lines = 0xFF;
	}
	SDL_Event sdlevent = {};
	sdlevent.type = SDL_KEYDOWN;
	sdlevent.key.repeat = 0;
	sdlevent.key.windowID = sdl_winid;
	sdlevent.key.state = SDL_PRESSED;
	//if (event->key.repeat == 0
	//#ifdef CONFIG_KBD_SELECT_FOCUS
	//&& (event->key.windowID == sdl_winid || event->key.windowID == 0)
	//hid_key_event(event->key.keysym.scancode, event->key.state == SDL_PRESSED);
	//sdlevent.key.keysym.sym = SDLK_1;
	// event->key.keysym.scancode
	sdlevent.key.keysym.scancode = SDL_SCANCODE_A;
	//SDL_PushEvent(&sdlevent);
	//printf("ACTIVE SCAN-LIKE STUFF" NL);
	//do_inject = 0;
#endif
}

#endif
