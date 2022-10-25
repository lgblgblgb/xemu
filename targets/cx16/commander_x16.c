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

// 1'000'000 / 60
#define FULL_FRAME_USECS	16667

#define	HI_RAM_MAX_SIZE		0x200000
#define	HI_ROM_MAX_SIZE		0x80000
#define LO_MEM_SIZE		0xA000
#define	LO_MEM_REAL_SIZE	(LO_MEM_SIZE - 0x100)

#define	MEM_STORAGE_SIZE	(LO_MEM_SIZE + HI_RAM_MAX_SIZE + HI_ROM_MAX_SIZE)
static Uint8 memory_storage[MEM_STORAGE_SIZE];
static int memconfig_ready = 0;
#define lo_ram_ptr	memory_storage
#define	hi_ram_abs_ptr	(memory_storage + LO_MEM_SIZE)
#define	hi_rom_abs_ptr	(memory_storage + LO_MEM_SIZE + HI_RAM_MAX_SIZE)
#define hi_ram_rel_ptr	(memory_storage + LO_MEM_SIZE - 0xA000)
#define hi_rom_rel_ptr	(memory_storage + LO_MEM_SIZE + HI_RAM_MAX_SIZE - 0xC000)
static Uint8 *hi_ram_p, *hi_rom_p;
static int hi_ram_access_bank, hi_rom_access_bank;
static int hi_ram_banks;

static int cycles_per_scanline;
static struct Via65c22 via1, via2;		// VIA-1 and VIA-2 emulation structures

Uint64 all_cycles = 0;


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


static XEMU_INLINE void set_ram_bank ( const Uint8 bank )
{
	hi_ram_access_bank = bank;
	lo_ram_ptr[0] = bank;
	hi_ram_p = hi_ram_rel_ptr + (bank << 13);
	//DEBUGPRINT("RAM: HI-RAM access offset set to $%06X, BANK=%d @ %s" NL, hi_ram_access_offset, bank, cpu_where());
}


// argument must be between 0...31
static XEMU_INLINE void set_rom_bank ( const Uint8 bank )
{
	hi_rom_access_bank = bank;
	lo_ram_ptr[1] = bank | (128 + 64 + 32);
	hi_rom_p = hi_rom_rel_ptr + (bank << 14);
	//DEBUGPRINT("ROM: HI-ROM access offset set to $%05X, BANK=%d @ %s" NL, hi_rom_access_offset, bank, cpu_where());
}


static int init_ram ( const int hi_ram_kbytes )
{
	memconfig_ready &= ~1;
	if (hi_ram_kbytes < 8 || hi_ram_kbytes > 2048) {
		ERROR_WINDOW("Bad high-RAM size, must be between 8 and 2048");
		return 1;
	}
	if (hi_ram_kbytes & 7) {
		ERROR_WINDOW("Bad high-RAM size, must be multiple of 8Kbytes");
		return 1;
	}
	hi_ram_banks = hi_ram_kbytes >> 3;
	DEBUGPRINT("RAM: Setting (hi-)RAM size memtop to %dK, %d banks." NL, hi_ram_kbytes, hi_ram_banks);
	memset(lo_ram_ptr, 0xFF, LO_MEM_SIZE);
	memset(hi_ram_abs_ptr, 0xFF, HI_RAM_MAX_SIZE);
	set_ram_bank(0);
	set_rom_bank(0);
	memconfig_ready |= 1;
	return 0;
}


// Called by CPU emulation code when any kind of memory byte must be written.
void  cpu65_write_callback ( Uint16 addr, Uint8 data )
{
	/**** 0000-9EFF: low RAM (fixed RAM) area ****/
	if (addr < 0x9F00U) {
		if (XEMU_LIKELY(addr & 0xFFFEU)) {
			lo_ram_ptr[addr] = data;
			return;
		}
		if (!addr)
			set_ram_bank(data);
		else
			set_rom_bank(data & 31);
		return;
	}
	/**** 9F00-9FFF: I/O area ****/
	if (addr < 0xA000U) {
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
	if (XEMU_LIKELY(addr < 0xC000U)) {
		if (XEMU_LIKELY(hi_ram_access_bank <= hi_ram_banks))
			hi_ram_p[addr] = data;
		return;
	}
	// Others (ROM) can be ignored, since it's ROM (not writable anyway) ...
	DEBUGPRINT("ROM: trying to write ROM at address $%04X (in bank %d) @ %s" NL, addr, hi_rom_access_bank, cpu_where());
}




// Called by CPU emulation code when any kind of memory byte must be read.
Uint8 cpu65_read_callback ( Uint16 addr )
{
	/**** 0000-9EFF: low RAM (fixed RAM) area ****/
	if (addr < 0x9F00U)
		return lo_ram_ptr[addr];
	/**** 9F00-9FFF: I/O area ****/
	if (addr < 0xA000U) {
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
	if (addr < 0xC000U)
		return hi_ram_p[addr];
	/**** C000-FFFF: the rest, 16K pagable ROM area ****/
	return hi_rom_p[addr];
}





static int load_rom ( const char *fn )
{
	memconfig_ready &= ~2;
	memset(hi_rom_abs_ptr, 0xFF, HI_ROM_MAX_SIZE);
	const int ret = xemu_load_file(fn, hi_rom_abs_ptr, 16384, HI_ROM_MAX_SIZE, "Cannot load ROM");
	if (ret <= 0)
		return 1;
	if (ret % 16384) {
		ERROR_WINDOW("Bad ROM image, size must be multiple of 16384 bytes:\n%s", xemu_load_filepath);
		return 1;
	}
	DEBUGPRINT("ROM: system ROM loaded, %d bytes, %d (16K) pages, source: %s" NL, ret, ret / 16384, xemu_load_filepath);
	set_rom_bank(0);
	memconfig_ready |= 2;
	return 0;
}




/* VIA emulation callbacks, called by VIA core. See main() near to via_init() calls for further information */

static void via1_outa ( Uint8 mask, Uint8 data )
{
	DEBUGPRINT("VIA1: OUTA: DDR=%02X I2C" NL, via1.DDRA);
	i2c_bus_write(data & 3);
}

static Uint8 via1_ina ( Uint8 mask )
{
	DEBUGPRINT("VIA1: INA: DDR=%02X I2C" NL, via1.DDRA);
	//DEBUGPRINT("I2C: reading port!" NL);
	//return via1.ORA;
	return i2c_bus_read() | (0xFF - 3);
}

static void via1_outb ( Uint8 mask, Uint8 data )
{
}

static Uint8 via1_inb ( Uint8 mask )
{
	return 0xFF;		// without this, no READY. prompt. Something needs to be emulated here.
	return via1.ORB;
}



static void via1_setint ( int level )
{
	// TODO: it seems, according to the DOC, VIA1 interrupt output is connected to NMI of the CPU!!!!! [and VIA2 to IRQ, but VIA2 is not even used by the system recently ...]
#if 0
	if (level)
		cpu65.irqLevel |=  0x100;
	else
		cpu65.irqLevel &= ~0x100;
#endif
	static int old_level = 0;
	if (!old_level && level)
		cpu65.nmiEdge = 1;
	old_level = level;

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
	// First: update screen ...
	xemu_update_screen();
	// Second: we must handle SDL events waiting for us in the event queue ...
	hid_handle_all_sdl_events();
	// Third: Sleep ... Please read emutools.c source about this madness ... 40000 is (PAL) microseconds for a full frame to be produced
	xemu_timekeeping_delay(FULL_FRAME_USECS);
}




static void emulation_loop ( void )
{
	static int cycles = 0;
	for (;;) { // our emulation loop ...
		int opcyc;
		//printf("PC=%04X OPC=%02X\n", cpu65.pc, cpu65_read_callback(cpu65.pc));
		opcyc = cpu65_step();	// execute one opcode (or accept IRQ, etc), return value is the used clock cycles
		via_tick(&via1, opcyc);	// run VIA-1 tasks for the same amount of cycles as the CPU
		via_tick(&via2, opcyc);	// -- "" -- the same for VIA-2
		//opcyc <<= speed_shifter;
		cycles += opcyc;
		all_cycles += opcyc;
		if (cycles >= cycles_per_scanline) {
			if (vera_render_line() == 0) {	// start of a new frame that is ...
				update_emulator();
				//vera_vsync();
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
	if (configdb.dumpmem && memconfig_ready == 3) {
		vera_dump_vram("ram_vera.dump");
		dump_stuff("ram_lo.dump", lo_ram_ptr, LO_MEM_REAL_SIZE);
		if (hi_ram_banks)
			dump_stuff("ram_hi.dump", hi_ram_abs_ptr, hi_ram_banks << 13);
	}
}


static void reset_machine ( void )
{
	set_rom_bank(0);
	set_ram_bank(0);
	cpu65_reset();
}


static void set_cpu_speed ( const unsigned int hz )
{
	cycles_per_scanline = hz / 525 / 60;	// FIXME: do not hard code scanline number per frame?
	DEBUGPRINT("CPU: Clock requested %f MHz, %d cycles per scanline." NL, (float)hz / 1000000, cycles_per_scanline);
	ps2_set_clock_factor(hz);
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
	if (init_ram(configdb.hiramsize))
		return 1;
	if (load_rom(configdb.rom))
		return 1;
	// Continue with initializing ...
	set_cpu_speed(configdb.clock * 1000000);
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
	XEMU_MAIN_LOOP(emulation_loop, 60, 1);
	return 0;
}
