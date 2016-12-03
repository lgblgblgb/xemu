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
static int   crtc_register;

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
	DEBUGPRINT("CRTC: register %02Xh is written with data %02Xh" NL, reg, data);
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
			break;
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
		case 0x06:
			colour_mode = value & 3;
			if (colour_mode == 3)
				colour_mode = 2;
			DEBUGPRINT("VIDEO: colour mode is %d" NL, colour_mode);
			break;
		case 0x0C:
		case 0x0D:
		case 0x0E:
		case 0x0F:
			crtc_p = mem.video_ram + ((value & 0x30) << 10);
			z80ex_pwrite_cb(2, io_port_values[2]);	// TODO: can be optimized to only call, if VID page is paged in by port 2 ...
			break;
		case 0x60:
		case 0x61:
		case 0x62:
		case 0x63:
			palette[port16 & 3] = TVC_COLOUR_BYTE_TO_SDL(value);
			break;
		case 0x70:
			crtc_register = value;
			break;
		case 0x71:
			crtc_write_register(crtc_register, value);
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
static const struct KeyMapping tvc_key_map[] = {
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
	if (!frameskip) {
		render_tvc_screen();
		hid_handle_all_sdl_events();
		emu_timekeeping_delay(40000);
	}
}


static void init_tvc ( void )
{
	int a;
	// Initialize colours (TODO: B&W mode is not implemented currently)
	for (a = 0; a < 16; a++) {
		tvc_palette_rgb[a] = SDL_MapRGBA(
			sdl_pix_fmt,
			(a & 2) ? ((a & 8) ? 0xFF : 0x80) : 0,		// RED
			(a & 4) ? ((a & 8) ? 0xFF : 0x80) : 0,		// GREEN
			(a & 1) ? ((a & 8) ? 0xFF : 0x80) : 0,		// BLUE
			0xFF						// alpha channel
		);
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
