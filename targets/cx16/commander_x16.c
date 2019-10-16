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
#include "input_devices.h"


//#define	IODEBUGPRINT	DEBUGPRINT
#define	IODEBUGPRINT	DEBUG
//#define	IODEBUGPRINT(...)

#define SCREEN_WIDTH		640
#define SCREEN_HEIGHT		480


// TODO: kill these
//#define LAST_SCANLINE		325
#define CYCLES_PER_SCANLINE	254

static Uint8 lo_ram[0x9F00];
static Uint8 hi_ram[256 * 8192];	// 2M
static Uint8 rom[0x20000];		// 128K
static int   hi_ram_banks;
static int   hi_ram_access_offset;
static int   hi_rom_access_offset;

static int frameskip = 0;
static struct Via65c22 via1, via2;		// VIA-1 and VIA-2 emulation structures


#define VIRTUAL_SHIFT_POS	0x31


static const struct KeyMappingDefault x16_key_map[] = {
	STD_XEMU_SPECIAL_KEYS,
	// **** this must be the last line: end of mapping table ****
	{ 0, -1 }
};





// Called by CPU emulation code when any kind of memory byte must be written.
void  cpu65_write_callback ( Uint16 addr, Uint8 data )
{
	/**** 0000-9EFF: low RAM (fixed RAM) area ****/
	if (addr < 0x9F00) {
		lo_ram[addr] = data;
		return;
	}
	/**** 9F00-9FFF: I/O area ****/
	if (addr < 0xA000) {
		switch ((addr >> 4) & 0xF) {
			case 0x0:
			case 0x1:
				IODEBUGPRINT("IO_W: writing to reg $%04X (data=$%02X), audio controller @ PC=$%04X" NL, addr, data, cpu65.old_pc);
				break;
			case 0x2:
			case 0x3:
				IODEBUGPRINT("IO_W: writing to reg $%04X (data=$%02X), VERA video controller @ PC=$%04X" NL, addr, data, cpu65.old_pc);
				vera_write_cpu_register(addr, data);	// VERA masks the addr bits, so it's OK.
				break;
			case 0x6:
				IODEBUGPRINT("IO_W: writing to reg $%04X (data=$%02X), VIA-1 @ PC=$%04X" NL, addr, data, cpu65.old_pc);
				via_write(&via1, addr & 0xF, data);
				break;
			case 0x7:
				IODEBUGPRINT("IO_W: writing to reg $%04X (data=$%02X), VIA-2 @ PC=$%04X" NL, addr, data, cpu65.old_pc);
				via_write(&via2, addr & 0xF, data);
				break;
			case 0x8:
			case 0x9:
				IODEBUGPRINT("IO_W: writing to reg $%04X (data=$%02X), RTC @ PC=$%04X" NL, addr, data, cpu65.old_pc);
				break;
			default:
				IODEBUGPRINT("IO_W: writing to reg $%04X (data=$%02X), UNKNOWN/RESERVED @ PC=$%04X" NL, addr, data, cpu65.old_pc);
				break;
		}
		return;
	}
	/**** A000-BFFF: 8K pageable RAM area, "hi-RAM" ****/
	if (XEMU_LIKELY(addr < 0xC000)) {
		hi_ram[hi_ram_access_offset + addr - 0xA000] = data;
		return;
	}
	// Others (ROM) can be ignored, since it's ROM (not writable anyway) ...
}




// Called by CPU emulation code when any kind of memory byte must be read.
Uint8 cpu65_read_callback ( Uint16 addr )
{
	/**** 0000-9EFF: low RAM (fixed RAM) area ****/
	if (addr < 0x9F00)
		return lo_ram[addr];
	/**** 9F00-9FFF: I/O area ****/
	if (addr < 0xA000) {
		Uint8 data = 0xFF;
		switch ((addr >> 4) & 0xF) {
			case 0x0:
			case 0x1:
				IODEBUGPRINT("IO_R: reading from reg $%04X (data=$%02X), audio controller @ PC=$%04X" NL, addr, data, cpu65.old_pc);
				break;
			case 0x2:
			case 0x3:
				data = vera_read_cpu_register(addr);	// VERA masks the addr bits, so it's OK
				IODEBUGPRINT("IO_R: reading from reg $%04X (data=$%02X), VERA video controller @ PC=$%04X" NL, addr, data, cpu65.old_pc);
				break;
			case 0x6:
				data = via_read(&via1, addr & 0xF);
				IODEBUGPRINT("IO_R: reading from reg $%04X (data=$%02X), VIA-1 @ PC=$%04X" NL, addr, data, cpu65.old_pc);
				break;
			case 0x7:
				data = via_read(&via2, addr & 0xF);
				IODEBUGPRINT("IO_R: reading from reg $%04X (data=$%02X), VIA-2 @ PC=$%04X" NL, addr, data, cpu65.old_pc);
				break;
			case 0x8:
			case 0x9:
				IODEBUGPRINT("IO_R: reading from reg $%04X (data=$%02X), RTC @ PC=$%04X" NL, addr, data, cpu65.old_pc);
				break;
			default:
				IODEBUGPRINT("IO_R: reading from reg $%04X (data=$%02X), UNKNOWN/RESERVED @ PC=$%04X" NL, addr, data, cpu65.old_pc);
				break;
		}
		return data;
	}
	/**** A000-BFFF: 8K pageable RAM area, "hi-RAM" ****/
	if (addr < 0xC000)
		return hi_ram[hi_ram_access_offset + addr - 0xA000];
	/**** C000-FFFF: the rest, 16K pagable ROM area ****/
	return rom[hi_rom_access_offset + addr - 0xC000];
}


static XEMU_INLINE void set_rom_bank ( Uint8 bank )
{
	bank &= 7;
	hi_rom_access_offset = bank << 14;
	//DEBUGPRINT("HI-ROM access offset set to $%05X, BANK=%d @ PC=$%04X" NL, hi_rom_access_offset, bank, cpu65.old_pc);
}

static XEMU_INLINE void set_ram_bank ( Uint8 bank )
{
	hi_ram_access_offset = (bank % hi_ram_banks) << 13;
	//DEBUGPRINT("HI-RAM access offset set to $%06X, BANK=%d @ PC=$%04X" NL, hi_ram_access_offset, bank, cpu65.old_pc);
}



static int load_rom ( const char *fn )
{
	if (xemu_load_file(fn, rom, sizeof rom, sizeof rom, "Cannot load ROM") != sizeof rom)
		return 1;
	set_rom_bank(0);
	return 0;
}


static void init_ram ( int hi_ram_size )
{
	if (hi_ram_size > 2048)
		hi_ram_size = 2048;
	if (hi_ram_size < 0)
		hi_ram_size = 0;
	hi_ram_banks = hi_ram_size >> 3;
	DEBUGPRINT("Setting (hi-)RAM size memtop to %dK, %d banks." NL, hi_ram_size, hi_ram_banks);
	memset(lo_ram, 0, sizeof lo_ram);
	memset(hi_ram, 0xFF, sizeof hi_ram);
	set_ram_bank(0xFF);
}



/* VIA emulation callbacks, called by VIA core. See main() near to via_init() calls for further information */

static void via1_outa_ram_bank ( Uint8 mask, Uint8 data )
{
	//DEBUGPRINT("VIA OUTA (RAM BANK) SET mask=%d data=%d" NL, (int)mask, (int)data);
	set_ram_bank(data);
}

static Uint8 via1_ina_ram_bank ( Uint8 mask )
{
	//DEBUGPRINT("READING VIA1-A (RAM BANK): $%02X" NL, via1.ORA);
	return via1.ORA;
}

static void via1_outb_rom_bank ( Uint8 mask, Uint8 data )
{
	//DEBUGPRINT("VIA OUTB (ROM BANK) SET mask=%d data=%d" NL, (int)mask, (int)data);
	set_rom_bank(data);
}

static Uint8 via1_inb_rom_bank ( Uint8 mask )
{
	//DEBUGPRINT("READING VIA1-B (ROM BANK): $%02X" NL, via1.ORB);
	return via1.ORB;
}



static void via1_setint ( int level )
{
	if (level)
		cpu65.irqLevel |=  0x100;
	else
		cpu65.irqLevel &= ~0x100;
}


static void via2_setint ( int level )
{
	if (level)
		cpu65.irqLevel |=  0x200;
	else
		cpu65.irqLevel &= ~0x200;
}


static Uint8 via2_ina ( Uint8 mask )
{
	//DEBUGPRINT("READING VIA2-A, DDR mask is: $%02X output register is $%02X" NL, via2.DDRA, via2.ORA);
	return read_ps2_port() | (0xFF - 3);
}


static void update_emulator ( void )
{
	if (!frameskip) {
		// First: update screen ...
		xemu_update_screen();
		// Second: we must handle SDL events waiting for us in the event queue ...
		hid_handle_all_sdl_events();
		// Third: Sleep ... Please read emutools.c source about this madness ... 40000 is (PAL) microseconds for a full frame to be produced
		xemu_timekeeping_delay(FULL_FRAME_USECS);
	}
	//vic_vsync(!frameskip);	// prepare for the next frame!
}



static int cycles;
Uint64 all_virt_cycles = 0;


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
		all_virt_cycles += opcyc;	// FIXME: should be scaled for different CPU speeds, but then also CYCLES_PER_SECOND should be altered for the desired CPU speed!!!
		if (cycles >= CYCLES_PER_SCANLINE) {
			if (!frameskip) {
			}
			if (vera_render_line() == 0) {	// start of a new frame that is ...
				update_emulator();
				//vera_vsync();
				frameskip = !frameskip;
				return;
			}
			cycles -= CYCLES_PER_SCANLINE;
		}
	}
}



int dump_stuff ( const char *fn, void *mem, int size )
{
	DEBUGPRINT("DUMPMEM: dumping %d bytes at %p into file %s" NL, size, mem, fn);
	FILE *f = fopen(fn, "w");
	if (f) {
		int r = fwrite(mem, size, 1, f) != 1;
		fclose(f);
		if (r) {
			DEBUGPRINT("DUMPMEM: cannot write file" NL);
			unlink(fn);
		}
		return r;
	}
	DEBUGPRINT("DUMPMEM: cannot create file" NL);
	return 1;
}


static void emulator_shutdown ( void )
{
	if (xemucfg_get_bool("dumpmem")) {
		vera_dump_vram("vram.dump");
		dump_stuff("loram.dump", lo_ram, sizeof lo_ram);
		if (hi_ram_banks)
			dump_stuff("hiram.dump", hi_ram, hi_ram_banks << 13);
	}
}



int main ( int argc, char **argv )
{
	xemu_pre_init(APP_ORG, TARGET_NAME, "The Surprising Commander X16 emulator from LGB");
	xemucfg_define_switch_option("fullscreen", "Start in fullscreen mode");
	xemucfg_define_str_option("rom", ROM_NAME, "Sets character ROM to use");
	xemucfg_define_num_option("hiramsize", 2048, "Size of high-RAM in Kbytes");
	xemucfg_define_num_option("clock", 8, "CPU frequency in MHz [1..8]");
	xemucfg_define_switch_option("syscon", "Keep system console open (Windows-specific effect only)");
	xemucfg_define_switch_option("dumpmem", "Dump memory states on exit into files");
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
		emulator_shutdown	// shutdown function
	))
		return 1;
	hid_init(
		x16_key_map,
		VIRTUAL_SHIFT_POS,
		SDL_ENABLE		// enable HID joy events
	);
	// --- memory initialization ---
	init_ram(xemucfg_get_num("hiramsize"));
	vera_init();
	if (
		load_rom(xemucfg_get_str("rom"))
	)
		return 1;
	// Continue with initializing ...
	clear_emu_events();	// also resets the keyboard
	cpu65_reset();	// reset CPU: it must be AFTER kernal is loaded at least, as reset also fetches the reset vector into PC ...
	// Initiailize VIAs.
	// Note: this is my unfinished VIA emulation skeleton, for my Commodore LCD emulator originally, ported from my JavaScript code :)
	// it uses callback functions, which must be registered here, NULL values means unused functionality
	via_init(&via1, "VIA-1",
		via1_outa_ram_bank,	// outa
		via1_outb_rom_bank,	// outb
		NULL,	// outsr
		via1_ina_ram_bank,	// ina
		via1_inb_rom_bank,	// inb
		NULL,	// insr
		via1_setint
	);
	via_init(&via2, "VIA-2",
		NULL,			// outa [reg 1]
		NULL, //via2_kbd_set_scan,	// outb [reg 0], we wire port B as output to set keyboard scan, HOWEVER, we use ORB directly in get scan!
		NULL,	// outsr
		via2_ina,	// ina  [reg 1], we wire port A as input to get the scan result, which was selected with port-A
		NULL, //via2_inb,
		NULL,	// insr
		via2_setint
	);
	// Without these, the first DDR register writes would cause problems, since not OR* (Output Register) is written first ...
	//via1.ORA = 0xFF;
	//via1.ORB = 0xFF;
	//via2.ORA = 0xFF;
	//via2.ORB = 0xFF;
	cycles = 0;
	xemu_set_full_screen(xemucfg_get_bool("fullscreen"));
	if (!xemucfg_get_bool("syscon"))
		sysconsole_close(NULL);
	xemu_timekeeping_start();	// we must call this once, right before the start of the emulation
	//vera_vsync();
#ifdef __EMSCRIPTEN__
	// http://xemu-dist.lgb.hu/dist/x16/xemu-xcx16-sample.html?t=11
	close(1);
	close(2);
#endif
	DEBUGPRINT("CPU: starting exection at $%04X" NL, cpu65.pc);
	XEMU_MAIN_LOOP(emulation_loop, 30, 1);
	return 0;
}
