/* Commodore LCD emulator.
   Copyright (C)2016 LGB (Gábor Lénárt) <lgblgblgb@gmail.com>

   This is an ongoing work to rewrite my old Commodore LCD emulator:

	* Commodore LCD emulator, C version.
	* (C)2013,2014 LGB Gabor Lenart
	* Visit my site (the better, JavaScript version of the emu is here too): http://commodore-lcd.lgb.hu/

   The goal is - of course - writing a primitive but still better than previous Commodore LCD emulator :)
   Note: I would be interested in VICE adoption, but I am lame with VICE, too complex for me :)

   This emulator based on my previous try (written in C), which is based on my previous JavaScript
   based emulator, which was the world's first Commodore LCD emulator.

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
#include <time.h>

#include <SDL.h>

#include "cpu65c02.h"
#include "via65c22.h"
#include "emutools.h"
#include "commodore_lcd.h"


static Uint8 memory[0x40000];
static Uint8 charrom[2048];
extern unsigned const char roms[];
static int mmu[3][4] = {
	{0, 0, 0, 0},
	{0, 0, 0, 0},
	{0, 0, 0x30000, 0x30000}
};
static int *mmu_current = mmu[0];
static int *mmu_saved = mmu[0];
static Uint8 lcd_ctrl[4];
static struct Via65c22 via1, via2;
static Uint8 kbd_matrix[9];
static Uint8 keysel;
static int running = 1;
static Uint8 rtc_regs[16];
static int rtc_sel = 0;

static const Uint8 init_lcd_palette_rgb[6] = {
	0xC0, 0xC0, 0xC0,
	0x00, 0x00, 0x00
};
static Uint32 lcd_palette[2];

static const Uint8 fontHack[] = {
	0x00,0x20,0x54,0x54,0x54,0x78,	0x00,0x7f,0x44,0x44,0x44,0x38,
	0x00,0x38,0x44,0x44,0x44,0x28,	0x00,0x38,0x44,0x44,0x44,0x7f,
	0x00,0x38,0x54,0x54,0x54,0x08,	0x00,0x08,0x7e,0x09,0x09,0x00,
	0x00,0x18,0xa4,0xa4,0xa4,0x7c,	0x00,0x7f,0x04,0x04,0x78,0x00,
	0x00,0x00,0x00,0x7d,0x40,0x00,	0x00,0x40,0x80,0x84,0x7d,0x00,
	0x00,0x7f,0x10,0x28,0x44,0x00,	0x00,0x00,0x00,0x7f,0x40,0x00,
	0x00,0x7c,0x04,0x18,0x04,0x78,	0x00,0x7c,0x04,0x04,0x78,0x00,
	0x00,0x38,0x44,0x44,0x44,0x38,	0x00,0xfc,0x44,0x44,0x44,0x38,
	0x00,0x38,0x44,0x44,0x44,0xfc,	0x00,0x44,0x78,0x44,0x04,0x08,
	0x00,0x08,0x54,0x54,0x54,0x20,	0x00,0x04,0x3e,0x44,0x24,0x00,
	0x00,0x3c,0x40,0x20,0x7c,0x00,	0x00,0x1c,0x20,0x40,0x20,0x1c,
	0x00,0x3c,0x60,0x30,0x60,0x3c,	0x00,0x6c,0x10,0x10,0x6c,0x00,
	0x00,0x9c,0xa0,0x60,0x3c,0x00,	0x00,0x64,0x54,0x54,0x4c,0x00
};

struct KeyMapping {
	SDL_Scancode	scan;	// SDL scancode for the given key we want to map
	Uint8		pos;	// BCD packed, high nibble / low nibble for col/row to map to.  0xFF means end of table!, high bit set on low nibble: press virtual shift as well!
};
static const struct KeyMapping key_map[] = {
	{ SDL_SCANCODE_BACKSPACE, 0x00 },
	{ SDL_SCANCODE_3, 0x01 },
	{ SDL_SCANCODE_5, 0x02 },
	{ SDL_SCANCODE_7, 0x03 },
	{ SDL_SCANCODE_9, 0x04 },
	{ SDL_SCANCODE_DOWN, 0x05 },
	{ SDL_SCANCODE_LEFT, 0x06 },
	{ SDL_SCANCODE_1, 0x07 },
	{ SDL_SCANCODE_RETURN, 0x10 },
	{ SDL_SCANCODE_W, 0x11 },
	{ SDL_SCANCODE_R, 0x12 },
	{ SDL_SCANCODE_Y, 0x13 },
	{ SDL_SCANCODE_I, 0x14 },
	{ SDL_SCANCODE_P, 0x15 },
	{ SDL_SCANCODE_RIGHTBRACKET, 0x16 },	// this would be '*'
	{ SDL_SCANCODE_HOME, 0x17 },
	{ SDL_SCANCODE_TAB, 0x20 },
	{ SDL_SCANCODE_A, 0x21 },
	{ SDL_SCANCODE_D, 0x22 },
	{ SDL_SCANCODE_G, 0x23 },
	{ SDL_SCANCODE_J, 0x24 },
	{ SDL_SCANCODE_L, 0x25 },
	{ SDL_SCANCODE_SEMICOLON, 0x26 },
	{ SDL_SCANCODE_F2, 0x27 },
	{ SDL_SCANCODE_F7, 0x30 },
	{ SDL_SCANCODE_4, 0x31 },
	{ SDL_SCANCODE_6, 0x32 },
	{ SDL_SCANCODE_8, 0x33 },
	{ SDL_SCANCODE_0, 0x34 },
	{ SDL_SCANCODE_UP, 0x35 },
	{ SDL_SCANCODE_RIGHT, 0x36 },
	{ SDL_SCANCODE_2, 0x37 },
	{ SDL_SCANCODE_F1, 0x40 },
	{ SDL_SCANCODE_Z, 0x41 },
	{ SDL_SCANCODE_C, 0x42 },
	{ SDL_SCANCODE_B, 0x43 },
	{ SDL_SCANCODE_M, 0x44 },
	{ SDL_SCANCODE_PERIOD, 0x45 },
	{ SDL_SCANCODE_ESCAPE, 0x46 },
	{ SDL_SCANCODE_SPACE, 0x47 },
	{ SDL_SCANCODE_F3, 0x50 },
	{ SDL_SCANCODE_S, 0x51 },
	{ SDL_SCANCODE_F, 0x52 },
	{ SDL_SCANCODE_H, 0x53 },
	{ SDL_SCANCODE_K, 0x54 },
	{ SDL_SCANCODE_APOSTROPHE, 0x55 },	// this would be ':'
	{ SDL_SCANCODE_EQUALS, 0x56 },
	{ SDL_SCANCODE_F8, 0x57 },
	{ SDL_SCANCODE_F5, 0x60 },
	{ SDL_SCANCODE_E, 0x61 },
	{ SDL_SCANCODE_T, 0x62 },
	{ SDL_SCANCODE_U, 0x63 },
	{ SDL_SCANCODE_O, 0x64 },
	{ SDL_SCANCODE_MINUS, 0x65 },
	{ SDL_SCANCODE_BACKSLASH, 0x66 }, // this would be '+'
	{ SDL_SCANCODE_Q, 0x67 },
	{ SDL_SCANCODE_LEFTBRACKET, 0x70 }, // this would be '@'
	{ SDL_SCANCODE_F4, 0x71 },
	{ SDL_SCANCODE_X, 0x72 },
	{ SDL_SCANCODE_V, 0x73 },
	{ SDL_SCANCODE_N, 0x74 },
	{ SDL_SCANCODE_COMMA, 0x75 },
	{ SDL_SCANCODE_SLASH, 0x76 },
	{ SDL_SCANCODE_F6, 0x77 },
	// extra keys, not part of main keyboard map, but separated byte (SR register can provide both in CLCD)
	{ SDL_SCANCODE_END, 0x80 },	// this would be the 'STOP' key
	{ SDL_SCANCODE_CAPSLOCK, 0x81},
	{ SDL_SCANCODE_LSHIFT, 0x82 }, { SDL_SCANCODE_RSHIFT, 0x82 },
	{ SDL_SCANCODE_LCTRL, 0x83 }, { SDL_SCANCODE_RCTRL, 0x83 },
	{ SDL_SCANCODE_LALT, 0x84 }, { SDL_SCANCODE_RALT, 0x84 },
	{ 0,	0xFF	}	// this must be the last line: end of mapping table
};




int cpu_trap ( Uint8 opcode )
{
	return 0;	// not recognized
}


void clear_emu_events ( void )
{
	memset(kbd_matrix, 0, sizeof kbd_matrix);	// resets the keyboard
}


#define GET_MEMORY(phys_addr) memory[phys_addr]


#if 0
static Uint8 GET_MEMORY(int phys_addr)
{
	if (phys_addr == 0x382F7) {
		printf("Before romchecksum: X=%02Xh A=%02Xh" NL, cpu_x, cpu_a);
	}
	if (phys_addr == 0x383A1) {
		printf("At romchecksum RTS: X=%02Xh A=%02Xh" NL, cpu_x, cpu_a);
	}
	if (phys_addr == 0x382FA) {
		printf("After romchecksum : X=%02Xh A=%02Xh" NL, cpu_x, cpu_a);
	}
	return memory[phys_addr];
}
#endif


Uint8 cpu_read ( Uint16 addr ) {
	if (addr <  0x1000) return GET_MEMORY(addr);
	if (addr <  0xF800) return GET_MEMORY((mmu_current[addr >> 14] + addr) & 0x3FFFF);
	if (addr >= 0xFA00) return GET_MEMORY(addr | 0x30000);
	if (addr >= 0xF980) return 0; // ACIA
	if (addr >= 0xF900) return 0xFF; // I/O exp
	if (addr >= 0xF880) return via_read(&via2, addr & 15);
	return via_read(&via1, addr & 15);
}

void cpu_write ( Uint16 addr, Uint8 data ) {
	int maddr;
	if (addr < 0x1000) {
		memory[addr] = data;
		return;
	}
	if (addr >= 0xF800) {
		switch ((addr - 0xF800) >> 7) {
			case  0: via_write(&via1, addr & 15, data); return;
			case  1: via_write(&via2, addr & 15, data); return;
			case  2: return; // I/O exp area is not handled
			case  3: return; // no ACIA yet
			case  4: mmu_current = mmu[2]; return;
			case  5: mmu_current = mmu[1]; return;
			case  6: mmu_current = mmu[0]; return;
			case  7: mmu_current = mmu_saved; return;
			case  8: mmu_saved = mmu_current; return;
			case  9: FATAL("MMU test mode is set, it would not work"); break;
			case 10: mmu[1][0] = data << 10; return;
			case 11: mmu[1][1] = data << 10; return;
			case 12: mmu[1][2] = data << 10; return;
			case 13: mmu[1][3] = data << 10; return;
			case 14: mmu[2][1] = data << 10; return;
			case 15: lcd_ctrl[addr & 3] = data; return;
		}
		printf("ERROR: should be not here!\n");
		return;
	}
	maddr = (mmu_current[addr >> 14] + addr) & 0x3FFFF;
	if (maddr < RAM_SIZE) {
		memory[maddr] = data;
		return;
	}
	printf("MEM: out-of-RAM write addr=$%04X maddr=$%05X\n", addr, maddr);
}


static Uint8 portB1 = 0, portA2 = 0;
static int keytrans = 0;
static int powerstatus = 0;


static void  via1_outa(Uint8 mask, Uint8 data) { keysel = data & mask; }
static void  via1_outb(Uint8 mask, Uint8 data) {
	keytrans = ((!(portB1 & 1)) && (data & 1));
	portB1 = data;
}
static void  via1_outsr(Uint8 data) {}
static Uint8 via1_ina(Uint8 mask) { return 0xFF; }
static Uint8 via1_inb(Uint8 mask) { return 0xFF; }
static void  via2_setint(int level) {}
static void  via2_outa(Uint8 mask, Uint8 data) {
	portA2 = data;
	// ugly stuff, but now the needed part is cut here from my other emulator :)
	if (portB1 & 2) {	// RTC RD
		if (data & 64) {
			rtc_sel = data & 15;
		}
	}
}
static void  via2_outb(Uint8 mask, Uint8 data) {}
static void  via2_outsr(Uint8 data) {}
static Uint8 via2_ina(Uint8 mask) {
	if (portB1 & 2) {
		if (portA2 & 16) {
			return rtc_regs[rtc_sel] | (portA2 & 0x70);
		}
		return portA2;
	}
	return 0;
}
static Uint8 via2_inb(Uint8 mask) { return 0xFF; }
static Uint8 via2_insr() { return 0xFF; }
static Uint8 via1_insr()
{
	if (keytrans) {
		int data = 0;
		keytrans = 0;
		if (!(keysel &   1)) data |= kbd_matrix[0];
		if (!(keysel &   2)) data |= kbd_matrix[1];
		if (!(keysel &   4)) data |= kbd_matrix[2];
		if (!(keysel &   8)) data |= kbd_matrix[3];
		if (!(keysel &  16)) data |= kbd_matrix[4];
		if (!(keysel &  32)) data |= kbd_matrix[5];
		if (!(keysel &  64)) data |= kbd_matrix[6];
		if (!(keysel & 128)) data |= kbd_matrix[7];
		return data;
	} else
		return kbd_matrix[8] | powerstatus;
}
static void  via1_setint(int level)
{
	//printf("IRQ level: %d\n", level);
	cpu_irqLevel = level;
}



#define BG lcd_palette[0]
#define FG lcd_palette[1]

static void render_screen ( void )
{
	int ps = lcd_ctrl[1] << 7;
	int pd = 0, x, y, ch;
	int tail;
	Uint32 *pix = emu_start_pixel_buffer_access(&tail);
	if (lcd_ctrl[2] & 2) { // graphic mode
		for (y = 0; y < 128; y++) {
			for (x = 0; x < 60; x++) {
				ch = memory[ps++];
				*(pix++) = (ch & 128) ? FG : BG;
				*(pix++) = (ch &  64) ? FG : BG;
				*(pix++) = (ch &  32) ? FG : BG;
				*(pix++) = (ch &  16) ? FG : BG;
				*(pix++) = (ch &   8) ? FG : BG;
				*(pix++) = (ch &   4) ? FG : BG;
				*(pix++) = (ch &   2) ? FG : BG;
				*(pix++) = (ch &   1) ? FG : BG;
			}
			ps = (ps + 4) & 0x7FFF;
			pix += tail;
		}
	} else { // text mode
		int a, pc, m, cof = (lcd_ctrl[2] & 1) << 10, col;
		ps += lcd_ctrl[0] & 127; // X-Scroll register, only the lower 7 bits are used
		for (y = 0; y < 16; y++) {
			for (x = 0; x < 80; x++) {
				ps &= 0x7FFF;
				ch = memory[ps++];
				/* BEGIN hack: lowercase */
				//if ((ch & 127) >= 65 && (ch & 127) <= 90)
				//	ch = ( ch & 128) | ((ch & 127) - 64 );
				/* END hack */
				pc = cof + (6 * (ch & 127));
				col = (ch & 128) ? 0xff : 0x00;
				for (a = 0; a < 6; a++) {
					m = charrom[pc++] ^ col;
					pix[pd       ] = (m &   1) ? FG : BG;
					pix[pd +  480 + 1 * tail] = (m &   2) ? FG : BG;
					pix[pd +  960 + 2 * tail] = (m &   4) ? FG : BG;
					pix[pd + 1440 + 3 * tail] = (m &   8) ? FG : BG;
					pix[pd + 1920 + 4 * tail] = (m &  16) ? FG : BG;
					pix[pd + 2400 + 5 * tail] = (m &  32) ? FG : BG;
					pix[pd + 2880 + 6 * tail] = (m &  64) ? FG : BG;
					pix[pd + 3360 + 7 * tail] = (m & 128) ? FG : BG;
					pd++;
				}
			}
			ps += 48; // 128 - 80
			pd += 3360;
		}
	}
	emu_update_screen();
}



// pressed: non zero value = key is pressed, zero value = key is released
static void emulate_keyboard ( SDL_Scancode key, int pressed )
{
	if (key == SDL_SCANCODE_F11) {  // toggle full screen mode on/off
		if (pressed)
			emu_set_full_screen(-1);
	} else if (key == SDL_SCANCODE_F9) {    // exit emulator ...
		if (pressed)
			running = 0;
	} else {
		const struct KeyMapping *map = key_map;
		while (map->pos != 0xFF) {
			if (map->scan == key) {
				if (!pressed) {
					kbd_matrix[map->pos >> 4] &= 255 - (1 << (map->pos & 0x7));
				} else {
					kbd_matrix[map->pos >> 4] |= 1 << (map->pos & 0x7);
				}
				break;  // key found, end.
			}
			map++;
		}
	}
}



static void update_rtc ( void )
{
	struct tm *t = emu_get_localtime();
	rtc_regs[ 0] = t->tm_sec % 10;
	rtc_regs[ 1] = t->tm_sec / 10;
	rtc_regs[ 2] = t->tm_min % 10;
	rtc_regs[ 3] = t->tm_min / 10;
	rtc_regs[ 4] = t->tm_hour % 10;
	rtc_regs[ 5] = (t->tm_hour / 10) | 8; // TODO: AM/PM, 24h/12h time format
	rtc_regs[ 6] = t->tm_wday; // day of week
	rtc_regs[ 9] = t->tm_mday % 10;
	rtc_regs[10] = t->tm_mday / 10;
	rtc_regs[ 7] = (t->tm_mon + 1) % 10;
	rtc_regs[ 8] = (t->tm_mon + 1) / 10;
	rtc_regs[11] = (t->tm_year - 84) % 10; // beware of 2084, Commodore LCDs will have "Y2K-like" problem ... :)
	rtc_regs[12] = (t->tm_year - 84) / 10;
}




static void update_emulator ( void )
{
	SDL_Event e;
	render_screen();
	while (SDL_PollEvent(&e) != 0) {
		switch (e.type) {
			case SDL_QUIT:		// ie: someone closes the SDL window ...
				running = 0;	// set running to zero, main loop will exit then
				break;
			case SDL_KEYDOWN:	// key is pressed (down)
			case SDL_KEYUP:		// key is released (up)
				// make sure that key event is for our window, also that it's not a releated event by long key presses (repeats should be handled by the emulated machine's KERNAL)
				if (e.key.repeat == 0 && (e.key.windowID == sdl_winid || e.key.windowID == 0))
					emulate_keyboard(e.key.keysym.scancode, e.key.state == SDL_PRESSED);	// the last argument will be zero in case of release, other val in case of pressing
				break;
		}
	}
	emu_sleep(40000);	// 40000 microseconds would be the real time for a full TV frame (see main() for more info: CLCD is not TV based for real ...)
	if (seconds_timer_trigger)
		update_rtc();
}




int main ( int argc, char **argv )
{
	int cycles;
	if (emu_init_sdl(
		"Commodore LCD",		// window title
		"nemesys.lgb", "xclcd",		// app organization and name, used with SDL pref dir formation
		1,				// resizable window
		SCREEN_WIDTH, SCREEN_HEIGHT,	// texture sizes
		SCREEN_WIDTH, SCREEN_HEIGHT,	// logical size (same as texture for now ...)
		SCREEN_WIDTH * SCREEN_DEFAULT_ZOOM, SCREEN_HEIGHT * SCREEN_DEFAULT_ZOOM,	// window size
		SCREEN_FORMAT,		// pixel format
		2,			// we have 2 colours :)
		init_lcd_palette_rgb,	// initialize palette from this constant array
		lcd_palette,		// initialize palette into this stuff
		RENDER_SCALE_QUALITY,	// render scaling quality
		USE_LOCKED_TEXTURE,	// 1 = locked texture access
		NULL			// no emulator specific shutdown function
	))
		return 1;
	memset(memory, 0xFF, sizeof memory);
	memset(charrom, 0xFF, sizeof charrom);
	if (
		emu_load_file("clcd-u102.rom", memory + 0x38000, 0x8000) +
		emu_load_file("clcd-u103.rom", memory + 0x30000, 0x8000) +
		emu_load_file("clcd-u104.rom", memory + 0x28000, 0x8000) +
		emu_load_file("clcd-u105.rom", memory + 0x20000, 0x8000)
	) {
		ERROR_WINDOW("Cannot load some of the needed ROM images (see console messages)!");
		return 1;
	}
	// Ugly hacks :-( <patching ROM>
#ifdef ROM_HACK_COLD_START
	// this ROM patching is needed, as Commodore LCD seems not to work to well with "not intact" SRAM content (ie: it has battery powered SRAM even when "switched off")
	puts("ROM HACK: cold start condition");
	memory[0x385BB] = 0xEA;
	memory[0x385BC] = 0xEA;
#endif
#ifdef ROM_HACK_NEW_ROM_SEARCHING
	// this ROM hack modifies the ROM signature searching bytes so we can squeeze extra menu points of the main screen!
	// this hack SHOULD NOT be used, if the ROM 32K ROM images from 0x20000 and 0x28000 are not empty after offset 0x6800
	// WARNING: Commodore LCDs are known to have different ROM versions, be careful with different ROMs, if you find any!
	// [note: if you find other ROM versions, please tell me!!!! - that's the other message ...]
	puts("ROM HACK: modifying ROM searching MMU table");
	// overwrite MMU table positions for ROM scanner in KERNAL
	memory[0x382CC] = 0x8A;	// offset 0x6800 in the ROM image of clcd-u105.rom [phys memory address: 0x26800]
	memory[0x382CE] = 0xAA;	// offset 0x6800 in the ROM image of clcd-u104.rom [phys memory address: 0x2E800]
	// try to load "parasite" ROMs (it's not fatal if we cannot ...)
	// these loads to an unused part of the original ROM images
	emu_load_file("clcd-u105-parasite.rom", memory + 0x26800, 0x8000 - 0x6800);
	emu_load_file("clcd-u104-parasite.rom", memory + 0x2E800, 0x8000 - 0x6800);
#endif
	/* we would need the chargen ROM of CLCD but we don't have. We have to use
	 * some charset from the KERNAL (which is NOT the "hardware" charset!) and cheat a bit to create the alternate charset */
	memcpy(charrom, memory + 0x3F700, 1024);
	memcpy(charrom + 1024, memory + 0x3F700, 1024);
	memcpy(charrom + 390, charrom + 6, 26 * 6);
	memcpy(charrom + 6, fontHack, sizeof fontHack);
	/* init CPU */
	cpu_reset();	// we must do this after loading KERNAL at least, since PC is fetched from reset vector here!
	/* init VIAs */
	via_init(&via1, "VIA#1", via1_outa, via1_outb, via1_outsr, via1_ina, via1_inb, via1_insr, via1_setint);
	via_init(&via2, "VIA#2", via2_outa, via2_outb, via2_outsr, via2_ina, via2_inb, via2_insr, via2_setint);
	/* keyboard */
	clear_emu_events();	// also resets the keyboard
	keysel = 0;
	/* --- START EMULATION --- */
	cycles = 0;
	emu_timekeeping_start();	// we must call this once, right before the start of the emulation
	update_rtc();			// this will use time-keeping stuff as well, so initially let's do after the function call above
	while (running) {
		int opcyc = cpu_step();	// execute one opcode (or accept IRQ, etc), return value is the used clock cycles
		via_tick(&via1, opcyc);	// run VIA-1 tasks for the same amount of cycles as the CPU
		via_tick(&via2, opcyc);	// -- "" -- the same for VIA-2
		cycles += opcyc;
		/* Note, Commodore LCD is not TV standard based ... Since I have no idea about the update etc, I still assume some kind of TV-related stuff, who cares :) */
		if (cycles >= CPU_CYCLES_PER_TV_FRAME) {	// if enough cycles elapsed (what would be the amount of CPU cycles for a TV frame), let's call the update function.
			update_emulator();			// this is the heart of screen update, also to handle SDL events (like key presses ...)
			cycles -= CPU_CYCLES_PER_TV_FRAME;	// not just cycle = 0, to avoid rounding errors, but it would not matter too much anyway ...
		}
	}
	puts("Goodbye!");
	return 0;
}
