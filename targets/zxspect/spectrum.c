/* Learning material to get to know ZX Spectrum (I never had that machine ...)
   by trying to write an emulator :-O
   Part of the Xemu project, please visit: https://github.com/lgblgblgb/xemu
   Copyright (C)2017 LGB (Gábor Lénárt) <lgblgblgb@gmail.com>

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
#include "xemu/emutools_config.h"
#include "xemu/z80.h"
#include "spectrum.h"



Z80EX_CONTEXT z80ex;

static Uint8 memory[0x10000];
static const Uint8 init_ula_palette_rgb[16 * 3] = {
	0x00, 0x00, 0x00,	// 0: black
	0x00, 0x00, 0xD7,	// 1: blue
	0xD7, 0x00, 0x00,	// 2: red
	0xD7, 0x00, 0xD7,	// 3: magenta
	0x00, 0xD7, 0x00,	// 4: green
	0x00, 0xD7, 0xD7,	// 5: cyan
	0xD7, 0xD7, 0x00,	// 6: yellow
	0xD7, 0xD7, 0xD7,	// 7: white
	0x00, 0x00, 0x00,	// 8: "bright black" ~ same as "black"
	0x00, 0x00, 0xFF,	// 9: bright blue
	0xFF, 0x00, 0x00,	// A: bright red
	0xFF, 0x00, 0xFF,	// B: bright magenta
	0x00, 0xFF, 0x00,	// C: bright green
	0x00, 0xFF, 0xFF,	// D: bright cyan
	0xFF, 0xFF, 0x00,	// E: bright yellow
	0xFF, 0xFF, 0xFF	// F: bright white
};
static Uint32 ula_palette[16];
static Uint32 ulaplus_palette[256];
static int vsync_int;

static int contended_memory_t_states;

static int tstate_x;
static int scanline;
static Uint8 *pix_mem_p[192];
static Uint8 *atr_mem_p[192];
static unsigned int frame_counter;
static int frame_done;
static int flash_state;
static Uint32 paper_colours[256], ink_colours[256], border_colour;
static Uint32 ulaplus_colours[64];
static Uint8 ulaplus_ink_index[256], ulaplus_paper_index[256];

static Uint32 *pix;



static void init_ula_tables ( void )
{
	int a;
	// Look-up tables with memory pointers for each of the 192 visible lines for both of pixel and attribute mode
	// So we can use an indexed value to get the pointer and increment it through one line, then load the next etc ...
	for (a = 0; a < 192; a++) {
		pix_mem_p[a] = memory + 0x4000 + (((((a >> 3) & 0x18) | (a & 7)) << 8) | (((a >> 3) & 7) << 5));
		atr_mem_p[a] = memory + 0x5800 + ((a >> 3) << 5);
	}
	// For standard ULA mode: Look-up tables for quicker paper/ink decoding, based on the attribute byte as index
	// (with AND'ing the FLASH state, changed in every 16 frames)
	// So first 128 entries are the normal, the next 128 are the swapped ink/paper.
	for (a = 0; a < 128; a++ ) {
		paper_colours[a] = ink_colours  [a + 128] = ula_palette[(a & 120) >> 3];
		ink_colours  [a] = paper_colours[a + 128] = ula_palette[(a &   7) | ((a & 64) >> 3)];
	}
	// ULAplus palette
	// I'm not so much familiar even with that ULA, but I try to follow this here for ULAplus:
	// http://faqwiki.zxnet.co.uk/wiki/ULAplus
	// ULAplus part is not used currently at all ...
	for (a = 0; a < 256; a++) {
		int red   = (a >> 2) & 7;
		int green = (a >> 5) & 7;
		int blue  = ((a & 3) << 1) | ((a & 3) ? 1 : 0);
		ulaplus_palette[a] = SDL_MapRGBA(
			sdl_pix_fmt,
			(red   << 5) | (red   << 2) | (red   >> 1),
			(green << 5) | (green << 2) | (green >> 1),
			(blue  << 5) | (blue  << 2) | (blue  >> 1),
			0xFF
		);
		ulaplus_ink_index[a]   = ((a & 0xC0) >> 2) + (a & 7);
		ulaplus_paper_index[a] = ((a & 0xC0) >> 2) + ((a >> 3) & 7) + 8;
		ulaplus_colours[a & 63] = ulaplus_palette[0];	// all black initial state
	}
}


// Z80ex is instructed to call this function at every T-states
// we use it then to emulate ULA functions: rendering screen
// at memory read/write (and I/O) we can use inserting extra T-states
// for contended memory emulation, which ends here too, so everything seems to be OK
void z80ex_tstate_cb ( void )
{
	if (scanline >= 16) {
		if (tstate_x < 128) {			// visible area (if in the range vertically, I mean ...)
			if (scanline >= 64 && scanline < 64 + 192) {
				Uint8 pd = *(pix_mem_p[scanline - 64] + (tstate_x >> 2));
				Uint8 ad = *(atr_mem_p[scanline - 64] + (tstate_x >> 2));
				Uint32 fg = ink_colours[ad & flash_state];
				Uint32 bg = paper_colours[ad & flash_state];
				switch (tstate_x & 3) {
					case 0:
						*pix++ = pd & 0x80 ? fg : bg;
						*pix++ = pd & 0x40 ? fg : bg;
						break;
					case 1:
						*pix++ = pd & 0x20 ? fg : bg;
						*pix++ = pd & 0x10 ? fg : bg;
						break;
					case 2:
						*pix++ = pd & 0x08 ? fg : bg;
						*pix++ = pd & 0x04 ? fg : bg;
						break;
					case 3:
						*pix++ = pd & 0x02 ? fg : bg;
						*pix++ = pd & 0x01 ? fg : bg;
						break;
				}
			} else {
				*pix++ = border_colour;
				*pix++ = border_colour;
			}
		} else if (tstate_x < 128 + 24) {	// right border
			*pix++ = border_colour;
			*pix++ = border_colour;
		} else if (tstate_x < 128 + 24 + 48) {	// HSYNC
		} else {				// left border
			*pix++ = border_colour;
			*pix++ = border_colour;
		}
	}
	// Next tstate in this line, or next line, or next frame ...
	if (XEMU_UNLIKELY(tstate_x == 223)) {
		if (XEMU_UNLIKELY(scanline == 311)) {
			vsync_int = 1;
			frame_counter++;
			flash_state = (frame_counter & 16) ? 0xFF : 0x7F;
			frame_done = 1;	// signal main loop that we're ready. We use this, as it's problematic to update emu in-opcode maybe (eg: RESET function in HID, etc called from here ...)
			scanline = 0;
		} else
			scanline++;
		tstate_x = 0;
	} else
		tstate_x++;
}


Z80EX_BYTE z80ex_mread_cb ( Z80EX_WORD addr, int m1_state )
{
	if (contended_memory_t_states & ((addr & 0xC000) == 0x4000)) // contended memory area
		z80ex_w_states(contended_memory_t_states);
	return memory[addr];
}


void z80ex_mwrite_cb ( Z80EX_WORD addr, Z80EX_BYTE value )
{
	if (XEMU_UNLIKELY(addr < 0x4000))
		return;		// ROM is not writable
	if (addr >= 0x8000)
		memory[addr] = value;	// no contended memory, simply do the write
	else {
		// contended memory area!
		if (contended_memory_t_states)
			z80ex_w_states(contended_memory_t_states);
		memory[addr] = value;
	}
}


Z80EX_BYTE z80ex_pread_cb ( Z80EX_WORD port16 )
{
	if (XEMU_UNLIKELY(port16 & 1))
		return 0xFF;
	// The ULA port: every even addresses ...
	return (
		((port16 & 0x0100) ? 0xFF : kbd_matrix[0]) &
		((port16 & 0x0200) ? 0xFF : kbd_matrix[1]) &
		((port16 & 0x0400) ? 0xFF : kbd_matrix[2]) &
		((port16 & 0x0800) ? 0xFF : kbd_matrix[3]) &
		((port16 & 0x1000) ? 0xFF : kbd_matrix[4]) &
		((port16 & 0x2000) ? 0xFF : kbd_matrix[5]) &
		((port16 & 0x4000) ? 0xFF : kbd_matrix[6]) &
		((port16 & 0x8000) ? 0xFF : kbd_matrix[7])
	) | 224;
}


void z80ex_pwrite_cb ( Z80EX_WORD port16, Z80EX_BYTE value )
{
	if (XEMU_UNLIKELY(port16 & 1))
		return;
	// The ULA port: every even addresses ...
	border_colour = ula_palette[value & 7];
}


Z80EX_BYTE z80ex_intread_cb ( void )
{
	return 0xFF;
}


void z80ex_reti_cb ( void )
{
}



#define VIRTUAL_SHIFT_POS	0x00


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
static const struct KeyMapping speccy_key_map[] = {
	{ SDL_SCANCODE_LSHIFT,	0x00 }, { SDL_SCANCODE_RSHIFT,	0x00},
	{ SDL_SCANCODE_Z,	0x01 },
	{ SDL_SCANCODE_X,	0x02 },
	{ SDL_SCANCODE_C,	0x03 },
	{ SDL_SCANCODE_V,	0x04 },
	{ SDL_SCANCODE_A,	0x10 },
	{ SDL_SCANCODE_S,	0x11 },
	{ SDL_SCANCODE_D,	0x12 },
	{ SDL_SCANCODE_F,	0x13 },
	{ SDL_SCANCODE_G,	0x14 },
	{ SDL_SCANCODE_Q,	0x20 },
	{ SDL_SCANCODE_W,	0x21 },
	{ SDL_SCANCODE_E,	0x22 },
	{ SDL_SCANCODE_R,	0x23 },
	{ SDL_SCANCODE_T,	0x24 },
	{ SDL_SCANCODE_1,	0x30 },
	{ SDL_SCANCODE_2,	0x31 },
	{ SDL_SCANCODE_3,	0x32 },
	{ SDL_SCANCODE_4,	0x33 },
	{ SDL_SCANCODE_5,	0x34 },
	{ SDL_SCANCODE_0,	0x40 },
	{ SDL_SCANCODE_9,	0x41 },
	{ SDL_SCANCODE_8,	0x42 },
	{ SDL_SCANCODE_7,	0x43 },
	{ SDL_SCANCODE_6,	0x44 },
	{ SDL_SCANCODE_P,	0x50 },
	{ SDL_SCANCODE_O,	0x51 },
	{ SDL_SCANCODE_I,	0x52 },
	{ SDL_SCANCODE_U,	0x53 },
	{ SDL_SCANCODE_Y,	0x54 },
	{ SDL_SCANCODE_RETURN,	0x60 },
	{ SDL_SCANCODE_L,	0x61 },
	{ SDL_SCANCODE_K,	0x62 },
	{ SDL_SCANCODE_J,	0x63 },
	{ SDL_SCANCODE_H,	0x64 },
	{ SDL_SCANCODE_SPACE,	0x70 },
	{ SDL_SCANCODE_LCTRL,	0x71 },		// SYM.SHIFT
	{ SDL_SCANCODE_M,	0x72 },
	{ SDL_SCANCODE_N,	0x73 },
	{ SDL_SCANCODE_B,	0x74 },
	STD_XEMU_SPECIAL_KEYS,
	// **** this must be the last line: end of mapping table ****
	{ 0, -1 }
};



void clear_emu_events ( void )
{
	hid_reset_events(1);
}



// HID needs this to be defined, it's up to the emulator if it uses or not ...
int emu_callback_key ( int pos, SDL_Scancode key, int pressed, int handled )
{
        return 0;
}



static void open_new_frame ( void )
{
	int tail;
	frame_done = 0;
	pix = xemu_start_pixel_buffer_access(&tail);
	if (XEMU_UNLIKELY(tail))
		FATAL("FATAL: Xemu texture tail is not zero, but %d", tail);
}




int main ( int argc, char **argv )
{
	xemu_pre_init(APP_ORG, TARGET_NAME, "The learner's ZX Spectrum emulator from LGB (the learner)");
	xemucfg_define_str_option("rom", ROM_NAME, "Path and filename for ROM to be loaded");
	xemucfg_define_switch_option("syscon", "Keep system console open (Windows-specific effect only)");
	if (xemucfg_parse_all(argc, argv))
		return 1;
	/* Initiailize SDL - note, it must be before loading ROMs, as it depends on path info from SDL! */
	if (xemu_post_init(
		TARGET_DESC APP_DESC_APPEND,	// window title
		1,				// resizable window
		SCREEN_WIDTH, SCREEN_HEIGHT,	// texture sizes
		SCREEN_WIDTH, SCREEN_HEIGHT,	// logical sizes
		SCREEN_WIDTH * 3, SCREEN_HEIGHT * 3,	// window size (tripled in size, original would be too small)
		SCREEN_FORMAT,			// pixel format
		16,				// we have 16 colours
		init_ula_palette_rgb,		// initialize palette from this constant array
		ula_palette,			// initialize palette into this stuff
		RENDER_SCALE_QUALITY,		// render scaling quality
		USE_LOCKED_TEXTURE,		// 1 = locked texture access
		NULL				// no emulator specific shutdown function
	))
		return 1;
	hid_init(
		speccy_key_map,
		VIRTUAL_SHIFT_POS,
		SDL_DISABLE		// no joystick HID events
	);
	init_ula_tables();
	/* Intialize memory and load ROMs */
	memset(memory, 0xFF, sizeof memory);
	if (xemu_load_file(xemucfg_get_str("rom"), memory, 0x4000, 0x4000, "Selected ROM image cannot be loaded. Without it, Xemu won't work.\nPlease install it, or use the CLI switch -rom to specify one.") < 0)
		return 1;
	// Continue with initializing ...
	clear_emu_events();	// also resets the keyboard
	z80ex_init();
	vsync_int = 0;
	contended_memory_t_states = 0;
	tstate_x = 0;
	scanline = 0;
	flash_state = 0xFF;
	frame_counter = 0;
	if (!xemucfg_get_bool("syscon"))
		sysconsole_close(NULL);
	open_new_frame();		// open new frame at the very first time, the rest of frames handled below, during the emulation
	xemu_timekeeping_start();	// we must call this once, right before the start of the emulation
	for (;;) { // our emulation loop ...
		if (XEMU_UNLIKELY(vsync_int)) {
			if (z80ex_int())
				vsync_int = 0;
			else
				z80ex_step();
		} else
			z80ex_step();
		if (XEMU_UNLIKELY(frame_done)) {
			if (frame_counter & 1)	// currently Xemu framework assumes about ~25Hz for calling this, so we do this at every second frames
				hid_handle_all_sdl_events();
			xemu_update_screen();		// updates screen (Xemu framework), also closes the access to the buffer
			xemu_timekeeping_delay(19968);	// FIXME: better accuracy, as some T-states over it is now ....
			open_new_frame();		// open new frame
		}
	}
	return 0;
}
