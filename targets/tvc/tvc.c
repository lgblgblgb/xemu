/* Test-case for a very simple and inaccurate Videoton TV computer
   (a Z80 based 8 bit computer) emulator using SDL2 library.

   Copyright (C)2016 LGB (Gábor Lénárt) <lgblgblgb@gmail.com>

   This emulator is HIGHLY inaccurate and unusable.

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
#include "tvc.h"


#define CLOCKS_PER_FRAME (CPU_CLOCK / 50)

Z80EX_CONTEXT z80ex;

static Uint8 io_port_values[0x100];
static Uint8 *pagedir_rd[8], *pagedir_wr[8];
static Uint8 *crtc_p;			// points to the 16K video memory in use for CRTC
static int   colour_mode;
static int   keyboard_row;
static int   crtc_regsel;
static Uint8 crtc_registers[18];
static int   interrupt_active;

static struct {
	Uint8 drain    [0x02000];	// "drain", ROM writing points here, but this memory area is never read!
	Uint8 empty    [0x02000];
	Uint8 user_ram [0x10000];
	Uint8 video_ram[0x10000];
	Uint8 sys_rom  [0x04000 + 1];
	Uint8 cart_rom [0x04000 + 1];
	Uint8 ext_rom  [0x02000 + 1];
	Uint8 cst       [4][0x02000];
} mem;


static Uint32 tvc_palette_rgb[16];
static Uint32 tvc_palette_bw [16];
static Uint32 palette[4];
static Uint32 palette_col16_pixel1[0x100];
static Uint32 palette_col16_pixel2[0x100];
static Uint32 border_colour;

static int frameskip = 0;



static INLINE Uint32 TVC_COLOUR_BYTE_TO_SDL ( Uint8 c )
{
	return tvc_palette_rgb[(c & 1) | ((c >> 1) & 2) | ((c >> 2) & 4) | ((c >> 3) & 8)];
}


static void crtc_write_register ( int reg, Uint8 data )
{
	if (reg < 12) {
		DEBUGPRINT("CRTC: register %02Xh is written with data %02Xh" NL, reg, data);
		crtc_registers[reg] = data;
	}
}


// Well, the whole point of pagedir_XX stuff, that we have so simple
// z80 emu callbacks for read/write to be fast.
// Note: however then special cases are not supported, like clock streching on VRAM access
// ROM cannot be overwritten, since then pagedir_wr[...] selects the "mem.drain" as destinaton pointer ...
Z80EX_BYTE z80ex_mread_cb ( Z80EX_WORD addr, int m1_state )
{
	return *(pagedir_rd[addr >> 13] + addr);
}
void z80ex_mwrite_cb ( Z80EX_WORD addr, Z80EX_BYTE value )
{
	*(pagedir_wr[addr >> 13] + addr) = value;
}


Z80EX_BYTE z80ex_pread_cb ( Z80EX_WORD port16 )
{
	port16 &= 0xFF;
	DEBUGPRINT("IO: reading I/O port %02Xh" NL, port16);
	switch (port16) {
		case 0x58:
			DEBUGPRINT("Reading keyboard!" NL);
			return keyboard_row < 10 ? kbd_matrix[keyboard_row] : 0xFF;
		case 0x59:
			return interrupt_active ? 0xEF: 0xFF;
	}
	return 0xFF;
}


void z80ex_pwrite_cb ( Z80EX_WORD port16, Z80EX_BYTE value )
{
	port16 &= 0xFF;
	io_port_values[port16] = value;
	if (port16 != 2)	// to avoid flooding ...
		DEBUGPRINT("IO: writing I/O port %02Xh with data %02Xh" NL, port16, value);
	switch (port16) {
		case 0x00:
			border_colour = TVC_COLOUR_BYTE_TO_SDL(value >> 1);
			break;
		case 0x02:
			// page 3
			switch (value >> 6) {
				case 0:
					pagedir_rd[6] = mem.cart_rom - 0xC000;
					pagedir_rd[7] = mem.cart_rom - 0xC000;
					pagedir_wr[6] = mem.drain    - 0xC000;
					pagedir_wr[7] = mem.drain    - 0xE000;
					break;
				case 1:
					pagedir_rd[6] = mem.sys_rom  - 0xC000;
					pagedir_rd[7] = mem.sys_rom  - 0xC000;
					pagedir_wr[6] = mem.drain    - 0xC000;
					pagedir_wr[7] = mem.drain    - 0xE000;
					break;
				case 2:
					pagedir_rd[6] = pagedir_rd[7] = pagedir_wr[6] = pagedir_wr[7] = mem.user_ram;
					break;
				case 3:
					pagedir_rd[6] = mem.empty    - 0xC000;	// would be cst stuffs can be paged with port 3
					pagedir_rd[7] = mem.ext_rom  - 0xE000;
					pagedir_wr[6] = mem.drain    - 0xC000;	// would be cst stuffs can be paged with port 3
					pagedir_wr[7] = mem.drain    - 0xE000;
					break;
			}
			// page 2
			pagedir_rd[4] = pagedir_rd[5] = pagedir_wr[4] = pagedir_wr[5] = (
				(value & 32) 	?
				(mem.user_ram )	:
				(mem.video_ram + ((io_port_values[0xF] & 0xC) << 12) - 0x8000)
			);
			// page 0
			switch ((value >> 3) & 3) {
				case 0:
					pagedir_rd[0] = mem.sys_rom ;
					pagedir_rd[1] = mem.sys_rom ;
					pagedir_wr[0] = mem.drain   - 0x0000;
					pagedir_wr[1] = mem.drain   - 0x2000;
					break;
				case 1:
					pagedir_rd[0] = mem.cart_rom ;
					pagedir_rd[1] = mem.cart_rom ;
					pagedir_wr[0] = mem.drain    - 0x0000;
					pagedir_wr[1] = mem.drain    - 0x2000;
					break;
				case 2:
					pagedir_rd[0] = pagedir_rd[1] = pagedir_wr[0] = pagedir_wr[1] = mem.user_ram;
					break;
				case 3:
					pagedir_rd[0] = pagedir_rd[1] = pagedir_wr[0] = pagedir_wr[1] = mem.user_ram + 0xC000;
					break;
			}
			// page 1
			pagedir_rd[2] = pagedir_rd[3] = pagedir_wr[2] = pagedir_wr[3] = (
				(!(value & 4))	?
				(mem.user_ram )	:
				(mem.video_ram + ((io_port_values[0xF] & 3) << 14) - 0x4000)
			);
			break;
		case 0x03:
			// z80ex_pwrite_cb(2, io_port_values[2]); // TODO: we need this later with expansion mem paging!
			keyboard_row = value & 0xF;	// however the lower 4 bits are for selecting row
			break;
		case 0x05:
			DEBUGPRINT("Enabled_SoundIT=%d Enabled_CursorIT=%d" NL, value & 32 ? 1 : 0, value & 16 ? 1 : 0);
			break;
		case 0x06:
			colour_mode = value & 3;
			if (colour_mode == 3)
				colour_mode = 2;
			DEBUGPRINT("VIDEO: colour mode is %d" NL, colour_mode);
			break;
		case 0x07:
			// clear cursor/sound IT. FIXME: any write would do it?!
			interrupt_active = 0;
			break;
		case 0x0C: case 0x0D: case 0x0E: case 0x0F:
			crtc_p = mem.video_ram + ((value & 0x30) << 10);
			z80ex_pwrite_cb(2, io_port_values[2]);	// TODO: can be optimized to only call, if VID page is paged in by port 2 ...
			break;
		case 0x60: case 0x61: case 0x62: case 0x63: case 0x64: case 0x65: case 0x66: case 0x67:
		case 0x68: case 0x69: case 0x6A: case 0x6B: case 0x6C: case 0x6D: case 0x6E: case 0x6F:
			palette[port16 & 3] = TVC_COLOUR_BYTE_TO_SDL(value);
			break;
		case 0x70: case 0x72: case 0x74: case 0x76: case 0x78: case 0x7A: case 0x7C: case 0x7E:
			crtc_regsel = value;
			break;
		case 0x71: case 0x73: case 0x75: case 0x77: case 0x79: case 0x7B: case 0x7D: case 0x7F:
			crtc_write_register(crtc_regsel, value);
			break;
	}
}


Z80EX_BYTE z80ex_intread_cb ( void )
{
	return 0xFF;
}


void z80ex_reti_cb ( void )
{
}


#define VIRTUAL_SHIFT_POS	0x63


static const struct KeyMapping tvc_key_map[] = {
	// Row 0
	{ SDL_SCANCODE_5,	0x00 },	// 5
	{ SDL_SCANCODE_3,	0x01 },	// 3
	{ SDL_SCANCODE_2,	0x02 }, // 2
	{ SDL_SCANCODE_GRAVE,	0x03 }, // 0	we relocate 0, to match the "imagined" Hungarian keyboard layout ...
	{ SDL_SCANCODE_6,	0x04 }, // 6
	{ 100,			0x05 },	// í	this is hard one, the scancode "100" is ISO keyboard, not ANSI and has different meaning on platforms ...
	{ SDL_SCANCODE_1,	0x06 }, // 1
	{ SDL_SCANCODE_4,	0x07 },	// 4
	// Row 1
	{ -1,			0x10 },	// ^	TODO!
	{ SDL_SCANCODE_8,	0x11 },	// 8
	{ SDL_SCANCODE_9,	0x12 },	// 9
	{ SDL_SCANCODE_MINUS,	0x13 },	// ü	Position on HUN kbd
	{ -1,			0x14 },	// *	TODO!
	{ SDL_SCANCODE_EQUALS,	0x15 },	// ó	Position on HUN kbd
	{ SDL_SCANCODE_0,	0x16 },	// ö	Position on HUN kbd
	{ SDL_SCANCODE_7,	0x17 },	// 7
	// Row 2
	{ SDL_SCANCODE_T,	0x20 },	// t
	{ SDL_SCANCODE_E,	0x21 },	// e
	{ SDL_SCANCODE_W,	0x22 },	// w
	{ -1,			0x23 },	// ;	TODO!
	{ SDL_SCANCODE_Z,	0x24 },	// z
	{ -1,			0x25 },	// @	TODO!
	{ SDL_SCANCODE_Q,	0x26 },	// q
	{ SDL_SCANCODE_R,	0x27 },	// r
	// Row 3
	{ -1,			0x30 },	// ]	TODO!	sadly, stupid HUN layout uses that at normal place :(
	{ SDL_SCANCODE_I,	0x31 },	// i
	{ SDL_SCANCODE_O,	0x32 },	// o
	{ SDL_SCANCODE_LEFTBRACKET,	0x33 },	// ő	on HUN kbd
	{ -1,			0x34 },	// [	TODO!	sadly, stupid HUN layout uses that at normal place :(
	{ SDL_SCANCODE_RIGHTBRACKET,	0x35 },	// ú
	{ SDL_SCANCODE_P,	0x36 },	// p
	{ SDL_SCANCODE_U,	0x37 },	// u
	// Row 4
	// gds\h<af
	{ SDL_SCANCODE_G,	0x40 },	// g
	{ SDL_SCANCODE_D,	0x41 },	// d
	{ SDL_SCANCODE_S,	0x42 },	// s
	{ -1,			0x43 },	// blackslash	TODO!
	{ SDL_SCANCODE_H,	0x44 },	// h
	{ -1,			0x45 },	// <		TODO!
	{ SDL_SCANCODE_A,	0x46 },	// a
	{ SDL_SCANCODE_F,	0x47 },	// f
	// Row 5
	//  klá űéj
	{ SDL_SCANCODE_BACKSPACE, 0x50 },	// DEL
	{ SDL_SCANCODE_K,	0x51 },	// k
	{ SDL_SCANCODE_L,	0x52 },	// l
	{ SDL_SCANCODE_APOSTROPHE,	0x53 },	// á	on HUN kbd
	{ SDL_SCANCODE_RETURN,	0x54 },	// RETURN
	{ SDL_SCANCODE_BACKSLASH,	0x55 },	// ű	on HUN kbd
	{ SDL_SCANCODE_SEMICOLON,	0x56 },	// é	on HUN kbd
	{ SDL_SCANCODE_J,	0x57 },	// j
	// Row 6
	// bcx n yv
	{ SDL_SCANCODE_B,	0x60 },	// b
	{ SDL_SCANCODE_C,	0x61 },	// c
	{ SDL_SCANCODE_X,	0x62 },	// x
	{ SDL_SCANCODE_LSHIFT,	0x63 },	// SHIFT
	{ SDL_SCANCODE_N,	0x64 },	// n
	{ SDL_SCANCODE_TAB,	0x65 },	// LOCK
	{ SDL_SCANCODE_Y,	0x66 },	// y
	{ SDL_SCANCODE_V,	0x67 },	// v
	// Row 7
	// 
	{ SDL_SCANCODE_LALT,	0x70 },	// ALT
	{ SDL_SCANCODE_COMMA,	0x71 },	// ,?
	{ SDL_SCANCODE_PERIOD,	0x72 },	// .:
	{ SDL_SCANCODE_ESCAPE,	0x73 },	// ESC
	{ SDL_SCANCODE_LCTRL,	0x74 },	// CTRL
	{ SDL_SCANCODE_SPACE,	0x75 },	// SPACE
	{ SDL_SCANCODE_SLASH,	0x76 },	// -_
	{ SDL_SCANCODE_M,	0x77 },	// m
	// Row 8, has "only" cursor control (joy), _and_ INS
	{ SDL_SCANCODE_INSERT,	0x80 },	// INS
	{ SDL_SCANCODE_UP,	0x81 }, // cursor up
	{ SDL_SCANCODE_DOWN,	0x82 },	// cursor down
	{ SDL_SCANCODE_RIGHT,	0x85 }, // cursor right
	{ SDL_SCANCODE_LEFT,	0x86 }, // cursor left
	// Standard Xemu hot-keys, given by a macro
	STD_XEMU_SPECIAL_KEYS,
	// **** this must be the last line: end of mapping table ****
	{ 0, -1 }
};



void clear_emu_events ( void )
{
	hid_reset_events(1);
}


// Well :) So there is not so much *ANY* CRTC emulation, sorry ...
// it just an ugly hack to render one frame at once, from the beginning
// of the 16K video RAM, that's all
// Note: though the selected 16K (TVC64+ is emulated!) is used ...
static inline void render_tvc_screen ( void )
{
	int tail, y;
	Uint32 *pix = emu_start_pixel_buffer_access(&tail);
	int stupid_sweep = 0;
	for (y = 0; y < 240; y++) {
		int x;
		for (x = 0; x < 64; x++) {
			Uint8 b = crtc_p[stupid_sweep];
			switch (colour_mode) {
				case 0:	// 2-colour mode
					pix[0] = palette[(b >> 7) & 1];
					pix[1] = palette[(b >> 6) & 1];
					pix[2] = palette[(b >> 5) & 1];
					pix[3] = palette[(b >> 4) & 1];
					pix[4] = palette[(b >> 3) & 1];
					pix[5] = palette[(b >> 2) & 1];
					pix[6] = palette[(b >> 1) & 1];
					pix[7] = palette[ b       & 1];
					break;
				case 1:	// 4-colour mode (this may be optimized with look-up table ...)
					pix[0] = pix[1] = palette[((b & 0x80) >> 7) | ((b & 0x08) >> 2)];
					pix[2] = pix[3] = palette[((b & 0x40) >> 6) | ((b & 0x04) >> 1)];
					pix[4] = pix[5] = palette[((b & 0x20) >> 5) |  (b & 0x02)      ];
					pix[6] = pix[7] = palette[((b & 0x10) >> 4) | ((b & 0x01) << 1)];
					break;
				case 2: // 16-colour mode
					pix[0] = pix[1] = pix[2] = pix[3] = palette_col16_pixel1[b];
					pix[4] = pix[5] = pix[6] = pix[7] = palette_col16_pixel2[b];
					break;
			}
			pix += 8;
			stupid_sweep = (stupid_sweep + 1 ) & 0x3FFF;
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
	// FIXME: Ugly trick, we generate IRQ here, not by CRTC cursor stuff, what would do it for real on TVC!
	// This means, that TVC uses the cursor info of CRTC "abused" to be used an a periodic interrupt source. Tricky!
	// However, this implementation of mine if very wrong, and the exact time for the IRQ should depend on CRTC reg settings
	// I THINK ... :-D
	if (io_port_values[5] & 16) {	// if enabled at all!
		interrupt_active = 1;
		DEBUG("Cursor interrupt!" NL);
	}
	// Rest of the update stuff, but only at 25Hz rate ...
	if (!frameskip) {
		render_tvc_screen();
		hid_handle_all_sdl_events();
		emu_timekeeping_delay(40000);	// number: the time needed (real-time) for a "full frame"
	}
}


static void init_tvc ( void )
{
	int a;
	// Initialize colours (TODO: B&W mode is not used currently!)
	for (a = 0; a < 16; a++) {
		int red   = (a & 2) ? ((a & 8) ? 0xFF : 0x80) : 0;
		int green = (a & 4) ? ((a & 8) ? 0xFF : 0x80) : 0;
		int blue  = (a & 1) ? ((a & 8) ? 0xFF : 0x80) : 0;
		int y     = 0.299 * red + 0.587 * green + 0.114 * blue;
		tvc_palette_rgb[a] = SDL_MapRGBA(sdl_pix_fmt, red, green, blue, 0xFF);
		tvc_palette_bw [a] = SDL_MapRGBA(sdl_pix_fmt, y,   y,     y,    0xFF);
	}
	// Initialize helper tables for 16 colours mode
	//  16 colour mode does not use the 4 element palette register, but layout is not "TVC colour byte" ...
	//  TVC "colour byte is":	-I-G-R-B
	//  Format in 16 col mode:
	//	* first pixel:		I-G-R-B-
	//	* second pixel:		-I-G-R-B	(this matches the colour byte layout, btw!)
	for (a = 0; a < 256; a++) {
		palette_col16_pixel1[a] = TVC_COLOUR_BYTE_TO_SDL(a >> 1);
		palette_col16_pixel2[a] = TVC_COLOUR_BYTE_TO_SDL(a);
	}
	// I/O, ROM and RAM intialization ...
	memset(&mem, 0xFF, sizeof mem);
	memset(&io_port_values, 0x00, 0x100);	// some I/O handlers re-calls itself with other values, so we zero the stuff first
	for (a = 0; a < 0x100; a++)		// ... and now use the callback which sets our variables for some initial value ...
		z80ex_pwrite_cb(a, 0);
	for (a = 0; a < sizeof crtc_registers; a++)
		crtc_write_register(a, 0);
	if (emu_load_file("TVC22_D6.64K", mem.sys_rom + 0x0000, 0x2001) != 0x2000 ||
	    emu_load_file("TVC22_D4.64K", mem.sys_rom + 0x2000, 0x2001) != 0x2000 ||
	    emu_load_file("TVC22_D7.64K", mem.ext_rom + 0x0000, 0x2001) != 0x2000
	)
		FATAL("Cannot load ROM(s).");
}




int main ( int argc, char **argv )
{
	int cycles;
	xemu_dump_version(stdout, "The Careless Videoton TV Computer emulator from LGB");
	/* Initiailize SDL - note, it must be before loading ROMs, as it depends on path info from SDL! */
	if (emu_init_sdl(
		TARGET_DESC APP_DESC_APPEND,	// window title
		APP_ORG, TARGET_NAME,		// app organization and name, used with SDL pref dir formation
		1,				// resizable window
		SCREEN_WIDTH, SCREEN_HEIGHT,	// texture sizes
		SCREEN_WIDTH, SCREEN_HEIGHT * 2,	// logical size (width is doubled for somewhat correct aspect ratio)
		SCREEN_WIDTH, SCREEN_HEIGHT * 2,	// window size (tripled in size, original would be too small)
		SCREEN_FORMAT,		// pixel format
		0,			// custom palette init. Later, in init_tvc()
		NULL,			// -- "" --
		NULL,			// -- "" --
		RENDER_SCALE_QUALITY,	// render scaling quality
		USE_LOCKED_TEXTURE,	// 1 = locked texture access
		NULL			// no emulator specific shutdown function
	))
		return 1;
	hid_init(
		tvc_key_map,
		VIRTUAL_SHIFT_POS,
		SDL_DISABLE		// no joystick HID events
	);
	init_tvc();
	// Continue with initializing ...
	clear_emu_events();	// also resets the keyboard
	z80ex_init();
	cycles = 0;
	interrupt_active = 0;
	emu_timekeeping_start();	// we must call this once, right before the start of the emulation
	for (;;) { // our emulation loop ...
		if (interrupt_active) {
			int a = z80ex_int();
			if (a)
				cycles += a;
			else
				cycles += z80ex_step();
		} else
			cycles += z80ex_step();
		if (cycles >= CLOCKS_PER_FRAME) {
			update_emulator();
			frameskip = !frameskip;
			cycles -= CLOCKS_PER_FRAME;
		}
	}
	return 0;
}
