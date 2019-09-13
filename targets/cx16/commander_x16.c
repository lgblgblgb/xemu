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
#include "xemu/emutools_files.h"
#include "xemu/emutools_hid.h"
#include "xemu/emutools_config.h"
#include "commander_x16.h"
#include "xemu/cpu65.h"
#include "xemu/via65c22.h"
#include "vera.h"

#define SCREEN_WIDTH		640
#define SCREEN_HEIGHT		480


// TODO: kill these
#define LAST_SCANLINE		325
#define CYCLES_PER_SCANLINE	71

static Uint8 lo_ram[0x9F00];
static Uint8 hi_ram[256 * 8192];
static Uint8 rom[9 * 8192];		// there are 8 banks, but kernal has one too!
static int   hi_ram_access_offset;
static int   hi_rom_access_offset;

static int   scanline = 0;


static int frameskip = 0;
static struct Via65c22 via1, via2;		// VIA-1 and VIA-2 emulation structures


#define VIRTUAL_SHIFT_POS	0x31


static const struct KeyMappingDefault x16_key_map[] = {
	// -- the following definitions are not VIC-20 keys, but emulator related stuffs
	STD_XEMU_SPECIAL_KEYS,
	//{ SDL_SCANCODE_ESCAPE,		0x81 },	// RESTORE key
	// **** this must be the last line: end of mapping table ****
	{ 0, -1 }
};






void clear_emu_events ( void )
{
	hid_reset_events(1);
}



// Called by CPU emulation code when any kind of memory byte must be written.
void  cpu65_write_callback ( Uint16 addr, Uint8 data )
{
	if (addr < 0x9F00) {
		lo_ram[addr] = data;
		return;
	}
	if (addr < 0xA000) {	// I/O stuff
		switch ((addr >> 4) & 0xF) {
			case 0x0:
			case 0x1:
				DEBUGPRINT("IO_W: writing to reg $%04X (data=$%02X), audio controller @ PC=$%04X" NL, addr, data, cpu65.old_pc);
				break;
			case 0x2:
			case 0x3:
				DEBUGPRINT("IO_W: writing to reg $%04X (data=$%02X), VERA video controller @ PC=$%04X" NL, addr, data, cpu65.old_pc);
				vera_write_reg_by_cpu(addr, data);	// VERA masks the addr bits, so it's OK.
				break;
			case 0x6:
				DEBUGPRINT("IO_W: writing to reg $%04X (data=$%02X), VIA-1 @ PC=$%04X" NL, addr, data, cpu65.old_pc);
				via_write(&via1, addr & 0xF, data);
				break;
			case 0x7:
				DEBUGPRINT("IO_W: writing to reg $%04X (data=$%02X), VIA-2 @ PC=$%04X" NL, addr, data, cpu65.old_pc);
				via_write(&via2, addr & 0xF, data);
				break;
			case 0x8:
			case 0x9:
				DEBUGPRINT("IO_W: writing to reg $%04X (data=$%02X), RTC @ PC=$%04X" NL, addr, data, cpu65.old_pc);
				break;
			default:
				DEBUGPRINT("IO_W: writing to reg $%04X (data=$%02X), UNKNOWN/RESERVED @ PC=$%04X" NL, addr, data, cpu65.old_pc);
				break;
		}
		return;
	}
	if (addr < 0xC000) {
		hi_ram[hi_ram_access_offset - 0xA000] = data;
		return;
	}
	// Others (hi-rom & kernal-rom) can be ignored, since it's ROM ...
}




// Called by CPU emulation code when any kind of memory byte must be read.
Uint8 cpu65_read_callback ( Uint16 addr )
{
	if (addr < 0x9F00)
		return lo_ram[addr];
	if (addr < 0xA000) {	// I/O stuff
		Uint8 data = 0xFF;
		switch ((addr >> 4) & 0xF) {
			case 0x0:
			case 0x1:
				DEBUGPRINT("IO_R: reading from reg $%04X (data=$%02X), audio controller @ PC=$%04X" NL, addr, data, cpu65.old_pc);
				break;
			case 0x2:
			case 0x3:
				data = vera_read_reg_by_cpu(addr);	// VERA masks the addr bits, so it's OK
				DEBUGPRINT("IO_R: reading from reg $%04X (data=$%02X), VERA video controller @ PC=$%04X" NL, addr, data, cpu65.old_pc);
				break;
			case 0x6:
				data = via_read(&via1, addr & 0xF);
				DEBUGPRINT("IO_R: reading from reg $%04X (data=$%02X), VIA-1 @ PC=$%04X" NL, addr, data, cpu65.old_pc);
				break;
			case 0x7:
				data = via_read(&via2, addr & 0xF);
				DEBUGPRINT("IO_R: reading from reg $%04X (data=$%02X), VIA-2 @ PC=$%04X" NL, addr, data, cpu65.old_pc);
				break;
			case 0x8:
			case 0x9:
				DEBUGPRINT("IO_R: reading from reg $%04X (data=$%02X), RTC @ PC=$%04X" NL, addr, data, cpu65.old_pc);
				break;
			default:
				DEBUGPRINT("IO_R: reading from reg $%04X (data=$%02X), UNKNOWN/RESERVED @ PC=$%04X" NL, addr, data, cpu65.old_pc);
				break;
		}
		return data;
	}
	if (addr < 0xC000)
		return hi_ram[hi_ram_access_offset - 0xA000];
	if (addr < 0xE000) {
		//fprintf(stderr, "HI ROM ACCESS: %d\n", hi_rom_access_offset);
		return rom[hi_rom_access_offset - 0xC000 + addr];
	}
	return rom[0x2000 + addr - 0xE000];
}


static XEMU_INLINE void set_rom_bank ( Uint8 bank )
{
	bank &= 7;
	hi_rom_access_offset = bank ? (bank + 1) << 13 : 0;
	//DEBUGPRINT("HI-ROM access offset set to %d, BANK=%d" NL, hi_rom_access_offset, bank);
}


static int load_rom ( const char *fn )
{
	// min ROM image size is 2*8192 since we need the fixed-ROM KERNAL [8K] and at least something (BASIC) as the paged ROM area.
	// max ROM image size is 9*8192, because of the 8*8K ROM pages + 8K kernal
	// actually the last 8K seems tbe the KERNAL, so we copy things that way ...
	int size = xemu_load_file(fn, rom, 2 * 8192, 9 * 8192, "Cannot load ROM");
	if (size < 0)
		return size;
	if (size < 9 * 8192)
		memset(rom + size, 0xFF, 9 * 8192 - size);
	set_rom_bank(7);
	return 0;
}

static void init_ram ( void )
{
	memset(lo_ram, 0xFF, sizeof lo_ram);
	memset(hi_ram, 0xFF, sizeof hi_ram);
	hi_ram_access_offset = 255 * 8192;
}



// HID needs this to be defined, it's up to the emulator if it uses or not ...
int emu_callback_key ( int pos, SDL_Scancode key, int pressed, int handled )
{
	return 0;
}



/* VIA emulation callbacks, called by VIA core. See main() near to via_init() calls for further information */

static void via1_outa_ram_bank ( Uint8 mask, Uint8 data )
{
	int bank = data;
	hi_ram_access_offset = bank << 13;
	DEBUGPRINT("MEM: setting HI-RAM bank to #$%02X (offset=%d)\n", bank, hi_ram_access_offset);
}


static void via1_outb_rom_bank ( Uint8 mask, Uint8 data )
{
	int bank = data & 7;
	set_rom_bank(bank);
	DEBUGPRINT("MEM: setting HI-ROM bank to #$%02X (offset=%d)\n", bank, hi_rom_access_offset);
}



static void via1_setint ( int level )
{
	if (level)
		cpu65.irqLevel |= 0x40;
	else
		cpu65.irqLevel &= ~0x40;
}


static void via2_setint ( int level )
{
	if (level)
		cpu65.irqLevel |= 0x80;
	else
		cpu65.irqLevel &= ~0x80;
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


static Uint8 via1_ina ( Uint8 mask )
{
	// joystick state (RIGHT direction is not handled here though)
	return
		hid_read_joystick_left  (0, 1 << 4) |
		hid_read_joystick_up    (0, 1 << 2) |
		hid_read_joystick_down  (0, 1 << 3) |
		hid_read_joystick_button(0, 1 << 5)
	;
}


static Uint8 via2_inb ( Uint8 mask )
{
	// Port-B in VIA2 is used (temporary with DDR-B set to input) to scan joystick direction 'RIGHT'
	return hid_read_joystick_right(0x7F, 0xFF);
}


static void update_emulator ( void )
{
	if (!frameskip) {
		// First: update VIC-20 screen ...
		xemu_update_screen();
		// Second: we must handle SDL events waiting for us in the event queue ...
		hid_handle_all_sdl_events();
		// Third: Sleep ... Please read emutools.c source about this madness ... 40000 is (PAL) microseconds for a full frame to be produced
		xemu_timekeeping_delay(FULL_FRAME_USECS);
	}
	//vic_vsync(!frameskip);	// prepare for the next frame!
}



static int cycles;
Uint64 all_virt_cycles;


static void emulation_loop ( void )
{
	for (;;) { // our emulation loop ...
		int opcyc;
		//printf("PC=%04X OPC=%02X\n", cpu65.pc, cpu65_read_callback(cpu65.pc));
		opcyc = cpu65_step();	// execute one opcode (or accept IRQ, etc), return value is the used clock cycles
		via_tick(&via1, opcyc);	// run VIA-1 tasks for the same amount of cycles as the CPU
		via_tick(&via2, opcyc);	// -- "" -- the same for VIA-2
		//opcyc <<= speed_shifter;
		cycles += opcyc;
		if (cycles >= CYCLES_PER_SCANLINE) {	// if [at least!] 71 (on PAL) CPU cycles passed then render a VIC-I scanline, and maintain scanline value + texture/SDL update (at the end of a frame)
			// render one (scan)line. Note: this is INACCURATE, we should do rendering per dot clock/cycle or something,
			// but for a simple emulator like this, it's already acceptable solultion, I think!
			// Note about frameskip: we render only every second (half) frame, no interlace (PAL VIC), not so correct, but we also save some resources this way
			if (!frameskip) {
				//vic_render_line();
			}
			if (vera_render_line()) {
				update_emulator();
				vera_vsync();
				frameskip = !frameskip;
				return;
			} else
				scanline++;
			cycles -= CYCLES_PER_SCANLINE;
		}
	}
}



int main ( int argc, char **argv )
{
	xemu_pre_init(APP_ORG, TARGET_NAME, "The Surprising Commander X16 emulator from LGB");
	xemucfg_define_switch_option("fullscreen", "Start in fullscreen mode");
	xemucfg_define_str_option("rom", ROM_NAME, "Sets character ROM to use");
	xemucfg_define_str_option("chrrom",  CHR_ROM_NAME, "Sets BASIC ROM to use");
	xemucfg_define_switch_option("syscon", "Keep system console open (Windows-specific effect only)");
	if (xemucfg_parse_all(argc, argv))
		return 1;
	/* Initiailize SDL - note, it must be before loading ROMs, as it depends on path info from SDL! */
	if (xemu_post_init(
		TARGET_DESC APP_DESC_APPEND,	// window title
		1,				// resizable window
		SCREEN_WIDTH, SCREEN_HEIGHT,	// texture sizes
		SCREEN_WIDTH, SCREEN_HEIGHT,	// logical size
		SCREEN_WIDTH, SCREEN_HEIGHT,	// window size
		SCREEN_FORMAT,		// pixel format
		0,			// we have 16 colours
		NULL,			// initialize palette from this constant array
		NULL,			// initialize palette into this stuff
		RENDER_SCALE_QUALITY,	// render scaling quality
		USE_LOCKED_TEXTURE,	// 1 = locked texture access
		NULL			// no emulator specific shutdown function
	))
		return 1;
	hid_init(
		x16_key_map,
		VIRTUAL_SHIFT_POS,
		SDL_ENABLE		// enable HID joy events
	);
	// --- memory initialization ---
	init_ram();
	vera_init(); // must be before vera_load_rom(), since it clears the charrom as well!
	if (
		load_rom(xemucfg_get_str("rom")) ||
		vera_load_rom(xemucfg_get_str("chrrom"))
	)
		FATAL("You need the ROM images ...");
	// Continue with initializing ...
	clear_emu_events();	// also resets the keyboard
	cpu65_reset();	// reset CPU: it must be AFTER kernal is loaded at least, as reset also fetches the reset vector into PC ...
	// Initiailize VIAs.
	// Note: this is my unfinished VIA emulation skeleton, for my Commodore LCD emulator originally, ported from my JavaScript code :)
	// it uses callback functions, which must be registered here, NULL values means unused functionality
	via_init(&via1, "VIA-1",	// from $9110 on VIC-20
		via1_outa_ram_bank,	// outa
		via1_outb_rom_bank,	// outb
		NULL,	// outsr
		NULL,	// ina
		NULL,	// inb
		NULL,	// insr
		via1_setint	// setint, called by via core, if interrupt level changed for whatever reason (ie: expired timer ...). It is wired to NMI on VIC20.
	);
	via_init(&via2, "VIA-2",	// from $9120 on VIC-20
		NULL,			// outa [reg 1]
		NULL, //via2_kbd_set_scan,	// outb [reg 0], we wire port B as output to set keyboard scan, HOWEVER, we use ORB directly in get scan!
		NULL,	// outsr
		via2_kbd_get_scan,	// ina  [reg 1], we wire port A as input to get the scan result, which was selected with port-A
		via2_inb,		// inb  [reg 0], used with DDR set to input for joystick direction 'right' in VIC20
		NULL,	// insr
		via2_setint	// setint, same for VIA2 as with VIA1, but this is wired to IRQ on VIC20.
	);
	cycles = 0;
	xemu_set_full_screen(xemucfg_get_bool("fullscreen"));
	if (!xemucfg_get_bool("syscon"))
		sysconsole_close(NULL);
	xemu_timekeeping_start();	// we must call this once, right before the start of the emulation
	vera_vsync();
#ifdef __EMSCRIPTEN__
	// http://xemu-dist.lgb.hu/dist/x16/xemu-xcx16-sample.html?t=11
	close(1);
	close(2);
#endif
	XEMU_MAIN_LOOP(emulation_loop, 25, 1);
	return 0;
}
