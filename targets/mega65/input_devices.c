/* A work-in-progess MEGA65 (Commodore-65 clone origins) emulator
   Part of the Xemu project, please visit: https://github.com/lgblgblgb/xemu
   Copyright (C)2016-2025 LGB (Gábor Lénárt) <lgblgblgb@gmail.com>

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
#include "input_devices.h"
#include "xemu/emutools_hid.h"
#include "xemu/c64_kbd_mapping.h"
#include "mega65.h"
#include "io_mapper.h"
#include "xemu/cpu65.h"
#include "hypervisor.h"
#include "ui.h"
#include "matrix_mode.h"
#include "dma65.h"
#include "configdb.h"

#include <string.h>


#define DEBUGKBD(...)		DEBUG(__VA_ARGS__)
#define DEBUGKBDHWA(...)	DEBUG(__VA_ARGS__)
#define DEBUGKBDHWACOM(...)	//DEBUGPRINT(__VA_ARGS__)


/* Note: M65 has a "hardware accelerated" keyboard scanner, it can provide you the
   last pressed character as ASCII (!) and PETSCII without the need to actually scan/etc the
   keyboard matrix */

/* These values are from matrix_to_ascii.vhdl in mega65-core project */
/* Some explanation is at the comments of function hwa_kbd_convert_and_push() */

// 64 possibility of C64 keys (ie, the 8*8 matrix) + 8 extra C65 keys = 72
#define KBD_MATRIX_SIZE		72

static const Uint8 matrix_base_to_ascii[KBD_MATRIX_SIZE] = {0x14,0x0D,0x1d,0xf7,0xf1,0xf3,0xf5,0x11,0x33,0x77,0x61,0x34,0x7a,0x73,0x65,0x00,0x35,0x72,0x64,0x36,0x63,0x66,0x74,0x78,0x37,0x79,0x67,0x38,0x62,0x68,0x75,0x76,0x39,0x69,0x6a,0x30,0x6d,0x6b,0x6f,0x6e,0x2b,0x70,0x6c,0x2d,0x2e,0x3a,0x40,0x2c,0xa3,0x2a,0x3b,0x13,0x00,0x3d,0xAF,0x2f,0x31,0x5f,0x00,0x32,0x20,0x00,0x71,0x03,0x00,0x09,0x00,0x1f,0xf9,0xfb,0xfd,0x1b};
static const Uint8 matrix_shift_to_ascii[KBD_MATRIX_SIZE] = {0x94,0x0D,0x9d,0xf8,0xf2,0xf4,0xf6,0x91,0x23,0x57,0x41,0x24,0x5a,0x53,0x45,0x00,0x25,0x52,0x44,0x26,0x43,0x46,0x54,0x58,0x27,0x59,0x47,0x28,0x42,0x48,0x55,0x56,0x29,0x49,0x4a,0x7b,0x4d,0x4b,0x4f,0x4e,0x00,0x50,0x4c,0x00,0x3e,0x5b,0x00,0x3c,0xE0,0x00,0x5d,0x93,0x00,0x5f,0x00,0x3f,0x21,0x60,0x00,0x22,0x20,0x00,0x51,0xa3,0x00,0x0f,0x00,0x1f,0xfa,0xfc,0xfe,0x1b};
static const Uint8 matrix_ctrl_to_ascii[KBD_MATRIX_SIZE] = {0x94,0x0D,0x9d,0xf8,0xf2,0xf4,0xf6,0x91,0x1c,0x17,0x01,0x9f,0x1a,0x13,0x05,0x00,0x9c,0x12,0x04,0x1e,0x03,0x06,0x14,0x18,0x1f,0x19,0x07,0x9e,0x02,0x08,0x15,0x16,0x12,0x09,0x0a,0x00,0x0d,0x0b,0x0f,0x0e,0x2b,0x10,0x0c,0x2d,0x2e,0x3a,0x40,0x2c,0x00,0xEF,0x3b,0x93,0x00,0x3d,0x00,0x2f,0x90,0x60,0x00,0x05,0x20,0x00,0x11,0xa3,0x00,0x0f,0x00,0x1f,0xfa,0xfc,0xfe,0x1b};
static const Uint8 matrix_cbm_to_ascii[KBD_MATRIX_SIZE] = {0x94,0x0D,0xED,0xf8,0xf2,0xf4,0xf6,0xEE,0x96,0xd7,0xc1,0x97,0xda,0xd3,0xc5,0x00,0x98,0xd2,0xc4,0x99,0xc3,0xc6,0xd4,0xd8,0x9a,0xd9,0xc7,0x9b,0xc2,0xc8,0xd5,0xd6,0x92,0xc9,0xca,0x81,0xcd,0xcb,0xcf,0xce,0x2b,0xd0,0xcc,0x2d,0x7c,0x7b,0x40,0x7e,0x00,0x2A,0x7d,0x93,0x00,0x5f,0x00,0x5c,0x81,0x60,0x00,0x95,0x20,0x00,0xd1,0xa3,0x00,0xef,0x00,0x1f,0xfa,0xfc,0xfe,0x1b};
static const Uint8 matrix_alt_to_ascii[KBD_MATRIX_SIZE] = {0x7f,0x00,0xdf,0xde,0xB9,0xB2,0xB3,0x00,0xA4,0xAE,0xE5,0xA2,0xF7,0xA7,0xE6,0x00,0xB0,0xAE,0xF0,0xA5,0xE7,0x00,0xFE,0xD7,0xB4,0xFF,0xE8,0xE2,0xFA,0xFD,0xFC,0xd3,0xda,0xED,0xE9,0xdb,0xB5,0xE1,0xF8,0xF1,0xB1,0xB6,0xF3,0xAC,0xBB,0xE4,0xA8,0xAB,0xA3,0xB7,0xE4,0xDC,0xDD,0xA6,0xAF,0xBF,0xA1,0xB8,0x00,0xAA,0xa0,0x00,0xA9,0xBA,0x00,0xC0,0x00,0x1f,0xBC,0xBD,0xBE,0xDB};

static const Uint8 matrix_base_to_petscii[KBD_MATRIX_SIZE] = {
	0x14,0x0d,0x1d,0x88,0x85,0x86,0x87,0x11,	// del ret rt  f7  f1  f3  f5  dn
	0x33,0x57,0x41,0x34,0x5a,0x53,0x45,0xff,	//  3   w   a   4   z   s   e  shf
	0x35,0x52,0x44,0x36,0x43,0x46,0x54,0x58,	//  5   r   d   6   c   f   t   x
	0x37,0x59,0x47,0x38,0x42,0x48,0x55,0x56,	//  7   y   g   8   b   h   u   v
	0x39,0x49,0x4a,0x30,0x4d,0x4b,0x4f,0x4e,	//  9   i   j   0   m   k   o   n
	0x2b,0x50,0x4c,0x2d,0x2e,0x3a,0x40,0x2c,	//  +   p   l   -   .   :   @   ,
	0x5c,0x2a,0x3b,0x13,0xff,0x3d,0x5e,0x2f,	// lb.  *   ;  hom shf  =   ^   /
	0x31,0x5f,0xff,0x32,0x20,0xff,0x51,0x03,	//  1  <-- ctl  2  spc  C=  q stop
	0xff,0x09,0xff,0x84,0x10,0x16,0x19,0x1b		// scl tab alt hlp  f9 f11 f13 esc
};
static const Uint8 matrix_shift_to_petscii[KBD_MATRIX_SIZE] = {		// mode2: English shifted keys (right keycap graphics)
	0x94,0x8d,0x9d,0x8c,0x89,0x8a,0x8b,0x91,	// ins RTN lft f8  f2  f4  f6  up
	0x23,0xd7,0xc1,0x24,0xda,0xd3,0xc5,0xff,	//  #   W   A   $   Z   S   E  shf
	0x25,0xd2,0xc4,0x26,0xc3,0xc6,0xd4,0xd8,	//  %   R   D   &   C   F   T   X
	0x27,0xd9,0xc7,0x28,0xc2,0xc8,0xd5,0xd6,	//  '   Y   G   (   B   H   U   V
	0x29,0xc9,0xca,0x30,0xcd,0xcb,0xcf,0xce,	//  )   I   J   0   M   K   O   N
	0xdb,0xd0,0xcc,0xdd,0x3e,0x5b,0xba,0x3c,	// +gr  P   L  -gr  >   [  @gr  <
	0xa9,0xc0,0x5d,0x93,0xff,0x3d,0xde,0x3f,	// lbg *gr  ]  clr shf  =  pi   ?
	0x21,0x5f,0xff,0x22,0xa0,0xff,0xd1,0x83,	//  !  <-- ctl  \" SPC  C=  Q  run
	0xff,0x1a,0xff,0x84,0x15,0x17,0x1a,0x1b		// scl TAB alt hlp f10 f12 f14 esc
};
static const Uint8 matrix_ctrl_to_petscii[KBD_MATRIX_SIZE] = {		// mode4: English control keys
	0xff,0xff,0xff,0x88,0x85,0x86,0x87,0xff,	//  ~   ~   ~   ~   ~   ~   ~   ~
	0x1c,0x17,0x01,0x9f,0x1a,0x13,0x05,0xff,	// red /w  /a  cyn /z  /s  /e   ~
	0x9c,0x12,0x04,0x1e,0x03,0x06,0x14,0x18,	// pur /r  /d  grn /c  /f  /t  /x
	0x1f,0x19,0x07,0x9e,0x02,0x08,0x15,0x16,	// blu /y  /g  yel /b  /h  /u  /v
	0x12,0x09,0x0a,0x92,0x0d,0x0b,0x0f,0x0e,	// ron /i  /j  rof /m  /k  /o  /n
	0xff,0x10,0x0c,0xff,0xff,0x1b,0x00,0xff,	//  ~  /p  /l   ~   ~  /[  /@   ~
	0x1c,0xff,0x1d,0xff,0xff,0xff,0x1e,0xff,	// /lb  ~  /]   ~   ~  /=  /pi  ~
	0x90,0x60,0xff,0x05,0xff,0xff,0x11,0xff,	// blk /<-  ~  wht  ~   ~  /q   ~
	0xff,0x09,0xff,0x84,0x10,0x16,0x19,0x1b		// scl tab alt hlp  ~   ~   ~  esc
};
static const Uint8 matrix_cbm_to_petscii[KBD_MATRIX_SIZE] = {		// mode3: English C= keys (left keycap graphics)
	0x94,0x8d,0x9d,0x8c,0x89,0x8a,0x8b,0x91,	// ins RTN lft f8  f2  f4  f6  up
	0x96,0xb3,0xb0,0x97,0xad,0xae,0xb1,0xff,	// red  W   A  cyn  Z   S   E  shf
	0x98,0xb2,0xac,0x99,0xbc,0xbb,0xa3,0xbd,	// pur  R   D  grn  C   F   T   X
	0x9a,0xb7,0xa5,0x9b,0xbf,0xb4,0xb8,0xbe,	// blu  Y   G  yel  B   H   U   V
	0x29,0xa2,0xb5,0x30,0xa7,0xa1,0xb9,0xaa,	//  )   I   J   0   M   K   O   N
	0xa6,0xaf,0xb6,0xdc,0x7c,0x7b,0xa4,0x7e,	// +gr  P   L  -gr  |   {  @gr  ~
	0xa8,0xdf,0x7d,0x93,0xff,0x5f,0xde,0x5c,	// lbg *gr  }  clr SHF  _  pi  bslash
	0x81,0x60,0xff,0x95,0xa0,0xff,0xab,0x03,	// blk <-- ctl wht spc  C=  Q  run
	0xff,0x18,0xff,0x84,0x15,0x17,0x1a,0x1b		// scl TAB alt hlp f10 f12 f14 esc
};
#if 0
static const Uint8 matrix_capslock_to_petscii[KBD_MATRIX_SIZE] = {	// mode5: English caps lock mode
	0x14,0x0d,0x1d,0x88,0x85,0x86,0x87,0x11,	// del ret rt  f7  f1  f3  f5  dn
	0x33,0xd7,0xc1,0x34,0xda,0xd3,0xc5,0xff,	//  3   w   a   4   z   s   e  shf
	0x35,0xd2,0xc4,0x36,0xc3,0xc6,0xd4,0xd8,	//  5   r   d   6   c   f   t   x
	0x37,0xd9,0xc7,0x38,0xc2,0xc8,0xd5,0xd6,	//  7   y   g   8   b   h   u   v
	0x39,0xc9,0xca,0x30,0xcd,0xcb,0xcf,0xce,	//  9   i   j   0   m   k   o   n
	0x2b,0xd0,0xcc,0x2d,0x2e,0x3a,0x40,0x2c,	//  +   p   l   -   .   :   @   ,
	0x5c,0x2a,0x3b,0x13,0xff,0x3d,0x5e,0x2f,	// lb.  *   ;   hom shf  =  ^   /
	0x31,0x5f,0xff,0x32,0x20,0xff,0xd1,0x03,	//  1  <-- ctl  2  spc  C=  q stop
	0xff,0x09,0xff,0x84,0x10,0x16,0x19,0x1b		// scl tab alt hlp  f9 f11 f13 esc
};
#endif

#define MODKEY_LSHIFT	0x01
#define MODKEY_RSHIFT	0x02
#define MODKEY_CTRL	0x04
#define	MODKEY_CBM	0x08
#define MODKEY_ALT	0x10
#define MODKEY_SCRL	0x20

// Decoding table based on modoifer keys (the index is the MODKEY stuffs, low 4 bits)
// Priority is CBM, ALT, SHIFT, CTRL
static const Uint8 *matrix_to_ascii_table_selector[32] = {
	matrix_base_to_ascii,   matrix_shift_to_ascii,   matrix_shift_to_ascii,   matrix_shift_to_ascii,
	matrix_ctrl_to_ascii,   matrix_ctrl_to_ascii,    matrix_ctrl_to_ascii,    matrix_ctrl_to_ascii,
	matrix_cbm_to_ascii,    matrix_cbm_to_ascii,     matrix_cbm_to_ascii,     matrix_cbm_to_ascii,		// CBM key has priority
	matrix_cbm_to_ascii,    matrix_cbm_to_ascii,     matrix_cbm_to_ascii,     matrix_cbm_to_ascii,		// CBM key has priority
	matrix_alt_to_ascii,    matrix_alt_to_ascii,     matrix_alt_to_ascii,     matrix_alt_to_ascii,
	matrix_alt_to_ascii,    matrix_alt_to_ascii,     matrix_alt_to_ascii,     matrix_alt_to_ascii,
	matrix_cbm_to_ascii,    matrix_cbm_to_ascii,     matrix_cbm_to_ascii,     matrix_cbm_to_ascii,
	matrix_cbm_to_ascii,    matrix_cbm_to_ascii,     matrix_cbm_to_ascii,     matrix_cbm_to_ascii
};
// Similar to the ASCII table but for PETSCII. It's also shorter.
static const Uint8 *matrix_to_petscii_table_selector[16] = {
	matrix_base_to_petscii, matrix_shift_to_petscii, matrix_shift_to_petscii, matrix_shift_to_petscii,
	matrix_ctrl_to_petscii, matrix_ctrl_to_petscii,  matrix_ctrl_to_petscii,  matrix_ctrl_to_petscii,
	matrix_cbm_to_petscii,  matrix_cbm_to_petscii,	 matrix_cbm_to_petscii,   matrix_cbm_to_petscii,
	matrix_cbm_to_petscii,  matrix_cbm_to_petscii,	 matrix_cbm_to_petscii,   matrix_cbm_to_petscii
};

#define HWA_KBD_QUEUE_SIZE	5

#define IS_QUEUE_EMPTY()	(queue_pos == 0)
#define IS_QUEUE_FULL()		(queue_pos == HWA_KBD_QUEUE_SIZE)

// kbd_queue is Xemu's representation of the unified ASCII/PETSCII/keymod queue
// least significant byte is ASCII, the next is PETSCII, the next is modkey (24 bits are used)
static Uint32 kbd_queue[HWA_KBD_QUEUE_SIZE];
static unsigned int queue_pos;
static int restore_is_held;
static Uint8 virtkey_state[3] = { 0xFF, 0xFF, 0xFF };
static Uint8 modkeys_mask;
static Uint8 modkeys;
static Uint8 crsr_leftup_status;


void hwa_kbd_disable_selector ( int state )
{
	state = state ? 0 : 0xFFU;
	if (state != modkeys_mask) {
		modkeys_mask = state;
		DEBUGKBDHWA("KBD: hardware accelerated keyboard scanner selector is now %s" NL, state ? "ENABLED" : "DISABLED");
	}
}


// No checks for queue length here, must be called with care!
static void add_hwa_fake_key_unprotected ( Uint8 asc, const int single_case )
{
	static Uint8 prev_asc = 0;
	if ((prev_asc == '\n' && asc == '\r') || (prev_asc == '\r' && asc == '\n')) {
		prev_asc = 0;
		return;
	}
	prev_asc = asc;
	Uint8 pet = asc;
	Uint8 mod = 0;
	if (asc == 10 || asc == 13) {
		asc = 13;
		pet = 13;
	} else if (asc < 32 || asc > 127) {
		return;
	} else if (asc >= 'A' && asc <= 'Z') {
		if (single_case) {
			asc += 32;
		} else {
			mod = MODKEY_LSHIFT;
			pet = asc + 32;
		}
	} else if (asc >= 'a' && asc <= 'z') {
		pet = asc - 32;
	} else if (asc == '@') {
		pet = 0;
	} else if (strchr("!\"#$%&'()<>?[]", asc)) {
		mod = MODKEY_LSHIFT;
	}
	kbd_queue[queue_pos++] = asc + (pet << 8) + (mod << 16);
}


// This works by ASCII input!
void hwa_kbd_set_fake_key ( const Uint8 asc )
{
	queue_pos = 0;
	if (asc)
		add_hwa_fake_key_unprotected(asc, 0);
}


const char *hwa_kbd_add_string ( const char *s, const int single_case )
{
	while (*s && !IS_QUEUE_FULL())
		add_hwa_fake_key_unprotected((Uint8)*s++, single_case);
	return s;
}


/* used by actual I/O function to read $D610 */
Uint8 hwa_kbd_get_queued_ascii ( void )
{
	if (IS_QUEUE_EMPTY())
		return 0x00;
	const Uint8 k = kbd_queue[0] & 0xFF;
	DEBUGKBDHWACOM("KBD: HWA: reading ASCII key @ PC=$%04X result = $%02X" NL, cpu65.pc, k);
	return k;
}


/* used by actual I/O function to read $D619 */
Uint8 hwa_kbd_get_queued_petscii ( void )
{
	if (IS_QUEUE_EMPTY())
		return 0xFF;
	const Uint8 k = (kbd_queue[0] >> 8) & 0xFF;
	DEBUGKBDHWACOM("KBD: HWA: reading PETSCII key @ PC=$%04X result = $%02X" NL, cpu65.pc, k);
	return k;
}


/* user by actual I/O function to read $D60A */
Uint8 hwa_kbd_get_queued_modkeys ( void )
{
	if (IS_QUEUE_EMPTY())
		return 0;
	const Uint8 k = ((kbd_queue[0] >> 16) & 0x7F) + (IS_QUEUE_EMPTY() ? 0 : 0x80);
	DEBUGKBDHWACOM("KBD: HWA: reading queued modkeys @ PC=$%04X result = $%02X" NL, cpu65.pc, k);
	return k;
}


/* used by actual I/O function to read $D611 */
Uint8 hwa_kbd_get_current_modkeys ( void )
{
	const Uint8 result = modkeys + ((~modkeys_mask) & 0x80);
	DEBUGKBDHWACOM("KBD: HWA: reading current modkeys @ PC=$%04X result = $%02X" NL, cpu65.pc, result);
	return result;
}


/* flush the full queue, used by write bit 7 of $D60A as '0' */
void hwa_kbd_flush_queue ( void )
{
	DEBUGKBDHWACOM("KBD: HWA: flushing queue @ PC=$%04X (previous value = %u)" NL, cpu65.pc, queue_pos);
	queue_pos = 0;
}


/* used by actual I/O functions to dequeue an item (if any) */
void hwa_kbd_move_next ( void )
{
	unsigned int new_queue_pos = 0;
	if (queue_pos > 1) {
		new_queue_pos = queue_pos - 1;
		memmove(kbd_queue, kbd_queue + 1, new_queue_pos * sizeof(Uint32));
	}
	DEBUGKBDHWACOM("KBD: HWA: moving to next ASCII key @ PC=$%04X keys left in queue: %u (previous value: %u)" NL, cpu65.pc, new_queue_pos, queue_pos);
	queue_pos = new_queue_pos;
}


/* This is called by emu_callback_key() which is called by emutools_hid.c on key events.
 * Purpose: convert keypress into MEGA65 hardware accelerated keyboard scanner's ASCII
 * (which is basically ASCII, though with "invented" codes for the non-printable char keys (like F1 or RUN/STOP).
 * Notions of variable names:
 *   - pos: emutools_hid.c related "position" info (of the key), non-linear, see the comments at the fist "if" at its two branches
 *   - scan: MEGA65 "scan code" (also 'table index' to index within the matrix2ascii tables): 0-63 nornal "c64 keys" (64 possibilities, 8*8 matrix), 64-71 "c65 extra keys" (8 possibilities)
 *   - ascii: the result ASCII value (with the mentioned "invented" codes included)
 */
static void hwa_kbd_convert_and_push ( const unsigned int pos )
{
	// Xemu has a design to have key positions stored in row/col as low/high nybble of a byte
	// normalize this here, to have a linear index.
	const unsigned int scan = ((pos & 0xF0) >> 1) | (pos & 7);
	if (scan >= KBD_MATRIX_SIZE) {
		DEBUGKBDHWA("KBD: HWA: PUSH: NOT storing key (outside of translation table) from kbd pos $%02X and table index $%02X at PC=$%04X" NL, pos, scan, cpu65.pc);
		return;
	}
	if (IS_QUEUE_FULL()) {
		DEBUGKBDHWA("KBD: HWA: PUSH: NOT storing key (queue is full) from kbd pos $%02X and table index $%02X at PC=$%04X" NL, pos, scan, cpu65.pc);
		return;
	}
	// Now, convert scan code to MEGA65 ASCII and PETSCII values, using one of the conversion tables selected by the actual used modifier key(s) [if selector is enabled]
	// Size of conversion table is 72 (64+8, C64keys+C65keys). This is already checked above, so it must be ok to do so without any further boundary checks
	unsigned int ascii_value    = matrix_to_ascii_table_selector  [modkeys & modkeys_mask & 0x1F][scan];
	unsigned int petscii_value  = matrix_to_petscii_table_selector[modkeys & modkeys_mask & 0x0F][scan];
	if (ascii_value == 0x00 && petscii_value == 0xFF) {
		DEBUGKBDHWA("KBD: HWA: PUSH: NOT storing key (no ASCII neither PETSCII representation) from kbd pos $%02X and table index $%02X at PC=$%04X" NL, pos, scan, cpu65.pc);
		return;
	}
	// The odd case, when ASCII table gets zero. The problem that it's also the "empty queue" marker. So the rule - it seems -
	// to give value of $FF instead, then. If PETSCII value is $FF (normally empty queue, but can be PI symbol) it seems we
	// can carry on regardless of the effect, and it's up to the programmer of MEGA65 to figure out if queue is really empty.
	if (ascii_value == 0x00)
		ascii_value = 0xFF;
	// FIXME: we don't need this [hopefully]. So remove it!
#if 0
	if (ascii_value == 0x00 || petscii_value == 0xFF) {
		// this is what prevent strange keys to be queued like shift alone and such
		DEBUGPRINT("KBD: HWA: PUSH: NOT storing key (would mean invalid ASCII[$%02X] or PETSCII[$%02X] value) from kbd pos $%02X and table index $%02X at PC=$%04X" NL,
			ascii_value, petscii_value,
			pos, scan, cpu65.pc
		);
		return;
	}
#endif
	kbd_queue[queue_pos++] = ascii_value + (petscii_value << 8) + (modkeys << 16);
}


// MEGA65's own way to do keyboard matrix scan. The theory is very similar to the C64 style
// (via CIA ports) scan, however the differenced/benefits:
// * no joystick interference on the keyboard
// * C65 extra keys are part of the main matrix
// * row selection is a simple number not mask of rows (that can be a "con" too if you want to check multiple rows at once?)
Uint8 kbd_directscan_query ( const Uint8 row )
{
	// row 0-7: C64-style matrix, row 8: C65/M65 extra row
	return row <= 8 ? kbd_matrix[row] : 0xFF;	// FIXME: what should happen if row > 8?
}


Uint8 kbd_query_leftup_status ( void )
{
	return crsr_leftup_status;
}


void clear_emu_events ( void )
{
	DEBUGKBDHWA("KBD: HWA: reset" NL);
	hid_reset_events(1);
	modkeys = 0;
	queue_pos = 0;
	crsr_leftup_status = 0;
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
	configdb.joyport = joystick_emu;
	OSD(-1, -1, "Joystick emulation on port #%d", joystick_emu);
}


// Used by D615,D616,D617 emulation, allows a program to
// simulate up to three keypresses as the same time (so the
// three registers, that is the 'rno' argument for, being: 0,1,2).
void virtkey ( Uint8 rno, Uint8 scancode )
{
	// Convert scancode to "Xemu kind of scan code" ...
	scancode = scancode < KBD_MATRIX_SIZE ? ((scancode & 0xF8) << 1) | (scancode & 7) : 0xFF;
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
			if (XEMU_UNLIKELY(in_hypervisor)) {
				DEBUGPRINT("KBD: *IGNORING* RESTORE trap trigger, already in hypervisor mode!" NL);
			} else if (XEMU_UNLIKELY(in_dma)) {
				// keyboard triggered trap is "async" but the CPU must be not in DMA mode to be able to handle that without a disaster
				DEBUGPRINT("KBD: *IGNORING* RESTORE trap trigger, DMA is in progress!" NL);
			} else {
				DEBUGPRINT("KBD: RESTORE trap has been triggered." NL);
				KBD_RELEASE_KEY(RESTORE_KEY_POS);
				hypervisor_enter(TRAP_FREEZER_RESTORE_PRESS);
			}
		}
	}
}


static void kbd_trigger_matrix_trap ( void )
{
	KBD_RELEASE_KEY(TAB_KEY_POS);
	//KBD_RELEASE_KEY(ALT_KEY_POS);
	//modkeys &= ~MODKEY_ALT;
	matrix_mode_toggle(!in_the_matrix);
}


/* BEGIN HACK */
// Super ugly way to implement key repeats with the hardware accelerated ASCII based keyboard scanner.
// Since rest of Xemu, the kbd-matrix emulation want to actually DISABLE any repeated key events to
// come ... For this trick, the emu_callback_key_raw_sdl() handler must be registered, which is done
// in input_init() function.
// TODO: this whole mess of the HID must be resolved some day in a much nicer way. Not only this
// problem but in general (like decoding 'hotkeys' of emulator here in this file and things
// like that ...)
static SDL_Scancode last_scancode_seen = SDL_SCANCODE_UNKNOWN;
static int last_poscode_seen = 0;

static int emu_callback_key_raw_sdl ( SDL_KeyboardEvent *ev )
{
	if (ev->repeat && ev->state == SDL_PRESSED && ev->keysym.scancode == last_scancode_seen) {
		hwa_kbd_convert_and_push(last_poscode_seen);
	}
	return 1;	// allow default handler to run, though
}
/* END HACK */

// Called by emutools_hid!!! to handle special private keys assigned to this emulator
int emu_callback_key ( int pos, SDL_Scancode key, int pressed, int handled )
{
	// Update status of modifier keys
	modkeys =
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
	if (pos == 0x02 + 8) {
		// the "cursor left" key - would be shift+right, but on MEGA65 there is a direct method to query as well,
		// not only as a virtual key. Thus this "hack" does that.
		if (pressed)
			crsr_leftup_status |= 1;
		else
			crsr_leftup_status &= ~1;
	} else if (pos == 0x07 + 8) {
		// the "cursor up" key - would be shift+down, .... [see the comment above, with 'left']
		if (pressed)
			crsr_leftup_status |= 2;
		else
			crsr_leftup_status &= ~2;
	}
	if (pressed) {
		// check if we have the ALT-TAB trap triggered (TAB is pressed now, and ALT is hold)
		if (key == SDL_SCANCODE_TAB && (modkeys & MODKEY_CTRL)) {
			if (!configdb.matrixdisable) {
				kbd_trigger_matrix_trap();
			} else {
				DEBUGPRINT("MATRIX: matrix mode hotkey is disabled by config!" NL);
				clear_emu_events();
			}
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
		if (pos == XEMU_EVENT_RESET) {	// was hard-coded to be SDL_SCANCODE_F10 before
			reset_mega65(
				RESET_MEGA65_ASK | (
					((modkeys & (MODKEY_LSHIFT | MODKEY_RSHIFT))) ?	// this is BAD, the common but bad hack to check emu keymap to get modkey not the native ;-P
					RESET_MEGA65_HARD :
					configdb.resethotkeytype
				)
			);
		} else if (key == SDL_SCANCODE_KP_ENTER) {
			input_toggle_joy_emu();
		} else if (((modkeys & (MODKEY_LSHIFT | MODKEY_RSHIFT)) == (MODKEY_LSHIFT | MODKEY_RSHIFT)) && set_mouse_grab(SDL_FALSE, 0)) {
			DEBUGPRINT("UI: mouse grab cancelled" NL);
		}
	} else {
		if (pos == RESTORE_KEY_POS)
			restore_is_held = 0;
		if (pos == -2 && key == 0) {	// special case pos = -2, key = 0, handled = mouse button (which?) and release event!
			if (handled == SDL_BUTTON_LEFT) {
				if (configdb.nomouseemu) {
					OSD(-1, -1, "Mouse emulation is disabled.\nCannot enter mouse grab mode.");
				} else {
					if (set_mouse_grab(SDL_TRUE, 0)) {
						OSD(-1, -1, "Mouse grab activated. Press\nboth SHIFTs together to cancel.");
						DEBUGPRINT("UI: mouse grab activated" NL);
					}
				}
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
	if (is_mouse_grab()) {
		static Uint8 result = 0;
		static int mouse_x = 0;
		mouse_x = (mouse_x + (hid_read_mouse_rel_x(-23, 23) / 3)) & 0x3F;
		DEBUG("MOUSE: X is %d, result byte is %d" NL, mouse_x, result);
		result = mouse_x << 1;
		return result;
	}
	const Uint8 result = configdb.nomouseemu ? 0xFF : 0x00;
	DEBUG("MOUSE: X query without mouse-grab is $%02X" NL, result);
	return result;
}


Uint8 get_mouse_y_via_sid ( void )
{
	if (is_mouse_grab()) {
		static Uint8 result = 0;
		static int mouse_y = 0;
		mouse_y = (mouse_y - (hid_read_mouse_rel_y(-23, 23) / 3)) & 0x3F;
		DEBUG("MOUSE: Y is %d, result byte is %d" NL, mouse_y, result);
		result = mouse_y << 1;
		return result;
	}
	const Uint8 result = configdb.nomouseemu ? 0xFF : 0x00;
	DEBUG("MOUSE: Y query without mouse-grab is $%02X" NL, result);
	return result;
}


void input_init ( void )
{
	hid_register_sdl_keyboard_event_callback(HID_CB_LEVEL_EMU, emu_callback_key_raw_sdl);
	modkeys_mask = 0xFFU;
	queue_pos = 0;
	restore_is_held = 0;
	crsr_leftup_status = 0;
}
