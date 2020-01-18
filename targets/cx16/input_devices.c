/* The Xemu project.
   Copyright (C)2016-2019 LGB (Gábor Lénárt) <lgblgblgb@gmail.com>

   This is the Commander X16 emulation. Note: the source is overcrowded with comments by intent :)
   That it can useful for other people as well, or someone wants to contribute, etc ...

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
#include "input_devices.h"
#include "commander_x16.h"
#include <string.h>

struct ps2_keymap_st {
	SDL_Scancode	key;
	int		ps2_code;	// + 0x100 for extended codes
};

// This table is taken from Mist's X16 emulator, but converted into a table
// >0xFF codes are extended keys (and only the low byte should be treated then as the PS/2 keycode)
static const struct ps2_keymap_st ps2_keymap[] = {
	{SDL_SCANCODE_GRAVE,		0x0e},	{SDL_SCANCODE_BACKSPACE,	0x66},	{SDL_SCANCODE_TAB,		0xd},	{SDL_SCANCODE_CLEAR,		0},	{SDL_SCANCODE_RETURN,		0x5a},
	{SDL_SCANCODE_PAUSE,		0},	{SDL_SCANCODE_ESCAPE,		0x76},	{SDL_SCANCODE_SPACE,		0x29},	{SDL_SCANCODE_APOSTROPHE,	0x52},	{SDL_SCANCODE_COMMA,		0x41},
	{SDL_SCANCODE_MINUS,		0x4e},	{SDL_SCANCODE_PERIOD,		0x49},	{SDL_SCANCODE_SLASH,		0x4a},	{SDL_SCANCODE_0,		0x45},	{SDL_SCANCODE_1,		0x16},
	{SDL_SCANCODE_2,		0x1e},	{SDL_SCANCODE_3,		0x26},	{SDL_SCANCODE_4,		0x25},	{SDL_SCANCODE_5,		0x2e},	{SDL_SCANCODE_6,		0x36},
	{SDL_SCANCODE_7,		0x3d},	{SDL_SCANCODE_8,		0x3e},	{SDL_SCANCODE_9,		0x46},	{SDL_SCANCODE_SEMICOLON,	0x4c},	{SDL_SCANCODE_EQUALS,		0x55},
	{SDL_SCANCODE_LEFTBRACKET,	0x54},	{SDL_SCANCODE_BACKSLASH,	0x5d},	{SDL_SCANCODE_RIGHTBRACKET,	0x5b},	{SDL_SCANCODE_A,		0x1c},	{SDL_SCANCODE_B,		0x32},
	{SDL_SCANCODE_C,		0x21},	{SDL_SCANCODE_D,		0x23},	{SDL_SCANCODE_E,		0x24},	{SDL_SCANCODE_F,		0x2b},	{SDL_SCANCODE_G,		0x34},
	{SDL_SCANCODE_H,		0x33},	{SDL_SCANCODE_I,		0x43},	{SDL_SCANCODE_J,		0x3B},	{SDL_SCANCODE_K,		0x42},	{SDL_SCANCODE_L,		0x4B},
	{SDL_SCANCODE_M,		0x3A},	{SDL_SCANCODE_N,		0x31},	{SDL_SCANCODE_O,		0x44},	{SDL_SCANCODE_P,		0x4D},	{SDL_SCANCODE_Q,		0x15},
	{SDL_SCANCODE_R,		0x2D},	{SDL_SCANCODE_S,		0x1B},	{SDL_SCANCODE_T,		0x2C},	{SDL_SCANCODE_U,		0x3C},	{SDL_SCANCODE_V,		0x2A},
	{SDL_SCANCODE_W,		0x1D},	{SDL_SCANCODE_X,		0x22},	{SDL_SCANCODE_Y,		0x35},	{SDL_SCANCODE_Z,		0x1A},	{SDL_SCANCODE_DELETE,		0},
	{SDL_SCANCODE_UP,		0x175},	{SDL_SCANCODE_DOWN,		0x172},	{SDL_SCANCODE_RIGHT,		0x174},	{SDL_SCANCODE_LEFT,		0x16b},	{SDL_SCANCODE_INSERT,		0},
	{SDL_SCANCODE_HOME,		0x16c},	{SDL_SCANCODE_END,		0},	{SDL_SCANCODE_PAGEUP,		0},	{SDL_SCANCODE_PAGEDOWN,		0},	{SDL_SCANCODE_F1,		0x05},
	{SDL_SCANCODE_F2,		0x06},	{SDL_SCANCODE_F3,		0x04},	{SDL_SCANCODE_F4,		0x0c},	{SDL_SCANCODE_F5,		0x03},	{SDL_SCANCODE_F6,		0x0b},
	{SDL_SCANCODE_F7,		0x83},	{SDL_SCANCODE_F8,		0x0a},	{SDL_SCANCODE_F9,		0x01},	{SDL_SCANCODE_F10,		0x09},	{SDL_SCANCODE_F11,		0x78},
	{SDL_SCANCODE_F12,		0x07},	{SDL_SCANCODE_RSHIFT,		0x59},	{SDL_SCANCODE_LSHIFT,		0x12},	{SDL_SCANCODE_LCTRL,		0x14},	{SDL_SCANCODE_RCTRL,		0x114},
	{SDL_SCANCODE_LALT,		0x11},	{SDL_SCANCODE_RALT,		0x111},	{SDL_SCANCODE_NONUSBACKSLASH,	0x61},	{SDL_SCANCODE_KP_ENTER,		0x15a},	{SDL_SCANCODE_KP_0,		0x70},
	{SDL_SCANCODE_KP_1,		0x69},	{SDL_SCANCODE_KP_2,		0x72},	{SDL_SCANCODE_KP_3,		0x7a},	{SDL_SCANCODE_KP_4,		0x6b},	{SDL_SCANCODE_KP_5,		0x73},
	{SDL_SCANCODE_KP_6,		0x74},	{SDL_SCANCODE_KP_7,		0x6c},	{SDL_SCANCODE_KP_8,		0x75},	{SDL_SCANCODE_KP_9,		0x7d},	{SDL_SCANCODE_KP_PERIOD,	0x71},
	{SDL_SCANCODE_KP_PLUS,		0x79},	{SDL_SCANCODE_KP_MINUS,		0x7b},	{SDL_SCANCODE_KP_MULTIPLY,	0x7c},	{SDL_SCANCODE_KP_DIVIDE,	0x14a},	{0,				-1}
};

void clear_emu_events ( void )
{
	hid_reset_events(1);
}





static Uint8  ps2_stream[128];
static int    ps2_stream_w_pos = 0;
static Uint64 virt_cycle_last_read;


int read_ps2_port ( void )
{
	static int clk = 2;
	static int data = 1;
	if (ps2_stream_w_pos) {
		Uint64 since = all_virt_cycles - virt_cycle_last_read;
		// This is BAD, since actually the keyboard sends data the CPU takes care or not, it won't "pause" just because PS/2 lines are not checked ...
		if (since > 333) {
			virt_cycle_last_read = all_virt_cycles;
			//while (since > 333 && ps2_stream_w_pos) {
				clk ^= 2;
				if (!clk) {
					data = ps2_stream[0];
					memmove(ps2_stream, ps2_stream + 1, --ps2_stream_w_pos);
				}
			//	since -= 333;
			//}
		}
	} else {
		clk = 2;
		data = 1;
	}
	return clk | data;
}


static void queue_ps2_device_packet ( Uint8 data )
{
	//DEBUGPRINT("PS2: queuing protocol byte $%02X" NL, data);
	ps2_stream[ps2_stream_w_pos++] = 0;	// start bit, always zero
	int parity = 1;				// odd parity, we start with '1'.
	for (int a = 0; a < 8; a++, data >>=1 ) {
		ps2_stream[ps2_stream_w_pos++] = (data & 1);
		if ((data & 1))
			parity ^= 1;
	}
	ps2_stream[ps2_stream_w_pos++] = parity;
	ps2_stream[ps2_stream_w_pos++] = 1;	// stop bit, always one
}


static XEMU_INLINE void emit_ps2_event ( SDL_Scancode key, int pressed )
{
	const struct ps2_keymap_st *p = ps2_keymap;
	while (p->ps2_code >= 0)
		if (key == p->key) {
			if (ps2_stream_w_pos < sizeof(ps2_stream) - 12 * 3) {
				int ps2 = p->ps2_code;
				//fprintf(stderr, "FOUND KEY: SDL=%d PS2=%d PRESSED=%s\n", p->key, ps2, pressed ? "pressed" : "released");
				if (ps2 >= 0x100)
					queue_ps2_device_packet(0xE0);	// extended key, emit E0
				if (!pressed)
					queue_ps2_device_packet(0xF0);	// "break" code, ie, release of a key
				queue_ps2_device_packet(ps2 & 0xFF);
			} else
				DEBUGPRINT("PS2: protocol stream is full :-(" NL);
			return;
		} else
			p++;
	//DEBUGPRINT("PS2: KEY NOT FOUND SDL=%d PRESSED=%s" NL, key, pressed ? "pressed" : "released");
}


// HID needs this to be defined, it's up to the emulator if it uses or not ...
// NOTE: Commander X16 is a special case, as it's not matrix based with keyboard implementation.
// Thus, we skip the matrix stuff more or less, and use the callback of the HID subsystem to
// inject the emulated PS/2 sequences then. We want to use some queue anyway ...
int emu_callback_key ( int pos, SDL_Scancode key, int pressed, int handled )
{
/*	DEBUGPRINT("KEY EVENT: pos=%d scancode=%d pressed=%d handled=%d" NL,
			pos, key, pressed, handled
	);*/
	if (pos == -1 && handled == 0)
		emit_ps2_event(key, pressed);
	return 0;
}
