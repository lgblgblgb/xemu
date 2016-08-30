/* Test-case for a very simple and inaccurate Primo (a Hungarian U880 - Z80 compatible - based
   8 bit computer) emulator using SDL2 library.
   Copyright (C)2016 LGB (Gábor Lénárt) <lgblgblgb@gmail.com>

   This Primo emulator is HIGHLY inaccurate and unusable.

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
#include "emutools.h"
#include "z80.h"
#include "primo.h"


static Uint8 memory[0x10000];
static const Uint8 init_primo_palette_rgb[2 * 3] = {	// Primo is simple enough, b&w :) Primo C/colour Primo is another problem ...
	0x00, 0x00, 0x00,	// black
	0xFF, 0xFF, 0xFF	// white
};
static Uint32 primo_palette[2];
static Uint64 keyboard_state;
static int frameskip = 0;
static int primo_screen = 0xC800;
static int enable_nmi = 0;
static int vsync = 0;

#define VSYNC_ON 32



Z80EX_BYTE z80ex_mread_cb ( Z80EX_WORD addr, int m1_state )
{
	return memory[addr];
}


void z80ex_mwrite_cb ( Z80EX_WORD addr, Z80EX_BYTE value )
{
	if (addr >= 0x4000)
		memory[addr] = value;
}


Z80EX_BYTE z80ex_pread_cb ( Z80EX_WORD port16 )
{
	if ((port16 & 0xFF) < 0x40) {
		return ((keyboard_state >> (port16 & 0x3F)) & 1) | vsync;
	} else
		return 0xFF;
}


void z80ex_pwrite_cb ( Z80EX_WORD port16, Z80EX_BYTE value )
{
	if ((port16 & 0xFF) < 0x40) {
		primo_screen = (value & 8) ? 0xE800 : 0xC800;
		enable_nmi = value & 128;
	}
}


Z80EX_BYTE z80ex_intread_cb ( void )
{
	return 0xFF;
}


void z80ex_reti_cb ( void )
{
}




struct KeyMapping {
	SDL_Scancode	scan;		// SDL scancode for the given key we want to map
	Uint8		pos;		// BCD packed, high nibble / low nibble for col/row to map to.  0xFF means end of table!, high bit set on low nibble: press virtual shift as well!
};
static const struct KeyMapping key_map[] = {
	{ SDL_SCANCODE_1,		0x00 }, // 1
	{ SDL_SCANCODE_3,		0x01 }, // 3
	{ SDL_SCANCODE_5,		0x02 }, // 5
	{ SDL_SCANCODE_7,		0x03 }, // 7
	{ SDL_SCANCODE_9,		0x04 }, // 9
	//{ SDL_SCANCODE_+		0x05 },	// PLUS
	//{ SDL_SCANCODE_font		0x06 },	// FONT
	{ SDL_SCANCODE_BACKSPACE,	0x07 },	// DEL
	//{ SDL_SCANCODE_// UNKNOWN KEY?			0x10	//
	{ SDL_SCANCODE_W,		0x11 }, // W
	{ SDL_SCANCODE_R,		0x12 }, // R
	{ SDL_SCANCODE_Y,		0x13 }, // Y
	{ SDL_SCANCODE_I,		0x14 }, // I
	{ SDL_SCANCODE_P,		0x15 }, // P
	//{ SDL_SCANCODE_STAR // *
	{ SDL_SCANCODE_RETURN,		0x17 }, // RETURN
	{ SDL_SCANCODE_LCTRL,		0x20 }, // CTRL, only the left ctrl is mapped as vic-20 ctrl ...
	{ SDL_SCANCODE_A,		0x21 }, // A
	{ SDL_SCANCODE_D,		0x22 }, // D
	{ SDL_SCANCODE_G,		0x23 }, // G
	{ SDL_SCANCODE_J,		0x24 }, // J
	{ SDL_SCANCODE_L,		0x25 }, // L
	{ SDL_SCANCODE_SEMICOLON,	0x26 }, // ;
	{ SDL_SCANCODE_RIGHT, 0x27 }, { SDL_SCANCODE_LEFT, 0x27 | 8 },	// CURSOR RIGHT, _SHIFTED_: CURSOR LEFT!
	{ SDL_SCANCODE_END,		0x30 }, // RUN/STOP !! we use PC key 'END' for this!
	{ SDL_SCANCODE_LSHIFT,		0x31 }, // LEFT SHIFT
	{ SDL_SCANCODE_X,		0x32 }, // X
	{ SDL_SCANCODE_V,		0x33 }, // V
	{ SDL_SCANCODE_N,		0x34 }, // N
	{ SDL_SCANCODE_COMMA,		0x35 }, // ,
	{ SDL_SCANCODE_SLASH,		0x36 }, // /
	{ SDL_SCANCODE_DOWN, 0x37 }, { SDL_SCANCODE_UP, 0x37 | 8 }, // CURSOR DOWN, _SHIFTED_: CURSOR UP!
	{ SDL_SCANCODE_SPACE,		0x40 }, // SPACE
	{ SDL_SCANCODE_Z,		0x41 }, // Z
	{ SDL_SCANCODE_C,		0x42 }, // C
	{ SDL_SCANCODE_B,		0x43 }, // B
	{ SDL_SCANCODE_M,		0x44 }, // M
	{ SDL_SCANCODE_PERIOD,		0x45 }, // .
	{ SDL_SCANCODE_RSHIFT,		0x46 }, // RIGHT SHIFT
	{ SDL_SCANCODE_F1, 0x47 }, { SDL_SCANCODE_F2, 0x47 | 8 }, // F1, _SHIFTED_: F2!
	{ SDL_SCANCODE_LALT, 0x50 }, { SDL_SCANCODE_RALT, 0x50 }, // COMMODORE (may fav key!), PC sucks, no C= key :) - we map left and right ALT here ...
	{ SDL_SCANCODE_S,		0x51 }, // S
	{ SDL_SCANCODE_F,		0x52 }, // F
	{ SDL_SCANCODE_H,		0x53 }, // H
	{ SDL_SCANCODE_K,		0x54 }, // K
	{ SDL_SCANCODE_APOSTROPHE,	0x55 },	// :    we map apostrophe here
	{ SDL_SCANCODE_EQUALS,		0x56 }, // =
	{ SDL_SCANCODE_F3, 0x57 }, { SDL_SCANCODE_F4, 0x57 | 8 }, // F3, _SHIFTED_: F4!
	{ SDL_SCANCODE_Q,		0x60 }, // Q
	{ SDL_SCANCODE_E,		0x61 }, // E
	{ SDL_SCANCODE_T,		0x62 }, // T
	{ SDL_SCANCODE_U,		0x63 }, // U
	{ SDL_SCANCODE_O,		0x64 }, // O
	//{ SDL_SCANCODE_// @
	// UNKNOWN KEY?!?! 0x66
	{ SDL_SCANCODE_F5, 0x67 }, { SDL_SCANCODE_F6, 0x67 | 8 }, // F5, _SHIFTED_: F6!
	{ SDL_SCANCODE_2,		0x70 }, // 2
	{ SDL_SCANCODE_4,		0x71 }, // 4
	{ SDL_SCANCODE_6,		0x72 }, // 6
	{ SDL_SCANCODE_8,		0x73 }, // 8
	{ SDL_SCANCODE_0,		0x74 }, // 0
	{ SDL_SCANCODE_MINUS,		0x75 }, // -
	{ SDL_SCANCODE_HOME,		0x76 }, // HOME
	{ SDL_SCANCODE_F7, 0x77 }, { SDL_SCANCODE_F8, 0x77 | 8 }, // F7, _SHIFTED_: F8!
	// -- the following key definitions are not really part of the original VIC20 kbd matrix, we just *emulate* things this way!!
	// Note: the exact "virtual" kbd matrix positions are *important* and won't work otherwise (arranged to be used more positions with one bit mask and, etc).
	{ SDL_SCANCODE_KP_5,		0x85 },	// for joy FIRE  we map PC num keypad 5
	{ SDL_SCANCODE_KP_0,		0x85 },	// PC num keypad 0 is also the FIRE ...
	{ SDL_SCANCODE_RCTRL,		0x85 }, // and RIGHT controll is also the FIRE ... to make Sven happy :)
	{ SDL_SCANCODE_KP_8,		0x82 },	// for joy UP    we map PC num keypad 8
	{ SDL_SCANCODE_KP_2,		0x83 },	// for joy DOWN  we map PC num keypad 2
	{ SDL_SCANCODE_KP_4,		0x84 },	// for joy LEFT  we map PC num keypad 4
	{ SDL_SCANCODE_KP_6,		0x87 },	// for joy RIGHT we map PC num keypad 6
	{ SDL_SCANCODE_ESCAPE,		0x81 },	// RESTORE key
	// **** this must be the last line: end of mapping table ****
	{ 0, 0xFF }
};



void clear_emu_events ( void )
{
	keyboard_state = 0;
}



// pressed: non zero value = key is pressed, zero value = key is released
static void emulate_keyboard ( SDL_Scancode key, int pressed )
{
	if (key == SDL_SCANCODE_F11) {	// toggle full screen mode on/off
		if (pressed)
			emu_set_full_screen(-1);
	} else if (key == SDL_SCANCODE_F9) {	// exit emulator ...
		if (pressed)
			exit(0);
	} else {
		const struct KeyMapping *map = key_map;
		while (map->pos != 0xFF) {
			if (map->scan == key) {
				if (pressed)
					keyboard_state |= 1 << (map->pos & 0x3F);
				else
					keyboard_state &= ~(1 << (map->pos & 0x3F));
				//if (map->pos & 8)		// shifted key emu?
				//	KBD_SET_KEY(0x31, pressed);	// maintain the shift key on VIC20!
				//KBD_SET_KEY(map->pos, pressed);
				break;	// key found, end.
			}
			map++;
		}
	}
}



static inline void render_primo_screen ( void )
{
	int tail, y;
	Uint32 *pix = emu_start_pixel_buffer_access(&tail);
	Uint8 *scr = memory + primo_screen;
	for (y = 0; y < 192; y++) {
		int x;
		for (x = 0; x < 32; x++) {
			Uint8 b = *(scr++);
			*(pix++) = primo_palette[(b >> 7) & 1];
			*(pix++) = primo_palette[(b >> 6) & 1];
			*(pix++) = primo_palette[(b >> 5) & 1];
			*(pix++) = primo_palette[(b >> 4) & 1];
			*(pix++) = primo_palette[(b >> 3) & 1];
			*(pix++) = primo_palette[(b >> 2) & 1];
			*(pix++) = primo_palette[(b >> 1) & 1];
			*(pix++) = primo_palette[b & 1];
		}
		pix += tail;
	}
	emu_update_screen();
}



static void update_emulator ( void )
{
	if (!frameskip) {
		SDL_Event e;
		render_primo_screen();
		while (SDL_PollEvent(&e) != 0) {
			switch (e.type) {
				case SDL_QUIT:		// ie: someone closes the SDL window ...
					exit(0);
					break;
				/* --- keyboard events --- */
				case SDL_KEYDOWN:	// key is pressed (down)
				case SDL_KEYUP:		// key is released (up)
					// make sure that key event is for our window, also that it's not a releated event by long key presses (repeats should be handled by the emulated machine's KERNAL)
					if (e.key.repeat == 0 && (e.key.windowID == sdl_winid || e.key.windowID == 0))
						emulate_keyboard(e.key.keysym.scancode, e.key.state == SDL_PRESSED);	// the last argument will be zero in case of release, other val in case of pressing
					break;
			}
		}
		emu_timekeeping_delay(40000);
	}
}



int main ( int argc, char **argv )
{
	int cycles;
	printf("**** The Unknown Primo emulator from LGB" NL
	"INFO: Texture resolution is %dx%d" NL "%s" NL,
		SCREEN_WIDTH, SCREEN_HEIGHT,
		emulators_disclaimer
	);
	/* Initiailize SDL - note, it must be before loading ROMs, as it depends on path info from SDL! */
	if (emu_init_sdl(
		TARGET_DESC APP_DESC_APPEND,	// window title
		APP_ORG, TARGET_NAME,		// app organization and name, used with SDL pref dir formation
		1,				// resizable window
		SCREEN_WIDTH, SCREEN_HEIGHT,	// texture sizes
		SCREEN_WIDTH, SCREEN_HEIGHT,	// logical size (width is doubled for somewhat correct aspect ratio)
		SCREEN_WIDTH * 3, SCREEN_HEIGHT * 3,	// window size (tripled in size, original would be too small)
		SCREEN_FORMAT,		// pixel format
		2,			// we have 2 colours
		init_primo_palette_rgb,	// initialize palette from this constant array
		primo_palette,		// initialize palette into this stuff
		RENDER_SCALE_QUALITY,	// render scaling quality
		USE_LOCKED_TEXTURE,	// 1 = locked texture access
		NULL			// no emulator specific shutdown function
	))
		return 1;
	/* Intialize memory and load ROMs */
	memset(memory, 0xFF, sizeof memory);
	if (emu_load_file(ROM_NAME, memory, 0x4001) != 0x4000)
		FATAL("Cannot load ROM: %s", ROM_NAME);
	// Continue with initializing ...
	clear_emu_events();	// also resets the keyboard
	z80ex_init();
	cycles = 0;
	emu_timekeeping_start();	// we must call this once, right before the start of the emulation
	for (;;) { // our emulation loop ...
		cycles += z80ex_step();
		if (cycles >= CPU_CLOCK / 50) {
			update_emulator();
			frameskip = !frameskip;
			cycles -= CPU_CLOCK / 50;
		}
	}
	return 0;
}
