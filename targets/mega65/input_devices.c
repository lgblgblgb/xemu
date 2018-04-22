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
#include "mega65.h"
#include "io_mapper.h"
#include "xemu/cpu65.h"


#define DEBUGKBD(...)	DEBUG(__VA_ARGS__)
//#define DEBUGKBD(...)	DEBUGPRINT(__VA_ARGS__)


/* Note: M65 has a "hardware accelerated" keyboard scanner, it can provide you the
   last pressed character as ASCII (!) without the need to actually scan/etc the
   keyboard matrix */

/* These values are from matrix_to_ascii.vhdl in mega65-core project */

#define MAT2ASC_TAB_SIZE	72

static const Uint8 matrix_normal_to_ascii[MAT2ASC_TAB_SIZE] ={0x14,0x0D,0x1d,0xf7,0xf1,0xf3,0xf5,0x11,0x33,0x77,0x61,0x34,0x7a,0x73,0x65,0x00,0x35,0x72,0x64,0x36,0x63,0x66,0x74,0x78,0x37,0x79,0x67,0x38,0x62,0x68,0x75,0x76,0x39,0x69,0x6a,0x30,0x6d,0x6b,0x6f,0x6e,0x2b,0x70,0x6c,0x2d,0x2e,0x3a,0x40,0x2c,0x00,0x00,0x3b,0x13,0x00,0x3d,0x00,0x2f,0x31,0x5f,0x00,0x32,0x20,0x00,0x71,0x03,0x00,0x09,0x00,0x00,0xf9,0xfb,0xfd,0x1b};
static const Uint8 matrix_shift_to_ascii[MAT2ASC_TAB_SIZE]  ={0x94,0x0D,0x9d,0xf8,0xf2,0xf4,0xf6,0x91,0x23,0x57,0x41,0x24,0x5a,0x53,0x45,0x00,0x25,0x52,0x44,0x26,0x43,0x46,0x54,0x58,0x27,0x59,0x47,0x7b,0x42,0x48,0x55,0x56,0x29,0x49,0x4a,0x7b,0x4d,0x4b,0x4f,0x4e,0x00,0x50,0x4c,0x00,0x3e,0x5b,0x00,0x3c,0x00,0x2a,0x5d,0x93,0x00,0x7d,0x00,0x3f,0x21,0x7e,0x00,0x22,0x20,0x00,0x51,0xa3,0x00,0x0f,0x00,0x00,0xfa,0xfc,0xfe,0x1b};
static const Uint8 matrix_control_to_ascii[MAT2ASC_TAB_SIZE]={0x94,0x0D,0x9d,0xf8,0xf2,0xf4,0xf6,0x91,0x9f,0x17,0x01,0x9c,0x1a,0x13,0x05,0x00,0x1e,0x12,0x04,0x1f,0x03,0x06,0x14,0x18,0x9e,0x19,0x07,0x81,0x02,0x08,0x15,0x16,0x95,0x09,0x0a,0x00,0x0d,0x0b,0x0f,0x0e,0x2b,0x10,0x0c,0x2d,0x2e,0x3a,0x40,0x2c,0x00,0x00,0x3b,0x93,0x00,0x3d,0x00,0x2f,0x05,0x5f,0x00,0x1c,0x20,0x00,0x11,0xa3,0x00,0x0f,0x00,0x00,0xfa,0xfc,0xfe,0x1b};
static const Uint8 matrix_cbm_to_ascii[MAT2ASC_TAB_SIZE]    ={0x94,0x0D,0xED,0xf8,0xf2,0xf4,0xf6,0xEE,0x97,0xd7,0xc1,0x98,0xda,0xd3,0xc5,0x00,0x9a,0xd2,0xc4,0x9b,0xc3,0xc6,0xd4,0xd8,0x9c,0xd9,0xc7,0x00,0xc2,0xc8,0xd5,0xd6,0x00,0xc9,0xca,0x81,0xcd,0xcb,0xcf,0xce,0x2b,0xd0,0xcc,0x2d,0x2e,0x3a,0x40,0x2c,0x00,0x00,0x3b,0x93,0x00,0x3d,0x00,0x2f,0x95,0xef,0x00,0x96,0x20,0x00,0xd1,0xa3,0x00,0xef,0x00,0x00,0xfa,0xfc,0xfe,0x1b};

#define MODKEY_LSHIFT	0x01
#define MODKEY_RSHIFT	0x02
#define MODKEY_CTRL	0x04
#define	MODKEY_CBM	0x08
#define MODKEY_ALT	0x10
#define MODKEY_SCRL	0x20

// Decoding table based on modoifer keys (the index is the MODKEY stuffs, low 4 bits)
static const Uint8 *matrix_to_ascii_table_selector[16] = {
	matrix_normal_to_ascii,  matrix_shift_to_ascii,   matrix_shift_to_ascii, matrix_shift_to_ascii,
	matrix_control_to_ascii, matrix_shift_to_ascii,   matrix_shift_to_ascii, matrix_shift_to_ascii,
	matrix_cbm_to_ascii,     matrix_cbm_to_ascii,     matrix_cbm_to_ascii,   matrix_cbm_to_ascii,	// CBM key has priority
	matrix_cbm_to_ascii,     matrix_cbm_to_ascii,     matrix_cbm_to_ascii,   matrix_cbm_to_ascii	// CBM key has priority
};

static Uint8 last_key_as_ascii = 0;
static Uint8 key_modifiers = 0;


/* used by actual I/O function to read $D610 */
Uint8 kbd_get_last ( void )
{
	DEBUG("KBD: HWA: reading key @ PC=$%04X result = $%02X" NL, cpu65.pc, last_key_as_ascii);
	return last_key_as_ascii;
}


/* used by actual I/O function to read $D611 */
Uint8 kbd_get_modifiers ( void )
{
	DEBUG("KBD: HWA: reading key modifiers @ PC=$%04X result = $%02X" NL, cpu65.pc, key_modifiers);
	return key_modifiers;
}


/* used by actual I/O function to write $D610, the written data itself is not used, only the fact of writing */
void kbd_move_next ( void )
{
	DEBUG("KBD: HWA: moving to next key @ PC=$%04X previous value was: $%02X" NL, cpu65.pc, last_key_as_ascii);
	last_key_as_ascii = 0;
}


/* basically the opposite as kbd_get_last() but this one used internally only */
static void store_new_ascii_keypress ( Uint8 ascii )
{
	DEBUG("KBD: HWA: storing key $%02X" NL, ascii);
	last_key_as_ascii = ascii;
}



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
	// Update status of modifier keys
	key_modifiers =
		  (IS_KEY_PRESSED(LSHIFT_KEY_POS) ? MODKEY_LSHIFT : 0)
		| (IS_KEY_PRESSED(RSHIFT_KEY_POS) ? MODKEY_RSHIFT : 0)
		| (IS_KEY_PRESSED(CTRL_KEY_POS)   ? MODKEY_CTRL   : 0)
		| (IS_KEY_PRESSED(CBM_KEY_POS)    ? MODKEY_CBM    : 0)
#ifdef ALT_KEY_POS
		| (IS_KEY_PRESSED(ALT_KEY_POS)    ? MODKEY_ALT    : 0)
#endif
#ifdef SCRL_KEY_POS
		| (IS_KEY_PRESSED(SCRL_KEY_POS)   ? MODKEY_SCRL   : 0)
#endif
	;
	DEBUGKBD("KBD: pos = %d sdl_key = %d, pressed = %d, handled = %d" NL, pos, key, pressed, handled);
	if (pressed) {
		if ((pos & (~0xF7)) == 0) {
			// Xemu has a design to have key positions stored in row/col as low/high nible of a byte
			// normalize this here, to have a linear index
			int i = ((pos & 0xF0) >> 1) | (pos & 7);
			if (i < MAT2ASC_TAB_SIZE) {
				int si = i;
				i = matrix_to_ascii_table_selector[key_modifiers & 0xF][i];
				store_new_ascii_keypress(i);
				DEBUGKBD("KBD: cool, ASCII val $%02X '%c' is stored for pos %X (serialized to %d)" NL,
					i,
					i >= 32 && i < 127 ? i : '?',
					pos,
					si
				);
			}
		}
		// Also check for special, emulator-related hot-keys (not C65 key)
		if (key == SDL_SCANCODE_F10) {
			reset_mega65();
		} else if (key == SDL_SCANCODE_KP_ENTER) {
			c64_toggle_joy_emu();
			OSD(-1, -1, "Joystick emulation on port #%d", joystick_emu);
		} else if (key == SDL_SCANCODE_ESCAPE) {
			set_mouse_grab(SDL_FALSE);
		}
	} else {
		if (pos == -2 && key == 0) {	// special case pos = -2, key = 0, handled = mouse button (which?) and release event!
			if (handled == SDL_BUTTON_LEFT) {
				OSD(-1, -1, "Mouse grab activated.\nPress ESC to cancel.");
				set_mouse_grab(SDL_TRUE);
			}
		}
	}
	return 0;
}
