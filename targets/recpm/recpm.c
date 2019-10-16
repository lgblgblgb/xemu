/* Re-CP/M: CP/M-like own implementation + Z80 emulator
   Part of the Xemu project, please visit: https://github.com/lgblgblgb/xemu
   Copyright (C)2016-2019 LGB (Gábor Lénárt) <lgblgblgb@gmail.com>

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

#define RECPM_MAIN_SCOPE

#include "xemu/emutools.h"
#include "xemu/emutools_files.h"
#include "xemu/emutools_config.h"
#include "recpm.h"
#include "hardware.h"
#include "bios.h"
#include "bdos.h"
#include "console.h"
#include "cpmfs.h"

#define FRAME_RATE 25


static void emulation_loop ( void )
{
	if (XEMU_UNLIKELY(stop_emulation)) {
		if (console_input() == 32)
			exit(0);
	} else {
		if (XEMU_UNLIKELY(trace)) {
			char disasm_buffer[128];
			while (cpu_cycles < cpu_cycles_per_frame) {
				z80_custom_disasm(Z80_PC, disasm_buffer, sizeof disasm_buffer);
				if (*disasm_buffer)
					puts(disasm_buffer);
				cpu_cycles += z80ex_step();
			}
		} else {
			while (cpu_cycles < cpu_cycles_per_frame)
				cpu_cycles += z80ex_step();
		}
		cpu_cycles -= cpu_cycles_per_frame;
#if 0
		if (emu_cost_cycles) {
			cpu_cycles += emu_cost_cycles;
			emu_cost_cycles = 0;
		}
		if (emu_cost_usecs) {
			cpu_cycles += emu_cost_usecs / cpu_mhz;
			emu_cost_usecs = 0;
		}
#endif
	}
	console_cursor_blink(5);
	console_iteration();
	xemu_timekeeping_delay(1000000 / FRAME_RATE);
}



static int get_guarded_cfg_num ( const char *optname, int min, int max )
{
	int ret = xemucfg_get_num(optname);
	if (ret < min) {
		DEBUGPRINT("BADNUM: too low value (%d) for option \"%s\", the minimum is %d, applying that, instead." NL, ret, optname, min);
		return min;
	} else if (ret > max) {
		DEBUGPRINT("BADNUM: too high value (%d) for option \"%s\", the maximum is %d, applying that, instead." NL, ret, optname, max);
		return max;
	} else
		return ret;
}


void recpm_shutdown_callback ( void )
{
	DEBUGPRINT("%s() is here!" NL, __func__);
	cpmfs_uninit();
}


#if 0
int cpmprg_prepare_psp ( int argc, char **argv )
{
        memset(memory + 8, 0, 0x100 - 8);
        memset(memory + 0x5C + 1, 32, 11);
        memset(memory + 0x6C + 1, 32, 11);
        for (int a = 0; a < argc; a++) {
                if (a <= 1)
                        write_filename_to_fcb(a == 0 ? 0x5C : 0x6C, argv[a]);
                if (memory[0x81])
                        strcat((char*)memory + 0x81, " ");
                strcat((char*)memory + 0x81, argv[a]);
                if (strlen((char*)memory + 0x81) > 0x7F)
                        return CPMPRG_STOP(1, "Too long command line for the CP/M program");
        }
        memory[0x80] = strlen((char*)memory + 0x81);
        return 0;
}
#endif

static int load ( const char *fn )
{
	if (!fn)
		return 0;
	if (!*fn)
		FATAL("Empty string for loading program.");
	// the last parameter for mounting drive will instruct CPM-FS to get the drive path as the dirname part of "fn"
	if (cpmfs_mount_drive(0, fn, 1))
		return 1;
	cpmfs_mount_drive(1, "/", 0);
	int tpa_size = memory[6] + (memory[7] << 8) - 0x100;
	DEBUGPRINT("LOAD: trying to load program \"%s\", max allowed size = %d bytes." NL, fn, tpa_size);
	int size = xemu_load_file(fn, memory + 0x100, 1, tpa_size, "Cannot load program");
	if (size < 0)
		return 1;
	DEBUGPRINT("LOAD: Program loaded, %d bytes, %d bytes TPA remained free" NL, size, tpa_size - size);
	Z80_PC = 0x100;
	cpm_dma = 0x80;	// default DMA
	Z80_C = 0;	// FIXME: should be the same as zero page loc 4, drive + user number ...
	memset(memory + 8, 0, 0x100 - 8);
	memset(memory + 0x5C + 1, 32, 11);
	memset(memory + 0x6C + 1, 32, 11);
	return 0;
}



int main ( int argc, char **argv )
{
	int memtop = 0x10000;
	xemu_pre_init(APP_ORG, TARGET_NAME, "Re-CP/M");
	xemucfg_define_switch_option("fullscreen", "Start in fullscreen mode");
	xemucfg_define_switch_option("syscon", "Keep system console open (Windows-specific effect only)");
	xemucfg_define_num_option("width", 80, "Terminal width in character columns");
	xemucfg_define_num_option("height", 25, "Terminal height in character rows");
	xemucfg_define_num_option("zoom", 100, "Zoom the window by the given percentage (50%-200%)");
	xemucfg_define_num_option("cpmsize", 64, "Size of the CP/M system");
	xemucfg_define_num_option("clock", 4, "Rough Z80 emulation speed in MHz with 'emulation cost'");
	xemucfg_define_num_option("baud", 0, "Emulate serial terminal with about the given baud rate [0=disable]");
	xemucfg_define_str_option("load", NULL, "Load and run a CP/M program");
	xemucfg_define_switch_option("trace", "Trace the program, VERY spammy!");
	xemucfg_define_switch_option("mapvideo", "Map video+colour RAM into the end of addr space");
	if (xemucfg_parse_all(argc, argv))
		return 1;
	trace = xemucfg_get_bool("trace");
	memset(memory, 0, sizeof memory);
	memset(modded, 0, sizeof modded);
	if (console_init(
		get_guarded_cfg_num("width",  38, 160),
		get_guarded_cfg_num("height", 20,  60),
		get_guarded_cfg_num("zoom",   50, 200),
		xemucfg_get_bool("mapvideo") ? &memtop : NULL,
		xemucfg_get_num("baud") > 0 ? get_guarded_cfg_num("baud", 300, 1000000) : 0
	))
		return 1;
	memtop &= ~0xFF;
	DEBUGPRINT("System RAM size: %d Kbytes." NL, memtop >> 10);
	bios_install(memtop - 0x100);
	bdos_install(memtop - 0x200);
	cpmfs_init();
	osd_init_with_defaults();
	/* Intialize memory and load ROMs */
	clear_emu_events();	// also resets the keyboard
	cpu_mhz = get_guarded_cfg_num("clock", 1, 33);
	cpu_cycles_per_frame = (1000000 * cpu_mhz) / FRAME_RATE;
	DEBUGPRINT("Z80: setting CPU speed to %dMHz, %d CPU cycles per refresh-rate (=%dHz)" NL, cpu_mhz, cpu_cycles_per_frame, FRAME_RATE);
	z80ex_init();
	if (!xemucfg_get_bool("syscon"))
		sysconsole_close(NULL);
	Z80_PC = 0;
	Z80_SP = 0x100;
	if (load(xemucfg_get_str("load")))
		return 1;
	conputs("re-CP/M\r\n");
	xemu_set_full_screen(xemucfg_get_bool("fullscreen"));
	xemu_timekeeping_start();	// we must call this once, right before the start of the emulation
	XEMU_MAIN_LOOP(emulation_loop, FRAME_RATE, 1);
	return 0;
}
