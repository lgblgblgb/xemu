/* A work-in-progess MEGA65 (Commodore-65 clone origins) emulator
   Part of the Xemu project, please visit: https://github.com/lgblgblgb/xemu
   Copyright (C)2016-2021 LGB (Gábor Lénárt) <lgblgblgb@gmail.com>

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
#include "hypervisor.h"
#include "ui.h"


#define DEBUGKBD(...)		DEBUG(__VA_ARGS__)
#define DEBUGKBDHWA(...)	DEBUG(__VA_ARGS__)
#define DEBUGKBDHWACOM(...)	//DEBUGPRINT(__VA_ARGS__)


/* Note: M65 has a "hardware accelerated" keyboard scanner, it can provide you the
   last pressed character as ASCII (!) without the need to actually scan/etc the
   keyboard matrix */

/* These values are from matrix_to_ascii.vhdl in mega65-core project */
/* Some explanation is at the comments of function hwa_kbd_convert_and_push() */

// 64 possibility of C64 keys (ie, the 8*8 matrix) + 8 extra C65 keys = 72
#define MAT2ASC_TAB_SIZE	72

static const Uint8 matrix_normal_to_ascii[MAT2ASC_TAB_SIZE] ={0x14,0x0D,0x1d,0xf7,0xf1,0xf3,0xf5,0x11,0x33,0x77,0x61,0x34,0x7a,0x73,0x65,0x00,0x35,0x72,0x64,0x36,0x63,0x66,0x74,0x78,0x37,0x79,0x67,0x38,0x62,0x68,0x75,0x76,0x39,0x69,0x6a,0x30,0x6d,0x6b,0x6f,0x6e,0x2b,0x70,0x6c,0x2d,0x2e,0x3a,0x40,0x2c,0x00,0x2a,0x3b,0x13,0x00,0x3d,0x00,0x2f,0x31,0x5f,0x00,0x32,0x20,0x00,0x71,0x03,0x00,0x09,0x00,0x00,0xf9,0xfb,0xfd,0x1b};
static const Uint8 matrix_shift_to_ascii[MAT2ASC_TAB_SIZE]  ={0x94,0x0D,0x9d,0xf8,0xf2,0xf4,0xf6,0x91,0x23,0x57,0x41,0x24,0x5a,0x53,0x45,0x00,0x25,0x52,0x44,0x26,0x43,0x46,0x54,0x58,0x27,0x59,0x47,0x28,0x42,0x48,0x55,0x56,0x29,0x49,0x4a,0x7b,0x4d,0x4b,0x4f,0x4e,0x00,0x50,0x4c,0x00,0x3e,0x5b,0x00,0x3c,0x00,0x00,0x5d,0x93,0x00,0x5f,0x00,0x3f,0x21,0x60,0x00,0x22,0x20,0x00,0x51,0xa3,0x00,0x0f,0x00,0x00,0xfa,0xfc,0xfe,0x1b};
static const Uint8 matrix_control_to_ascii[MAT2ASC_TAB_SIZE]={0x94,0x0D,0x9d,0xf8,0xf2,0xf4,0xf6,0x91,0x1c,0x17,0x01,0x9f,0x1a,0x13,0x05,0x00,0x9c,0x12,0x04,0x1e,0x03,0x06,0x14,0x18,0x1f,0x19,0x07,0x9e,0x02,0x08,0x15,0x16,0x12,0x09,0x0a,0x00,0x0d,0x0b,0x0f,0x0e,0x2b,0x10,0x0c,0x2d,0x2e,0x3a,0x40,0x2c,0x00,0xEF,0x3b,0x93,0x00,0x3d,0x00,0x2f,0x90,0x60,0x00,0x05,0x20,0x00,0x11,0xa3,0x00,0x0f,0x00,0x00,0xfa,0xfc,0xfe,0x1b};
static const Uint8 matrix_cbm_to_ascii[MAT2ASC_TAB_SIZE]    ={0x94,0x0D,0xED,0xf8,0xf2,0xf4,0xf6,0xEE,0x96,0xd7,0xc1,0x97,0xda,0xd3,0xc5,0x00,0x98,0xd2,0xc4,0x99,0xc3,0xc6,0xd4,0xd8,0x9a,0xd9,0xc7,0x9b,0xc2,0xc8,0xd5,0xd6,0x92,0xc9,0xca,0x81,0xcd,0xcb,0xcf,0xce,0x2b,0xd0,0xcc,0x2d,0x7c,0x7b,0x40,0x7e,0x00,0x2A,0x7d,0x93,0x00,0x5f,0x00,0x5c,0x81,0x60,0x00,0x95,0x20,0x00,0xd1,0xa3,0x00,0xef,0x00,0x00,0xfa,0xfc,0xfe,0x1b};
static const Uint8 matrix_alt_to_ascii[MAT2ASC_TAB_SIZE]    ={0x00,0x00,0x00,0x00,0xB9,0xB2,0xB3,0x00,0xA4,0xAE,0xE5,0xA2,0xF7,0xA7,0xE6,0x00,0xB0,0xAE,0xF0,0xA5,0xE7,0x00,0xFE,0xD7,0xB4,0xFF,0x00,0x00,0xFA,0xFD,0xFC,0x00,0x00,0xED,0xE9,0x00,0xB5,0xE1,0xF8,0xF1,0xB1,0xB6,0xF3,0xAC,0xBB,0xE4,0xA8,0xAB,0xA3,0xB7,0xE4,0x00,0x00,0xAF,0x00,0xBF,0xA1,0x00,0x00,0x00,0x00,0x00,0xA9,0x00,0x00,0x00,0x00,0x00,0xBC,0xBD,0xBE,0x00};

#define MODKEY_LSHIFT	0x01
#define MODKEY_RSHIFT	0x02
#define MODKEY_CTRL	0x04
#define	MODKEY_CBM	0x08
#define MODKEY_ALT	0x10
#define MODKEY_SCRL	0x20

// Decoding table based on modoifer keys (the index is the MODKEY stuffs, low 4 bits)
// Priority is CBM, ALT, SHIFT, CTRL
static const Uint8 *matrix_to_ascii_table_selector[32] = {
	matrix_normal_to_ascii,  matrix_shift_to_ascii,   matrix_shift_to_ascii,   matrix_shift_to_ascii,
	matrix_control_to_ascii, matrix_control_to_ascii, matrix_control_to_ascii, matrix_control_to_ascii,
	matrix_cbm_to_ascii,     matrix_cbm_to_ascii,     matrix_cbm_to_ascii,     matrix_cbm_to_ascii,		// CBM key has priority
	matrix_cbm_to_ascii,     matrix_cbm_to_ascii,     matrix_cbm_to_ascii,     matrix_cbm_to_ascii,		// CBM key has priority
	matrix_alt_to_ascii,     matrix_alt_to_ascii,     matrix_alt_to_ascii,     matrix_alt_to_ascii,
	matrix_alt_to_ascii,     matrix_alt_to_ascii,     matrix_alt_to_ascii,     matrix_alt_to_ascii,
	matrix_cbm_to_ascii,     matrix_cbm_to_ascii,     matrix_cbm_to_ascii,     matrix_cbm_to_ascii,
	matrix_cbm_to_ascii,     matrix_cbm_to_ascii,     matrix_cbm_to_ascii,     matrix_cbm_to_ascii
};

#define HWA_SINGLE_ITEM


static struct {
	Uint8	modifiers;
	Uint8	next;
	Uint8	last;
} hwa_kbd;

static int restore_is_held = 0;
static Uint8 virtkey_state[3] = { 0xFF, 0xFF, 0xFF };


void hwa_kbd_fake_key ( Uint8 k )
{
	hwa_kbd.next = 0;
	hwa_kbd.last = k;
}


/* used by actual I/O function to read $D610 */
Uint8 hwa_kbd_get_last ( void )
{
	if (hwa_kbd.next && !hwa_kbd.last) {
		hwa_kbd.last = hwa_kbd.next;
		hwa_kbd.next = 0;
	}
	DEBUGKBDHWACOM("KBD: HWA: reading key @ PC=$%04X result = $%02X" NL, cpu65.pc, hwa_kbd.last);
	return hwa_kbd.last;
}


/* used by actual I/O function to read $D611 */
Uint8 hwa_kbd_get_modifiers ( void )
{
	DEBUGKBDHWACOM("KBD: HWA: reading key modifiers @ PC=$%04X result = $%02X" NL, cpu65.pc, hwa_kbd.modifiers);
	return hwa_kbd.modifiers;
}


/* used by actual I/O function to write $D610, the written data itself is not used, only the fact of writing */
void hwa_kbd_move_next ( void )
{
	DEBUGKBDHWACOM("KBD: HWA: moving to next key @ PC=$%04X previous queued key = $%02X" NL, cpu65.pc, hwa_kbd.last);
	hwa_kbd.last = 0;
}


#define CHR_EQU(i) ((i >= 32 && i < 127) ? (char)i : '?')


/* basically the opposite as kbd_get_last() but this one used internally only
 * This is called by emu_callback_key() which is called by emutools_hid.c on key events.
 * Purpose: convert keypress into MEGA65 hardware accelerated keyboard scanner's ASCII
 * (which is basically ASCII, though with "invented" codes for the non-printable char keys (like F1 or RUN/STOP).
 * Notions of variable names:
 *   - pos: emutools_hid.c related "position" info (of the key), non-linear, see the comments at the fist "if" at its two branches
 *   - scan: MEGA65 "scan code" (also 'table index' to index within the matrix2ascii tables): 0-63 nornal "c64 keys" (64 possibilities, 8*8 matrix), 64-71 "c65 extra keys" (8 possibilities)
 *   - ascii: the result ASCII value (with the mentioned "invented" codes included)
 */
static void hwa_kbd_convert_and_push ( int pos )
{
	int scan;
	if (pos >= (C65_KEYBOARD_EXTRA_POS) && pos < ((C65_KEYBOARD_EXTRA_POS) + 8)) {
		// Ugly hack: this should fix the problem that Xemu has disjoint space for std C64 and extra C65 keys ...
		scan = pos - (C65_KEYBOARD_EXTRA_POS) + 64;
		DEBUGKBDHWA("KBD: HWA: PUSH: doing C65 extra key translation from kbd pos $%02X to table index $%02X ..." NL, pos, scan);
	} else {
		// this is the normal case (ie, no special extra C65 key, but among the regular C64 ones):
		// Xemu has a design to have key positions stored in row/col as low/high nybble of a byte
		// normalize this here, to have a linear index.
		// Note, that "extra C65 keys" are kinda handled as a hack, see above.
		scan = ((pos & 0xF0) >> 1) | (pos & 7);
		if (scan > 63) {
			DEBUGKBDHWA("KBD: HWA: PUSH: NOT storing key (outside of translation table) from kbd pos $%02X and table index $%02X at PC=$%04X" NL, pos, scan, cpu65.pc);
			return;
		}
	}
	// Now, convert scan code to MEGA65 ASCII value, using one of the conversion tables selected by the actual used modifier key(s)
	// Size of conversion table is 72 (64+8, C64keys+C65keys). This is already checked above, so it must be ok to do so without any further boundary checks
	int ascii = matrix_to_ascii_table_selector[hwa_kbd.modifiers & 0x1F][scan];
	if (ascii) {
		if (!hwa_kbd.next) {
			DEBUGKBDHWA("KBD: HWA: PUSH: storing key $%02X '%c' from kbd pos $%02X and table index $%02X at PC=$%04X" NL, ascii, CHR_EQU(ascii), pos, scan, cpu65.pc);
			hwa_kbd.next = ascii;
		} else
			DEBUGKBDHWA("KBD: HWA: PUSH: NOT storing key (already waiting) $%02X '%c' from kbd pos $%02X and table index $%02X at PC=$%04X" NL, ascii, CHR_EQU(ascii), pos, scan, cpu65.pc);
	} else
		DEBUGKBDHWA("KBD: HWA: PUSH: NOT storing key (zero in translation table) from kbd pos $%02X and table index $%02X at PC=$%04X" NL, pos, scan, cpu65.pc);
}


// MEGA65's own way to do keyboard matrix scan. The theory is very similar to the C64 style
// (via CIA ports) scan, however the differenced/benefits:
// * no joystick interference on the keyboard
// * C65 extra keys are part of the main matrix
// * row selection is a simple number not mask of rows (that can be a "con" too if you want to check multiple rows at once?)
Uint8 kbd_directscan_query ( Uint8 row )
{
	if (row > 8)	// FIXME: what happens in this case?
		return 0xFF;
	if (row == 8)	// this is the "extra" row of the "C65 keys"
		return kbd_matrix[(C65_KEYBOARD_EXTRA_POS) >> 4];
	// Otherwise the normal "C64-style" matrix is used.
	return kbd_matrix[row];
}


void clear_emu_events ( void )
{
	DEBUGKBDHWA("KBD: HWA: reset" NL);
	hid_reset_events(1);
	hwa_kbd.modifiers = 0;
	hwa_kbd.next = 0;
	hwa_kbd.last = 0;
	for (int a = 0; a < 3; a++) {
		if (virtkey_state[0] != 0xFF) {
			hid_sdl_synth_key_event(virtkey_state[a], 0);
			virtkey_state[a] = 0xFF;
		}

	}
}


void input_toggle_joy_emu ( void )
{
	c64_toggle_joy_emu();
	OSD(-1, -1, "Joystick emulation on port #%d", joystick_emu);
}


void virtkey ( Uint8 rno, Uint8 scancode )
{
	// Convert scancode to "Xemu kind of scan code" ...
	if (scancode >= MAT2ASC_TAB_SIZE)
		scancode = 0xFF;
	else if (scancode < 64)
		scancode = ((scancode & (32 + 16 + 8)) << 1) | (scancode & 7);
	else
		scancode += C65_KEYBOARD_EXTRA_POS - 64;
	if (virtkey_state[rno] == scancode)
		return;
	if (virtkey_state[rno] != 0xFF)
		hid_sdl_synth_key_event(virtkey_state[rno], 0);
	virtkey_state[rno] = scancode;
	if (scancode != 0xFF)
		hid_sdl_synth_key_event(scancode, 1);
}


Uint8 cia1_in_b ( void )
{
#ifdef FAKE_TYPING_SUPPORT
	if (XEMU_UNLIKELY(c64_fake_typing_enabled) && (((cia1.PRA | (~cia1.DDRA)) & 0xFF) != 0xFF) && (((cia1.PRB | (~cia1.DDRB)) & 0xFF) == 0xFF))
		c64_handle_fake_typing_internals(cia1.PRA | (~cia1.DDRA));
#endif
	return c64_keyboard_read_on_CIA1_B(
		cia1.PRA | (~cia1.DDRA),
		cia1.PRB | (~cia1.DDRB),
		joystick_emu == 1 ? c64_get_joy_state() : 0xFF,
		port_d607 & 2
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


void kbd_trigger_restore_trap ( void )
{
	if (XEMU_UNLIKELY(restore_is_held)) {
		restore_is_held++;
		if (restore_is_held >= 20) {
			restore_is_held = 0;
#ifdef FREEZER_WORKS
			if (!in_hypervisor) {
				DEBUGPRINT("KBD: RESTORE trap has been triggered." NL);
				KBD_RELEASE_KEY(RESTORE_KEY_POS);
				hypervisor_enter(TRAP_RESTORE);
			} else
				DEBUGPRINT("KBD: *IGNORING* RESTORE trap trigger, already in hypervisor mode!" NL);
#else
			WARNING_WINDOW("Long press of RESTORE would trigger FREEZER.\nHowever FREEZER is not yet implemented in Xemu :-(");
#endif
		}
	}
}


static void kbd_trigger_alttab_trap ( void )
{
	KBD_RELEASE_KEY(TAB_KEY_POS);
	//KBD_RELEASE_KEY(ALT_KEY_POS);
	//hwa_kbd.modifiers &= ~MODKEY_ALT;
	if (!in_hypervisor) {
		DEBUGPRINT("KBD: ALT-TAB trap has been triggered." NL);
		hypervisor_enter(TRAP_ALTTAB);
	} else
		DEBUGPRINT("KBD: *IGNORING* ALT-TAB trap trigger, already in hypervisor mode!" NL);
}


/* BEGIN HACK */
// Super ugly way to implement key repeats with the hardware accelerated ASCII based keyboard scanner.
// Since rest of Xemu, the kbd-matrix emulation want to actually DISABLE any repeated key events to
// come ... For this trick, xemu-target.h must contain: #define CONFIG_KBD_ALSO_RAW_SDL_CALLBACK
// TODO: this whole mess of the HID must be resolved some day in a much nicer way. Not only this
// problem but in general (like decoding 'hotkeys' of emulator here in this file and things
// like that ...)
static SDL_Scancode last_scancode_seen = SDL_SCANCODE_UNKNOWN;
static int last_poscode_seen = 0;

void emu_callback_key_raw_sdl ( SDL_KeyboardEvent *ev )
{
	if (ev->repeat && ev->state == SDL_PRESSED && ev->keysym.scancode == last_scancode_seen) {
		hwa_kbd_convert_and_push(last_poscode_seen);
	}
}
/* END HACK */

// Called by emutools_hid!!! to handle special private keys assigned to this emulator
int emu_callback_key ( int pos, SDL_Scancode key, int pressed, int handled )
{
	// Update status of modifier keys
	hwa_kbd.modifiers =
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
	DEBUGKBD("KBD: HWA: pos = %d sdl_key = %d, pressed = %d, handled = %d" NL, pos, key, pressed, handled);
	if (pressed) {
		// check if we have the ALT-TAB trap triggered (TAB is pressed now, and ALT is hold)
		if (pos == TAB_KEY_POS && (hwa_kbd.modifiers & MODKEY_ALT)) {
			kbd_trigger_alttab_trap();
			return 0;
		}
		// RESTORE triggered trap is different as it depends on timing (how long it's pressed)
		// So we just flag this, and the main emulation loop need to increment the value to see if the long press event comes, and trigger the trap.
		// This is done by main emulation loop calling kbd_trigger_restore_trap() function, see above in this very source.
		// Please note about the pair of this condition below with the "else" branch of the "pressed" condition.
		if (pos == RESTORE_KEY_POS)
			restore_is_held = 1;
	        // Check to be sure, some special Xemu internal stuffs uses kbd matrix positions does not exist for real
		if (pos >= 0 && pos < 0x100) {
			hwa_kbd_convert_and_push(pos);
			// See the "HACK" above about key repeating ...
			last_scancode_seen = key;
			last_poscode_seen = pos;
		}
		// Also check for special, emulator-related hot-keys
		if (key == SDL_SCANCODE_F10) {
			reset_mega65_asked();
		} else if (key == SDL_SCANCODE_KP_ENTER) {
			input_toggle_joy_emu();
		} else if (((hwa_kbd.modifiers & (MODKEY_LSHIFT | MODKEY_RSHIFT)) == (MODKEY_LSHIFT | MODKEY_RSHIFT)) && set_mouse_grab(SDL_FALSE, 0)) {
			DEBUGPRINT("UI: mouse grab cancelled" NL);
		}
	} else {
		if (pos == RESTORE_KEY_POS)
			restore_is_held = 0;
		if (pos == -2 && key == 0) {	// special case pos = -2, key = 0, handled = mouse button (which?) and release event!
			if ((handled == SDL_BUTTON_LEFT) && set_mouse_grab(SDL_TRUE, 0)) {
				OSD(-1, -1, "Mouse grab activated. Press\nboth SHIFTs together to cancel.");
				DEBUGPRINT("UI: mouse grab activated" NL);
			}
			if (handled == SDL_BUTTON_RIGHT) {
				ui_enter();
			}
		}
	}
	return 0;
}


Uint8 get_mouse_x_via_sid ( void )
{
	if (!is_mouse_grab())
		return 0xFF;
	static int mouse_x = 0;
	mouse_x = (mouse_x + hid_read_mouse_rel_x(-31, 31)) & 63;
	DEBUG("MOUSE-X: reading X as %d" NL, mouse_x << 1);
	return mouse_x << 1;
}


Uint8 get_mouse_y_via_sid ( void )
{
	if (!is_mouse_grab())
		return 0xFF;
	static int mouse_y = 0;
	mouse_y = (mouse_y - hid_read_mouse_rel_y(-31, 31)) & 63;
	DEBUG("MOUSE-Y: reading Y as %d" NL, mouse_y << 1);
	return mouse_y << 1;
}
