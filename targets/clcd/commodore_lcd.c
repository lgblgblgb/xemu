/* Commodore LCD emulator.
   Copyright (C)2016-2019 LGB (Gábor Lénárt) <lgblgblgb@gmail.com>
   Part of the Xemu project: https://github.com/lgblgblgb/xemu

   This is an ongoing work to rewrite my old Commodore LCD emulator:

	* Commodore LCD emulator, C version.
	* (C)2013,2014 LGB Gabor Lenart
	* Visit my site (the older, JavaScript version of the emu is here too): http://commodore-lcd.lgb.hu/

   The goal is - of course - writing a primitive but still better than previous Commodore LCD emulator :)
   Note: I would be interested in VICE adoption, but I am lame with VICE, too complex for me :)

   This emulator based on my previous try (written in C), which is based on my previous JavaScript
   based emulator, which was the world's first Commodore LCD emulator. Actually this emulator turned
   out to be Xemu with many new machines since then to be emulated, including Commodore 65 and
   MEGA65 too.

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
#include "xemu/emutools_files.h"
#include "xemu/emutools_hid.h"
#include "xemu/emutools_config.h"
#include "xemu/cpu65.h"
#include "xemu/via65c22.h"
#include "xemu/emutools.h"
#include "commodore_lcd.h"
#include <time.h>


static const char *rom_fatal_msg = "This is one of the selected ROMs. Without it, Xemu won't work.\nInstall it, or use -romXXX CLI switches to specify another path, see the -h output for help.";

static int cpu_mhz, cpu_cycles_per_tv_frame;

static Uint8 memory[0x40000];
static Uint8 charrom[4096];
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
static Uint8 keysel;
static Uint8 rtc_regs[16];
static int rtc_sel = 0;
static int ram_size;

static struct {
	int phase;
	Uint8 data[65536];
	int size;
} prg_inject;

static const Uint8 init_lcd_palette_rgb[6] = {
	0xC0, 0xC0, 0xC0,
	0x00, 0x00, 0x00
};
static Uint32 lcd_palette[2];

#define VIRTUAL_SHIFT_POS 0x82
static const struct KeyMappingDefault lcd_key_map[] = {
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
	STD_XEMU_SPECIAL_KEYS,
	{ 0,	-1	}	// this must be the last line: end of mapping table
};




int cpu65_trap_callback ( Uint8 opcode )
{
	return 0;	// not recognized
}


void clear_emu_events ( void )
{
	hid_reset_events(1);
}


#define GET_MEMORY(phys_addr) memory[phys_addr]


Uint8 cpu65_read_callback ( Uint16 addr ) {
	if (addr <  0x1000) return GET_MEMORY(addr);
	if (addr <  0xF800) return GET_MEMORY((mmu_current[addr >> 14] + addr) & 0x3FFFF);
	if (addr >= 0xFA00) return GET_MEMORY(addr | 0x30000);
	if (addr >= 0xF980) return 0; // ACIA
	if (addr >= 0xF900) return 0xFF; // I/O exp
	if (addr >= 0xF880) return via_read(&via2, addr & 15);
	return via_read(&via1, addr & 15);
}

void cpu65_write_callback ( Uint16 addr, Uint8 data ) {
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
		DEBUG("ERROR: should be not here!" NL);
		return;
	}
	maddr = (mmu_current[addr >> 14] + addr) & 0x3FFFF;
	if (maddr < ram_size) {
		memory[maddr] = data;
		return;
	}
	DEBUG("MEM: out-of-RAM write addr=$%04X maddr=$%05X" NL, addr, maddr);
}


// I guess Commodore LCD since used CMOS 65C02 already, no need to emulate the RMW behaviour on NMOS 6502 (??)
void cpu65_write_rmw_callback ( Uint16 addr, Uint8 old_data, Uint8 new_data )
{
	cpu65_write_callback(addr, new_data);
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
		if (!(keysel &   1)) data |= ~kbd_matrix[0];
		if (!(keysel &   2)) data |= ~kbd_matrix[1];
		if (!(keysel &   4)) data |= ~kbd_matrix[2];
		if (!(keysel &   8)) data |= ~kbd_matrix[3];
		if (!(keysel &  16)) data |= ~kbd_matrix[4];
		if (!(keysel &  32)) data |= ~kbd_matrix[5];
		if (!(keysel &  64)) data |= ~kbd_matrix[6];
		if (!(keysel & 128)) data |= ~kbd_matrix[7];
		return data;
	} else
		return (~kbd_matrix[8]) | powerstatus;
}
static void  via1_setint(int level)
{
	//DEBUG("IRQ level: %d" NL, level);
	cpu65.irqLevel = level;
}



#define BG lcd_palette[0]
#define FG lcd_palette[1]

static void render_screen ( void )
{
	int ps = lcd_ctrl[1] << 7;
	int x, y, ch;
	int tail;
	Uint32 *pix = xemu_start_pixel_buffer_access(&tail);
	if (lcd_ctrl[2] & 2) { // graphic mode
		for (y = 0; y < 128; y++) {
			for (x = 0; x < 60; x++) {
				ch = memory[ps++];
				*(pix++) = (ch & 0x80) ? FG : BG;
				*(pix++) = (ch & 0x40) ? FG : BG;
				*(pix++) = (ch & 0x20) ? FG : BG;
				*(pix++) = (ch & 0x10) ? FG : BG;
				*(pix++) = (ch & 0x08) ? FG : BG;
				*(pix++) = (ch & 0x04) ? FG : BG;
				*(pix++) = (ch & 0x02) ? FG : BG;
				*(pix++) = (ch & 0x01) ? FG : BG;
			}
			ps = (ps + 4) & 0x7FFF;
			pix += tail;
		}
	} else { // text mode
		int cof  = (lcd_ctrl[2] & 1) << 10;
		int maxx = (lcd_ctrl[3] & 4) ? 60 : 80;
		ps += lcd_ctrl[0] & 0x7F; // X-Scroll register, only the lower 7 bits are used
		for (y = 0; y < 128; y++) {
			for (x = 0; x < maxx; x++) {
				ps &= 0x7FFF;
				ch = memory[ps++];
				ch = charrom[cof + ((ch & 0x7F) << 3) + (y & 7)] ^ ((ch & 0x80) ? 0xFF : 0x00);
				pix[0] = (ch & 0x80) ? FG : BG;
				pix[1] = (ch & 0x40) ? FG : BG;
				pix[2] = (ch & 0x20) ? FG : BG;
				pix[3] = (ch & 0x10) ? FG : BG;
				pix[4] = (ch & 0x08) ? FG : BG;
				pix[5] = (ch & 0x04) ? FG : BG;
				if (lcd_ctrl[3] & 4) {
					pix[6] = (ch & 0x02) ? FG : BG;
					pix[7] = (ch & 0x01) ? FG : BG;
					pix += 8;
				} else
					pix += 6;
			}
			if ((y & 7) == 7)
				ps += 128 - maxx;
			else
				ps -= maxx;
			pix += tail;
		}
	}
	xemu_update_screen();
}



// HID needs this to be defined, it's up to the emulator if it uses or not ...
int emu_callback_key ( int pos, SDL_Scancode key, int pressed, int handled )
{
        return 0;
}



static void update_rtc ( void )
{
	struct tm *t = xemu_get_localtime();
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
	if (XEMU_UNLIKELY(prg_inject.phase == 1)) {
		static const Uint8 screen_sample1[] = { 0x20, 0x03, 0x0f, 0x0d, 0x0d, 0x0f, 0x04, 0x0f, 0x12, 0x05, 0x20, 0x0c, 0x03, 0x04, 0x20 };
		static const Uint8 screen_sample2[] = { 0x12, 0x05, 0x01, 0x04, 0x19, 0x2e };
		if (
			!memcmp(memory + 0x880, screen_sample1, sizeof screen_sample1) &&
			!memcmp(memory + 0x980, screen_sample2, sizeof screen_sample2)
		) {
			prg_inject.phase = 2;
			DEBUGPRINT("BASIC: startup screen detected, injecting loaded basic program!" NL);
			memory[0xA01] = 'R' - 'A' + 1;
			memory[0xA02] = 'U' - 'A' + 1;
			memory[0xA03] = 'N' - 'A' + 1;
			memory[0x1000] = 0;
			memset(memory + 0x1000, 0, 0x8000);
			memcpy(memory + 0x1001, prg_inject.data + 2, prg_inject.size - 2);
			//memset(memory + 0x1001 + prg_inject.size - 2, 0, 4);
			int addr = 0x1001;
			memory[0x65] = addr & 0xFF;
			memory[0x66] = addr >> 8;
			addr += prg_inject.size - 2;
			memory[0x67] = addr & 0xFF;
			memory[0x68] = addr >> 8;
		}
	}
	render_screen();
	hid_handle_all_sdl_events();
	xemu_timekeeping_delay(40000);	// 40000 microseconds would be the real time for a full TV frame (see main() for more info: CLCD is not TV based for real ...)
	if (seconds_timer_trigger)
		update_rtc();
}


static void shutdown_emu ( void )
{
#ifndef __EMSCRIPTEN__
	FILE *f = fopen("memory.dump", "wb");
	if (f) {
		fwrite(memory, sizeof memory, 1, f);
		fclose(f);
	}
#endif
	printf("Shutting down ...\n");
}


static void rom_list ( void )
{
	//const char *defprg = xemucfg_get_str("defprg");
	for (int addr = 0x20000; addr < 0x40000; addr += 0x4000) {
		if (!memcmp(memory + addr + 8, "Commodore LCD", 13)) {
			printf("ROM directory entry point @ $%05X\n", addr);
			int pos = addr + 13 + 8;
			while (memory[pos]) {
				char name[256];
				memcpy(name, memory + pos + 6, memory[pos] - 6);
				name[memory[pos] - 6] = 0;
				printf("\t($%02X $%02X $%02X) START=$%04X : \"%s\"\n",
					memory[pos + 1], memory[pos + 2], memory[pos + 3],
					memory[pos + 4] | (memory[pos + 5] <<8),
					name
				);
				//if (defprg && !strcasecmp(name, defprg)) {
				//	printf("\tFOUND!!!!!\n");
				//	memory[pos + 1] |= 0x20;
				//	memory[pos + 1] = 0x20;
				//} else if (defprg) {
				//	//memory[pos + 1] &= ~0x20;
				//}
				pos += memory[pos];
			}
		}
	}
}



static void load_program_for_inject ( const char *file_name, int new_address )
{
	prg_inject.phase = 0;
	if (!file_name || !*file_name)
		return;
	memset(prg_inject.data, 0, sizeof prg_inject.data);
	prg_inject.size = xemu_load_file(file_name, prg_inject.data, 8, sizeof(prg_inject.data) - 4, "Cannot load program");
	if (prg_inject.size <= 0)
		return;
	int old_address = prg_inject.data[0] | (prg_inject.data[1] << 8);
	DEBUGPRINT("PRG: program \"%s\" load_addr=$%04X, new_load_addr=$%04X" NL, file_name, old_address, new_address);
	if (old_address != new_address) {
		int i = 2;
		for (;;) {
			if (prg_inject.data[i] == 0 && prg_inject.data[i + 1] == 0)
				break;
			int o = i;	// offset of the next addr to patch
			i += 4;		// skip next line offset + line number
			while (prg_inject.data[i])
				i++;
			i++;
			new_address += i - o;
			DEBUGPRINT("BASIC: re-linking line (%d) $%04X -> $%04X" NL,
				prg_inject.data[o + 2] | (prg_inject.data[o + 3] << 8),
				prg_inject.data[o] | (prg_inject.data[o + 1] << 8),
				new_address
			);
			prg_inject.data[o] = new_address & 0xFF;
			prg_inject.data[o + 1] = new_address >> 8;
		}

	}
	prg_inject.phase = 1;
}




static void set_cpu_speed ( int mhz )
{
	if (mhz < 1)
		mhz = 1;
	else if (mhz > 16)
		mhz = 16;
	cpu_mhz = mhz;
	cpu_cycles_per_tv_frame = mhz * 1000000 / 25;
	DEBUGPRINT("CPU: setting CPU to %dMHz, %d CPU cycles per full 1/25sec frame." NL, mhz, cpu_cycles_per_tv_frame);
}



static int cycles, viacyc;


static void emulation_loop ( void )
{
	for (;;) {
		int opcyc = cpu65_step();	// execute one opcode (or accept IRQ, etc), return value is the used clock cycles
		viacyc += opcyc;
		cycles += opcyc;
		if (viacyc >= cpu_mhz) {
			int steps = viacyc / cpu_mhz;
			viacyc = viacyc % cpu_mhz;
			via_tick(&via1, steps);	// run VIA-1 tasks for the same amount of cycles as the CPU would do @ 1MHz
			via_tick(&via2, steps);	// -- "" -- the same for VIA-2
		}
		/* Note, Commodore LCD is not TV standard based ... Since I have no idea about the update etc, I still assume some kind of TV-related stuff, who cares :) */
		if (XEMU_UNLIKELY(cycles >= cpu_cycles_per_tv_frame)) {	// if enough cycles elapsed (what would be the amount of CPU cycles for a TV frame), let's call the update function.
			update_emulator();			// this is the heart of screen update, also to handle SDL events (like key presses ...)
			cycles -= cpu_cycles_per_tv_frame;	// not just cycle = 0, to avoid rounding errors, but it would not matter too much anyway ...
			return;
		}
	}
}





int main ( int argc, char **argv )
{
	xemu_pre_init(APP_ORG, TARGET_NAME, "The world's first Commodore LCD emulator from LGB");
	xemucfg_define_switch_option("fullscreen", "Start in fullscreen mode");
	xemucfg_define_num_option("ram", 128, "Sets RAM size in KBytes.");
	xemucfg_define_num_option("clock", 1, "Sets CPU speed in MHz, integer only, 1-16");
	xemucfg_define_str_option("rom102", "#clcd-u102.rom", "Selects 'U102' ROM to use");
	xemucfg_define_str_option("rom103", "#clcd-u103.rom", "Selects 'U103' ROM to use");
	xemucfg_define_str_option("rom104", "#clcd-u104.rom", "Selects 'U104' ROM to use");
	xemucfg_define_str_option("rom105", "#clcd-u105.rom", "Selects 'U105' ROM to use");
	xemucfg_define_str_option("romchr", "#clcd-chargen.rom", "Selects character ROM to use");
	//xemucfg_define_str_option("defprg", NULL, "Selects the ROM-program to set default to");
	xemucfg_define_str_option("prg", NULL, "Inject BASIC program on entering to BASIC");
	xemucfg_define_switch_option("syscon", "Keep system console open (Windows-specific effect only)");
	if (xemucfg_parse_all(argc, argv))
		return 1;
	set_cpu_speed(xemucfg_get_num("clock"));
	ram_size = xemucfg_get_num("ram");
	if (ram_size < 32 || ram_size > 128)
		FATAL("Bad ram size is defined, must be 32...128");
	ram_size <<= 10;
	DEBUGPRINT("CFG: ram size is %d bytes." NL, ram_size);
	if (xemu_post_init(
		TARGET_DESC APP_DESC_APPEND,	// window title
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
		shutdown_emu		// no emulator specific shutdown function
	))
		return 1;
	hid_init(
		lcd_key_map,
		VIRTUAL_SHIFT_POS,
		SDL_DISABLE	// no joystick HID events enabled
	);
	memset(memory, 0xFF, sizeof memory);
	memset(charrom, 0xFF, sizeof charrom);
	if (
		xemu_load_file(xemucfg_get_str("rom102"), memory + 0x38000, 0x8000, 0x8000, rom_fatal_msg) < 0 ||
		xemu_load_file(xemucfg_get_str("rom103"), memory + 0x30000, 0x8000, 0x8000, rom_fatal_msg) < 0 ||
		xemu_load_file(xemucfg_get_str("rom104"), memory + 0x28000, 0x8000, 0x8000, rom_fatal_msg) < 0 ||
		xemu_load_file(xemucfg_get_str("rom105"), memory + 0x20000, 0x8000, 0x8000, rom_fatal_msg) < 0 ||
		xemu_load_file(xemucfg_get_str("romchr"), charrom,          0x1000, 0x1000, rom_fatal_msg) < 0
	)
		return 1;
	// Ugly hacks :-( <patching ROM>
#ifdef ROM_HACK_COLD_START
	// this ROM patching is needed, as Commodore LCD seems not to work to well with "not intact" SRAM content (ie: it has battery powered SRAM even when "switched off")
	DEBUG("ROM HACK: cold start condition" NL);
	memory[0x385BB] = 0xEA;
	memory[0x385BC] = 0xEA;
#endif
#ifdef ROM_HACK_NEW_ROM_SEARCHING
	// this ROM hack modifies the ROM signature searching bytes so we can squeeze extra menu points of the main screen!
	// this hack SHOULD NOT be used, if the ROM 32K ROM images from 0x20000 and 0x28000 are not empty after offset 0x6800
	// WARNING: Commodore LCDs are known to have different ROM versions, be careful with different ROMs, if you find any!
	// [note: if you find other ROM versions, please tell me!!!! - that's the other message ...]
	DEBUG("ROM HACK: modifying ROM searching MMU table" NL);
	// overwrite MMU table positions for ROM scanner in KERNAL
	memory[0x382CC] = 0x8A;	// offset 0x6800 in the ROM image of clcd-u105.rom [phys memory address: 0x26800]
	memory[0x382CE] = 0xAA;	// offset 0x6800 in the ROM image of clcd-u104.rom [phys memory address: 0x2E800]
	// try to load "parasite" ROMs (it's not fatal if we cannot ...)
	// these loads to an unused part of the original ROM images
	xemu_load_file("#clcd-u105-parasite.rom", memory + 0x26800, 32, 0x8000 - 0x6800, NULL);
	xemu_load_file("#clcd-u104-parasite.rom", memory + 0x2E800, 32, 0x8000 - 0x6800, NULL);
#endif
	load_program_for_inject(xemucfg_get_str("prg"), 0x1001);
	rom_list();
	/* init CPU */
	cpu65_reset();	// we must do this after loading KERNAL at least, since PC is fetched from reset vector here!
	/* init VIAs */
	via_init(&via1, "VIA#1", via1_outa, via1_outb, via1_outsr, via1_ina, via1_inb, via1_insr, via1_setint);
	via_init(&via2, "VIA#2", via2_outa, via2_outb, via2_outsr, via2_ina, via2_inb, via2_insr, via2_setint);
	/* keyboard */
	clear_emu_events();	// also resets the keyboard
	keysel = 0;
	/* --- START EMULATION --- */
	cycles = 0;
	xemu_set_full_screen(xemucfg_get_bool("fullscreen"));
	if (!xemucfg_get_bool("syscon"))
		sysconsole_close(NULL);
	xemu_timekeeping_start();	// we must call this once, right before the start of the emulation
	update_rtc();			// this will use time-keeping stuff as well, so initially let's do after the function call above
	viacyc = 0;
	XEMU_MAIN_LOOP(emulation_loop, 25, 1);
	return 0;
}
