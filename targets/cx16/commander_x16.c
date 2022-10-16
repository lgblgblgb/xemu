/* Part of the Xemu project, please visit: https://github.com/lgblgblgb/xemu
   Copyright (C)2016-2022 LGB (Gábor Lénárt) <lgblgblgb@gmail.com>

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


#define	IODEBUGPRINT	DEBUGPRINT
//#define	IODEBUGPRINT	DEBUG
//#define	IODEBUGPRINT(...)

#define SCREEN_WIDTH		640
#define SCREEN_HEIGHT		480


static Uint8 lo_ram[0x9F00];
static Uint8 hi_ram[256 * 8192];	// 2M
static Uint8 rom[0x80000];		// 512K
static int   hi_ram_banks;
static int   hi_ram_access_offset, hi_ram_access_bank;
static int   hi_rom_access_offset, hi_rom_access_bank;
static Uint8 *hi_ram_p, *hi_rom_p;
static Uint8 *hi_ram_base_ptr, *hi_rom_base_ptr;	// simply the "rom" and "hi_ram". Unfortunately some C compilers are angry to set pointers directly outside of the bounds of an array, even if I WANT.

static int frameskip = 0;
static int cycles_per_scanline;
static struct Via65c22 via1, via2;		// VIA-1 and VIA-2 emulation structures


#define VIRTUAL_SHIFT_POS	0x31


static const struct KeyMappingDefault x16_key_map[] = {
	STD_XEMU_SPECIAL_KEYS,
	// **** this must be the last line: end of mapping table ****
	{ 0, -1 }
};

static struct {
	int	fullscreen, syscon, dumpmem, sdlrenderquality;
	int	hiramsize, clock;
	char	*rom;
} configdb;




static const char *cpu_where ( void )
{
	static char buffer[128];
	if (cpu65.old_pc < 0xA000)
		sprintf(buffer, "PC=[LO]:%04X", cpu65.old_pc);
	else if (cpu65.old_pc < 0xC000)
		sprintf(buffer, "PC=[HIRAM:%d]:%04X", hi_ram_access_bank, cpu65.old_pc);
	else
		sprintf(buffer, "PC=[HIROM:%d]:%04X", hi_rom_access_bank, cpu65.old_pc);
	return buffer;
}


static XEMU_INLINE void set_ram_bank ( Uint8 bank )
{
	lo_ram[0] = bank;
	hi_ram_access_bank = bank;
	hi_ram_access_offset = bank << 13;
	hi_ram_p = hi_ram_base_ptr + hi_ram_access_offset - 0xA000;	// hi_ram_p is a pointer can be dereferenced with cpu addr directly
	//DEBUGPRINT("RAM: HI-RAM access offset set to $%06X, BANK=%d @ %s" NL, hi_ram_access_offset, bank, cpu_where());
}


static XEMU_INLINE void set_rom_bank ( Uint8 bank )
{
	bank &= 31;
	lo_ram[1] = bank | (128 + 64 + 32);
	hi_rom_access_bank = bank;
	hi_rom_access_offset = bank << 14;
	hi_rom_p = hi_rom_base_ptr + hi_rom_access_offset - 0xC000;	// hi_rom_p is a pointer can be dereferenced with cpu addr directly
	DEBUGPRINT("ROM: HI-ROM access offset set to $%05X, BANK=%d @ %s" NL, hi_rom_access_offset, bank, cpu_where());
}


// Called by CPU emulation code when any kind of memory byte must be written.
void  cpu65_write_callback ( Uint16 addr, Uint8 data )
{
	/**** 0000-9EFF: low RAM (fixed RAM) area ****/
	if (addr < 0x9F00) {
		if (XEMU_LIKELY(addr & 0xFFFE)) {
			lo_ram[addr] = data;
			return;
		}
		if (!addr)
			set_ram_bank(data);
		else
			set_rom_bank(data);
		return;
	}
	/**** 9F00-9FFF: I/O area ****/
	if (addr < 0xA000) {
		switch ((addr >> 4) & 0xF) {
			case 0x0:
				//IODEBUGPRINT("IO_W: writing to reg $%04X (data=$%02X), VIA-1 @ %s" NL, addr, data, cpu_where());
				via_write(&via1, addr & 0xF, data);
				break;
			case 0x1:
				IODEBUGPRINT("IO_W: writing to reg $%04X (data=$%02X), VIA-2 @ %s" NL, addr, data, cpu_where());
				via_write(&via2, addr & 0xF, data);
				break;
			case 0x2:
			case 0x3:
				IODEBUGPRINT("IO_W: writing to reg $%04X (data=$%02X), VERA video controller @ %s" NL, addr, data, cpu_where());
				vera_write_cpu_register(addr, data);	// VERA masks the addr bits, so it's OK.
				break;
			default:
				IODEBUGPRINT("IO_W: writing to reg $%04X (data=$%02X), UNKNOWN/RESERVED @ %s" NL, addr, data, cpu_where());
				break;
		}
		return;
	}
	/**** A000-BFFF: 8K pageable RAM area, "hi-RAM" ****/
	if (XEMU_LIKELY(addr < 0xC000)) {
		if (XEMU_LIKELY(hi_ram_access_bank <= hi_ram_banks))
			hi_ram_p[addr] = data;
		return;
	}
	// Others (ROM) can be ignored, since it's ROM (not writable anyway) ...
	DEBUGPRINT("ROM: trying to write ROM at address $%04X (in bank %d) @ %s" NL, addr, hi_ram_access_bank, cpu_where());
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
				data = via_read(&via1, addr & 0xF);
				//IODEBUGPRINT("IO_R: reading from reg $%04X (data=$%02X), VIA-1 @ %s" NL, addr, data, cpu_where());
				break;
			case 0x1:
				data = via_read(&via2, addr & 0xF);
				IODEBUGPRINT("IO_R: reading from reg $%04X (data=$%02X), VIA-2 @ %s" NL, addr, data, cpu_where());
				break;
			case 0x2:
			case 0x3:
				data = vera_read_cpu_register(addr);	// VERA masks the addr bits, so it's OK
				IODEBUGPRINT("IO_R: reading from reg $%04X (data=$%02X), VERA video controller @ %s" NL, addr, data, cpu_where());
				break;
			default:
				data = 0x00;
				IODEBUGPRINT("IO_R: reading from reg $%04X (data=$%02X), UNKNOWN/RESERVED @ %s" NL, addr, data, cpu_where());
				break;
		}
		return data;
	}
	/**** A000-BFFF: 8K pageable RAM area, "hi-RAM" ****/
	if (addr < 0xC000)
		return hi_ram_p[addr];
	/**** C000-FFFF: the rest, 16K pagable ROM area ****/
	return hi_rom_p[addr];
}





static int load_rom ( const char *fn )
{
	memset(rom, 0xFF, sizeof rom);
	const int ret = xemu_load_file(fn, rom, 16384, sizeof rom, "Cannot load ROM");
	if (ret <= 0)
		return 1;
	if (ret % 16384) {
		ERROR_WINDOW("Bad ROM image, size must be multiple of 16384 bytes:\n%s", xemu_load_filepath);
		return 1;
	}
	DEBUGPRINT("ROM: system ROM loaded, %d bytes, %d (16K) pages, source: %s" NL, ret, ret / 16384, xemu_load_filepath);
	set_rom_bank(0);
	return 0;
}


static void init_ram ( int hi_ram_size )
{
	hi_rom_base_ptr = rom;
	hi_ram_base_ptr = hi_ram;
	if (hi_ram_size > 2048)
		hi_ram_size = 2048;
	if (hi_ram_size < 0)
		hi_ram_size = 0;
	hi_ram_banks = hi_ram_size >> 3;
	DEBUGPRINT("RAM: Setting (hi-)RAM size memtop to %dK, %d banks." NL, hi_ram_size, hi_ram_banks);
	memset(lo_ram, 0, sizeof lo_ram);
	memset(hi_ram, 0xFF, sizeof hi_ram);
	set_ram_bank(0);
	set_rom_bank(0);
}



/* VIA emulation callbacks, called by VIA core. See main() near to via_init() calls for further information */

static void via1_outa ( Uint8 mask, Uint8 data )
{
	//DEBUGPRINT("VIA OUTA (RAM BANK) SET mask=%d data=%d" NL, (int)mask, (int)data);
	//set_ram_bank(data);
}

static Uint8 via1_ina ( Uint8 mask )
{
	//DEBUGPRINT("READING VIA1-A (RAM BANK): $%02X" NL, via1.ORA);
	//return via1.ORA;
	return read_ps2_port() | (0xFF - 3);
}

static void via1_outb ( Uint8 mask, Uint8 data )
{
	//DEBUGPRINT("VIA OUTB (ROM BANK) SET mask=%d data=%d" NL, (int)mask, (int)data);
	//set_rom_bank(data);
}

static Uint8 via1_inb ( Uint8 mask )
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
	return via2.ORA;
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
		all_virt_cycles += opcyc;
		if (cycles >= cycles_per_scanline) {
			if (!frameskip) {
			}
			if (vera_render_line() == 0) {	// start of a new frame that is ...
				update_emulator();
				//vera_vsync();
				frameskip = !frameskip;
				return;
			}
			cycles -= cycles_per_scanline;
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
	if (configdb.dumpmem) {
		vera_dump_vram("vram.dump");
		dump_stuff("loram.dump", lo_ram, sizeof lo_ram);
		if (hi_ram_banks)
			dump_stuff("hiram.dump", hi_ram, hi_ram_banks << 13);
	}
}


static void reset_machine ( void )
{
	set_rom_bank(0);
	set_ram_bank(0);
	cpu65_reset();
}


int main ( int argc, char **argv )
{
	xemu_pre_init(APP_ORG, TARGET_NAME, "The Surprising Commander X16 emulator from LGB", argc, argv);
	xemucfg_define_switch_option("fullscreen", "Start in fullscreen mode", &configdb.fullscreen);
	xemucfg_define_str_option("rom", ROM_NAME, "Sets character ROM to use", &configdb.rom);
	xemucfg_define_num_option("hiramsize", 2048, "Size of high-RAM in Kbytes", &configdb.hiramsize, 512, 2048);
	xemucfg_define_num_option("clock", 8, "CPU frequency in MHz [1..8]", &configdb.clock, 1, 8);
	xemucfg_define_switch_option("syscon", "Keep system console open (Windows-specific effect only)", &configdb.syscon);
	xemucfg_define_switch_option("dumpmem", "Dump memory states on exit into files", &configdb.dumpmem);
	xemucfg_define_num_option("sdlrenderquality", RENDER_SCALE_QUALITY, "Setting SDL hint for scaling method/quality on rendering (0, 1, 2)", &configdb.sdlrenderquality, 0, 2 );
	if (xemucfg_parse_all())
		return 1;
	/* Initiailize SDL - note, it must be before loading ROMs, as it depends on path info from SDL! */
	if (xemu_post_init(
		TARGET_DESC APP_DESC_APPEND,	// window title
		1,				// resizable window
		SCREEN_WIDTH, SCREEN_HEIGHT,	// texture sizes
		SCREEN_WIDTH, SCREEN_HEIGHT,	// logical size
		SCREEN_WIDTH, SCREEN_HEIGHT,	// window size
		SCREEN_FORMAT,			// pixel format
		0,				// no predef colours at this point
		NULL,				// initialize palette from this constant array
		NULL,				// initialize palette into this stuff
		configdb.sdlrenderquality,	// render scaling quality
		USE_LOCKED_TEXTURE,		// 1 = locked texture access
		emulator_shutdown		// shutdown function
	))
		return 1;
	hid_init(
		x16_key_map,
		VIRTUAL_SHIFT_POS,
		SDL_ENABLE		// enable HID joy events
	);
	// --- memory initialization ---
	init_ram(configdb.hiramsize);
	if (load_rom(configdb.rom))
		return 1;
	// Continue with initializing ...
	cycles_per_scanline = configdb.clock * 1000000 / 525 / 60;
	DEBUGPRINT("Clock requested %d MHz, %d cycles per scanline" NL, configdb.clock, cycles_per_scanline);
	vera_init();
	clear_emu_events();	// also resets the keyboard
	// Initiailize VIAs.
	// Note: this is my unfinished VIA emulation skeleton, for my Commodore LCD emulator originally, ported from my JavaScript code :)
	// it uses callback functions, which must be registered here, NULL values means unused functionality
	via_init(&via1, "VIA-1",
		via1_outa,	// outa
		via1_outb,	// outb
		NULL,		// outsr
		via1_ina,	// ina
		via1_inb,	// inb
		NULL,		// insr
		via1_setint
	);
	via_init(&via2, "VIA-2",
		NULL,			// outa [reg 1]
		NULL,
		NULL,	// outsr
		NULL,
		NULL,	//via2_inb,
		NULL,	// insr
		via2_setint
	);
	reset_machine();
	// Without these, the first DDR register writes would cause problems, since not OR* (Output Register) is written first ...
	//via1.ORA = 0xFF;
	//via1.ORB = 0xFF;
	//via2.ORA = 0xFF;
	//via2.ORB = 0xFF;
	cycles = 0;
	xemu_set_full_screen(configdb.fullscreen);
	if (!configdb.syscon)
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
