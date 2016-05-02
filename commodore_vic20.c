/* Test-case for a very simple and inaccurate Commodore VIC-20 emulator using SDL2 library.
   Copyright (C)2016 LGB (Gábor Lénárt) <lgblgblgb@gmail.com>

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

#include "commodore_vic20.h"
#include "cpu65c02.h"
#include "via65c22.h"
#include "emutools.h"




static Uint8 memory[0x10000];	// 64K address space of the 6502 CPU (some of it is ROM, undecoded, whatsoever ...)
static const Uint8 init_vic_palette_rgb[16 * 3] = {	// VIC palette given by RGB components
	0x00, 0x00, 0x00,	// black
	0xFF, 0xFF, 0xFF,	// white
	0xF0, 0x00, 0x00,	// red
	0x00, 0xF0, 0xF0,	// cyan
	0x60, 0x00, 0x60,	// purple
	0x00, 0xA0, 0x00,	// green
	0x00, 0x00, 0xF0,	// blue
	0xD0, 0xD0, 0x00,	// yellow
	0xC0, 0xA0, 0x00,	// orange
	0xFF, 0xA0, 0x00,	// light orange
	0xF0, 0x80, 0x80,	// pink
	0x00, 0xFF, 0xFF,	// light cyan
	0xFF, 0x00, 0xFF,	// light purple
	0x00, 0xFF, 0x00,	// light green
	0x00, 0xA0, 0xFF,	// light blue
	0xFF, 0xFF, 0x00	// light yellow
};
static Uint32 vic_palette[16];			// VIC palette with native SCREEN_FORMAT aware way. It will be initialized once only from vic_palette_rgb
static int running = 1;
static struct Via65c22 via1, via2;
static Uint8 kbd_matrix[8];		// keyboard matrix state, 8 * 8 bits
static int is_kpage_writable[64] = {	// writable flag (for different memory expansions) for every kilobytes of the address space, this shows the default, unexpanded config!
	1,		// @ 0K     (sum 1K), RAM
	0,0,0,		// @ 1K -3K (sum 3K), place for 3K expansion
	1,1,1,1,	// @ 4K- 7K (sum 4K), RAM
	0,0,0,0,0,0,0,0,// @ 8K-15K (sum 8K), expansion block
	0,0,0,0,0,0,0,0,// @16K-23K (sum 8K), expansion block
	0,0,0,0,0,0,0,0,// @24K-31K (sum 8K), expansion block
	0,0,0,0,	// @32K-35K (sum 4K), character ROM
	1,		// @36K     (sum 1K), I/O block   (VIAs, VIC-I, ...) [it's not RAM for real, but more-or-less we use that way in the emulator]
	1,		// @37K     (sum 1K), colour RAM, it seems only 0.5K, but the position depends on the other RAM config ...
	0,		// @38K     (sum 1K), I/O block 2 (?)
	0,		// @39K     (sum 1K), I/O block 3 (?)
	0,0,0,0,0,0,0,0,// @40K-47K (sum 8K), expansion ROM?
	0,0,0,0,0,0,0,0,// @48K-55K (sum 8K), basic ROM
	0,0,0,0,0,0,0,0 // @56K-63K (sum 8K), kernal ROM
};


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
	{ SDL_SCANCODE_LCTRL, 0x20}, { SDL_SCANCODE_RCTRL, 0x20}, // CTRL, we map both PC keyboard CTRL keys to the same location
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
	{ 0,	0xFF	}		// this must be the last line: end of mapping table
};




// Called by CPU emulation code
void  cpu_write(Uint16 addr, Uint8 data)
{
	if ((addr & 0xFFF0) == 0x9110) {
		memory[addr] = data;	// also store in "RAM" (well it's actually not that, but anyway ...)
		via_write(&via1, addr & 0xF, data);
	} else if ((addr & 0xFFF0) == 0x9120) {
		memory[addr] = data;	// also store in "RAM" (well it's actually not that, but anyway ...)
		via_write(&via2, addr & 0xF, data);
	} else if (is_kpage_writable[addr >> 10]) {
		if ((addr >> 10) == 37)
			data |= 0xF0;	// colour RAM has only 4 bits. Emulate this by forcing high 4 bits to '1' on each writes!
		memory[addr] = data;
	}
}

// Called by CPU emulation code
Uint8 cpu_read(Uint16 addr)
{
	if ((addr & 0xFFF0) == 0x9110)
		return via_read(&via1, addr & 0xF);
	if ((addr & 0xFFF0) == 0x9120)
		return via_read(&via2, addr & 0xF);
	return memory[addr];
}



/* To be honest, I am lame with VIC-I addressing ...
   I got these five "one-liners" from Sven's shadowVIC emulator, thanks a lot!!! */

static inline Uint16 vic_get_address (Uint8 bits10to12) {
	return ((bits10to12 & 7) | ((bits10to12 & 8) ? 0 : 32)) << 10;
}
static inline Uint16 vic_get_chrgen_address ( void ) {
	return vic_get_address(memory[0x9005] & 0xF);
}
static inline Uint16 vic_get_address_bit9 ( void ) {
	return (memory[0x9002] & 0x80) << 2;
}
static inline Uint16 vic_get_screen_address ( void ) {
	return vic_get_address(memory[0x9005] >> 4) | vic_get_address_bit9();
}
static inline Uint16 vic_get_colour_address ( void ) {
	return 0x9400 | vic_get_address_bit9();
}



static void render_screen ( void )
{
	int x, y, sc, tail;
	Uint32 *pp;
	Uint8 *vidp, *colp, *chrp;
	Uint32 bg = vic_palette[memory[0x900F] >> 4];	// background colour ...
	// Render VIC screen to "pixels"
	// Note: this is VERY incorrect, and only some std screen aware rendering,
	// with ignoring almost ALL of the VIC's possibilities!!!!!!
	// This is only a demonstration
	// Real emulation would use VIC registers, also during the main loop
	// so raster effects etc would work!!!! But this is only a quick demonstration :)
	// This should be done in sync with CPU emulation instead in steps, using the VIC-I registers to define parameters, and VIC-20 memory expansion (ie different video RAM location or so?) ...
	x = 0;
	y = 0;
	sc = 0;
#if 0
	vidp = memory + ((((memory[0x9005] & 0xF0) ^ 128) << 6) | ((memory[0x9002] & 128) << 2));
	colp = memory + (0x9400 | ((memory[0x9002] & 128) << 2));
	chrp = memory + ((memory[0x9005] & 15) << 10);
	//printf("SCREEN: vidp = %04Xh\n", vidp - memory);
	vidp = memory + 0x1E00;
	colp = memory + 0x9600;
	chrp = memory + 0x8000;
#endif
	vidp = memory + vic_get_screen_address();
	colp = memory + vic_get_colour_address();
	chrp = memory + vic_get_chrgen_address();
	pp = emu_start_pixel_buffer_access(&tail);
	while (y < 23) {
		int b;
		Uint8 shape = chrp[((*vidp) << 3) + sc];	// shape of current scanline of the current character
		Uint32 fg = vic_palette[*(colp) & 0x7];			// foreground colour
		//Uint8 shape = memory[0x8000 + sc];
		if ((*(colp) & 0x8))
			shape = 255 - shape; 	// inverse
		for (b = 128; b; b >>= 1)
			*(pp++) = (shape & b) ? fg : bg;
		if (x < 21) {
			vidp++;
			colp++;
			x++;
		} else {
			x = 0;
			pp += tail;		// texture 'tail'
			if (sc < 7) {
				vidp -= 21;	// "rewind" video pointer
				colp -= 21;	// ... and also the colour RAM pointer
				sc++;		// next scanline of character
			} else {
				y++;
				sc = 0;
				vidp++;
				colp++;
			}
		}
	}
	emu_update_screen();
}






// pressed: non zero value = key is pressed, zero value = key is released
static void emulate_keyboard ( SDL_Scancode key, int pressed )
{
	if (key == SDL_SCANCODE_F11) {	// toggle full screen mode on/off
		if (pressed)
			emu_set_full_screen(-1);
	} else if (key == SDL_SCANCODE_F9) {	// exit emulator ...
		if (pressed)
			running = 0;
	} else {
		const struct KeyMapping *map = key_map;
		while (map->pos != 0xFF) {
			if (map->scan == key) {
				if (pressed) {
					if (map->pos & 8)	// shifted key emu?
						kbd_matrix[3] &= 0xFD;	// press shift on VIC20!
					kbd_matrix[map->pos >> 4] &= 255 - (1 << (map->pos & 0x7));
				} else {
					if (map->pos & 8)	// shifted key emu?
						kbd_matrix[3] |= 2;	// release shift on VIC20!
					kbd_matrix[map->pos >> 4] |= 1 << (map->pos & 0x7);
				}
				//fprintf(stderr, "Found key, pos = %02Xh\n", map->pos);
				//debug_show_kbd_matrix();
				break;	// key found, end.
			}
			map++;
		}
	}
}



static void update_emulator ( void )
{
	SDL_Event e;
	// First: rendner VIC-20 screen ...
	render_screen();
	// Second: we must handle SDL events waiting for us in the event queue ...
	while (SDL_PollEvent(&e) != 0) {
		switch (e.type) {
			case SDL_QUIT:		// ie: someone closes the SDL window ...
				running = 0;	// set running to zero, main loop will exit then
				break;
			case SDL_KEYDOWN:	// key is pressed (down)
			case SDL_KEYUP:		// key is released (up)
				// make sure that key event is for our window, also that it's not a releated event by long key presses
				if (e.key.repeat == 0 && (e.key.windowID == sdl_winid || e.key.windowID == 0))
					emulate_keyboard(e.key.keysym.scancode, e.key.state == SDL_PRESSED);	// the last argument will be zero in case of release, other val in case of pressing
				break;
		}
	}
	// Sleep ... Please read emutools.c source about this madness ... 40000 is (PAL) microseconds for a full frame to be produced
	emu_sleep(40000);
}


/* VIA emulation callbacks, called by VIA core. See main() near to via_init() calls for further information */


static void via1_setint ( int level )
{
	if (level)	cpu_irqLevel |=   1;
	else		cpu_irqLevel &= 254;
}


static void via2_setint ( int level )
{
	if (level)	cpu_irqLevel |=   2;
	else		cpu_irqLevel &= 253;
}




static Uint8 via2_kbd_get_scan ( Uint8 mask )
{
	return
		((via2.ORB &   1) ? 0xFF : kbd_matrix[0]) &
		((via2.ORB &   2) ? 0xFF : kbd_matrix[1]) &
		((via2.ORB &   4) ? 0xFF : kbd_matrix[2]) &
		((via2.ORB &   8) ? 0xFF : kbd_matrix[3]) &
		((via2.ORB &  16) ? 0xFF : kbd_matrix[4]) &
		((via2.ORB &  32) ? 0xFF : kbd_matrix[5]) &
		((via2.ORB &  64) ? 0xFF : kbd_matrix[6]) &
		((via2.ORB & 128) ? 0xFF : kbd_matrix[7])
	;
}



static inline void __mark_ram ( int start_k, int size_k )
{
	while (size_k--)
		is_kpage_writable[start_k++] = 1;
}


static void vic20_configure_ram ( int exp0, int exp1, int exp2, int exp3, int exp4 )
{
	if (exp0)
		__mark_ram( 1, 3);
	if (exp1)
		__mark_ram( 8, 8);
	if (exp2)
		__mark_ram(16, 8);
	if (exp3)
		__mark_ram(24, 8);
	if (exp4)
		__mark_ram(40, 8);
}






int main ( int argc, char **argv )
{
	int cycles;
	/* Select RAM config based on command line options, quite lame currently :-) */
	if (argc > 1) {
		if (strlen(argv[1]) == 5)
			vic20_configure_ram(
				argv[1][0] & 1,
				argv[1][1] & 1,
				argv[1][2] & 1,
				argv[1][3] & 1,
				argv[1][4] & 1
			);
	}
	/* Initiailize SDL - note, it must be before loading ROMs, as it depends on path info from SDL! */
	if (emu_init_sdl(
		"VIC-20", "nemesys.lgb", "xclcd-vic20",
		1,	// resizable window
		SCREEN_WIDTH, SCREEN_HEIGHT,	// texture sizes
		SCREEN_WIDTH, SCREEN_HEIGHT,	// logical size (same as texture for now ...)
		SCREEN_WIDTH * SCREEN_DEFAULT_ZOOM, SCREEN_HEIGHT * SCREEN_DEFAULT_ZOOM,	// window size
		SCREEN_FORMAT,		// pixel format
		16,			// we have 16 colours
		init_vic_palette_rgb,	// initialize palette from this constant array
		vic_palette,		// initialize palette into this stuff
		1,			// render scaling quality
		USE_LOCKED_TEXTURE,	// 1 = locked texture access
		NULL			// no emulator specific shutdown function
	))
		return 1;
	/* Intialize memory and load ROMs */
	memset(memory, 0xFF, sizeof memory);
	if (
		emu_load_file("vic20-chargen.rom", memory + 0x8000, 0x1000) +	// load chargen ROM
		emu_load_file("vic20-basic.rom",   memory + 0xC000, 0x2000) +	// load basic ROM
		emu_load_file("vic20-kernal.rom",  memory + 0xE000, 0x2000)	// load kernal ROM
	) {
		fprintf(stderr, "Cannot load some of the needed ROM images (see message above)!\n");
		return 1;
	}
	memset(kbd_matrix, 0xFF, sizeof kbd_matrix);	// initialize keyboard matrix [bit 1 = unpressed, thus 0xFF for a line]
	cpu_reset();	// reset CPU: it must be AFTER kernal is loaded at least, as reset also fetches the reset vector into PC ...
	// Initiailize VIAs.
	// Note: this is my unfinished VIA emulation skeleton, for my Commodore LCD emulator originally, ported from my JavaScript code :)
	// it uses call back functions, which must be registered here, NULL values means unused functionality
	via_init(&via1, "VIA-1",	// from $9110 on VIC-20
		NULL,	// outa
		NULL,	// outb
		NULL,	// outsr
		NULL,	// ina
		NULL,	// inb
		NULL,	// insr
		via1_setint	// setint, called by via core, if interrupt level changed for whatever reason (ie: expired timer ...)
	);
	via_init(&via2, "VIA-2",	// from $9120 on VIC-20
		NULL,			// outa [reg 1]
		NULL, //via2_kbd_set_scan,	// outb [reg 0], we wire port B as output to set keyboard scan, HOWEVER, we use ORB directly in get scan!
		NULL,	// outsr
		via2_kbd_get_scan,	// ina  [reg 1], we wire port A as input to get the scan result, which was selected with port-A
		NULL,			// inb  [reg 0]
		NULL,	// insr
		via2_setint	// setint, same for VIA2 as with VIA1. Note: I have no idea if both VIAs can generate IRQ on VIC-20 though, maybe it's overkill to do for both and even cause problems?
	);
	cycles = 0;
	emu_timekeeping_start();	// we must call this once, right before the start of the emulation
	while (running) { // our emulation loop ...
		int opcyc;
		//printf("%04Xh\n", cpu_pc);	
		opcyc = cpu_step();	// execute one opcode (or accept IRQ, etc), return value is the used clock cycles
		via_tick(&via1, opcyc);	// run VIA-1 tasks for the same amount of cycles as the CPU
		via_tick(&via2, opcyc);	// -- "" -- the same for VIA-2
		cycles += opcyc;
		if (cycles >= CPU_CYCLES_PER_TV_FRAME) {	// if enough cycles elapsed (what would be the amount of CPU cycles for a TV frame), let's call the update function.
			update_emulator();	// this is the heart of screen update, also to handle SDL events (like key presses ...)
			cycles -= CPU_CYCLES_PER_TV_FRAME;	// not just cycle = 0, to avoid rounding errors, but it would not matter too much anyway ...
		}
	}
	puts("Goodbye!");
	return 0;
}

