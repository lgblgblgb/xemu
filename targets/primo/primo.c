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

#include "xemu/emutools.h"
#include "xemu/emutools_hid.h"
#include "xemu/z80.h"
#include "primo.h"


#define CLOCKS_PER_FRAME (CPU_CLOCK / 50)

Z80EX_CONTEXT z80ex;

static Uint8 memory[0x10000];
static const Uint8 init_primo_palette_rgb[2 * 3] = {	// Primo is simple enough, b&w :) Primo C/colour Primo is another problem ...
	0x00, 0x00, 0x00,	// black
	0xFF, 0xFF, 0xFF	// white
};
static Uint32 primo_palette[2];
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
		// See the comments at the "matrix" definition below
		return (((~kbd_matrix[(port16 >> 3) & 7]) >> (port16 & 7)) & 1) | vsync;
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



#define VIRTUAL_SHIFT_POS	0x03


/* Primo for real does not have the notion if "keyboard matrix", well or we
   can say it has 1*64 matrix (not like eg C64 with 8*8). Since the current
   Xemu HID structure is more about a "real" matrix with "sane" dimensions,
   I didn't want to hack it over, instead we use a more-or-less artificial
   matrix, and we map that to the Primo I/O port request on port reading.
   Since, HID assumes the high nibble of the "position" is the "row" and
   low nibble can be only 0-7 we have values like:
   $00 - $07, $10 - $17, $20 - $27, ...
   ALSO: Primo uses bit '1' for pressed, so we also invert value in
   the port read function above.
*/
static const struct KeyMapping primo_key_map[] = {
	{ SDL_SCANCODE_Y,	0x00 },	// scan 0 Y
	{ SDL_SCANCODE_UP,	0x01 },	// scan 1 UP-ARROW
	{ SDL_SCANCODE_S,	0x02 },	// scan 2 S
	{ SDL_SCANCODE_LSHIFT,	0x03 },	{ SDL_SCANCODE_RSHIFT,  0x03 }, // scan 3 SHIFT
	{ SDL_SCANCODE_E,	0x04 },	// scan 4 E
	//{ SDL_SCANCODE_UPPER,	0x05 },	// scan 5 UPPER
	{ SDL_SCANCODE_W,	0x06 },	// scan 6 W
	{ SDL_SCANCODE_LCTRL,	0x07 },	// scan 7 CTR
	{ SDL_SCANCODE_D,	0x10 },	// scan 8 D
	{ SDL_SCANCODE_3,	0x11 },	// scan 9 3 #
	{ SDL_SCANCODE_X,	0x12 },	// scan 10 X
	{ SDL_SCANCODE_2,	0x13 },	// scan 11 2 "
	{ SDL_SCANCODE_Q,	0x14 },	// scan 12 Q
	{ SDL_SCANCODE_1,	0x15 },	// scan 13 1 !
	{ SDL_SCANCODE_A,	0x16 },	// scan 14 A
	{ SDL_SCANCODE_DOWN,	0x17 },	// scan 15 DOWN-ARROW
	{ SDL_SCANCODE_C,	0x20 },	// scan 16 C
	//{ SDL_SCANCODE_----,	0x21 },	// scan 17 ----
	{ SDL_SCANCODE_F,	0x22 },	// scan 18 F
	//{ SDL_SCANCODE_----,	0x23 },	// scan 19 ----
	{ SDL_SCANCODE_R,	0x24 },	// scan 20 R
	//{ SDL_SCANCODE_----,	0x25 },	// scan 21 ----
	{ SDL_SCANCODE_T,	0x26 },	// scan 22 T
	{ SDL_SCANCODE_7,	0x27 },	// scan 23 7 /
	{ SDL_SCANCODE_H,	0x30 },	// scan 24 H
	{ SDL_SCANCODE_SPACE,	0x31 },	// scan 25 SPACE
	{ SDL_SCANCODE_B,	0x32 },	// scan 26 B
	{ SDL_SCANCODE_6,	0x33 },	// scan 27 6 &
	{ SDL_SCANCODE_G,	0x34 },	// scan 28 G
	{ SDL_SCANCODE_5,	0x35 },	// scan 29 5 %
	{ SDL_SCANCODE_V,	0x36 },	// scan 30 V
	{ SDL_SCANCODE_4,	0x37 },	// scan 31 4 $
	{ SDL_SCANCODE_N,	0x40 },	// scan 32 N
	{ SDL_SCANCODE_8,	0x41 },	// scan 33 8 (
	{ SDL_SCANCODE_Z,	0x42 },	// scan 34 Z
	//{ SDL_SCANCODE_PLUS,	0x43 },	// scan 35 + ?
	{ SDL_SCANCODE_U,	0x44 },	// scan 36 U
	{ SDL_SCANCODE_0,	0x45 },	// scan 37 0
	{ SDL_SCANCODE_J,	0x46 },	// scan 38 J
	//{ SDL_SCANCODE_>,	0x47 },	// scan 39 > <
	{ SDL_SCANCODE_L,	0x50 },	// scan 40 L
	{ SDL_SCANCODE_MINUS,	0x51 },	// scan 41 - i
	{ SDL_SCANCODE_K,	0x52 },	// scan 42 K
	{ SDL_SCANCODE_PERIOD,	0x53 },	// scan 43 . :
	{ SDL_SCANCODE_M,	0x54 },	// scan 44 M
	{ SDL_SCANCODE_9,	0x55 },	// scan 45 9 ;
	{ SDL_SCANCODE_I,	0x56 },	// scan 46 I
	{ SDL_SCANCODE_COMMA,	0x57 },	// scan 47 ,
	//{ SDL_SCANCODE_U",	0x60 },	// scan 48 U"
	{ SDL_SCANCODE_APOSTROPHE,	0x61 },	// scan 49 ' #
	{ SDL_SCANCODE_P,	0x62 },	// scan 50 P
	//{ SDL_SCANCODE_u',	0x63 },	// scan 51 u' u"
	{ SDL_SCANCODE_O,	0x64 },	// scan 52 O
	{ SDL_SCANCODE_HOME,	0x65 },	// scan 53 CLS
	//{ SDL_SCANCODE_----,	0x66 },	// scan 54 ----
	{ SDL_SCANCODE_RETURN,	0x67 },	// scan 55 RETURN
	//{ SDL_SCANCODE_----,	0x70 },	// scan 56 ----
	{ SDL_SCANCODE_LEFT,	0x71 },	// scan 57 LEFT-ARROW
	//{ SDL_SCANCODE_E',	0x72 },	// scan 58 E'
	//{ SDL_SCANCODE_o',	0x73 },	// scan 59 o'
	//{ SDL_SCANCODE_A',	0x74 },	// scan 60 A'
	{ SDL_SCANCODE_RIGHT,	0x75 },	// scan 61 RIGHT-ARROW
	//{ SDL_SCANCODE_O:,	0x76 },	// scan 62 O:
	{ SDL_SCANCODE_ESCAPE,	0x77 },	// scan 63 BRK
	STD_XEMU_SPECIAL_KEYS,
	// **** this must be the last line: end of mapping table ****
	{ 0, -1 }
};



void clear_emu_events ( void )
{
	hid_reset_events(1);
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



// HID needs this to be defined, it's up to the emulator if it uses or not ...
int emu_callback_key ( int pos, SDL_Scancode key, int pressed, int handled )
{
        return 0;
}



static void update_emulator ( void )
{
	if (!frameskip) {
		render_primo_screen();
		hid_handle_all_sdl_events();
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
	hid_init(
		primo_key_map,
		VIRTUAL_SHIFT_POS,
		SDL_DISABLE		// no joystick HID events
	);
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
		if (cycles >= CLOCKS_PER_FRAME) {
			update_emulator();
			frameskip = !frameskip;
			cycles -= CLOCKS_PER_FRAME;
		}
	}
	return 0;
}
