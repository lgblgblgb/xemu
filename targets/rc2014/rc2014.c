/* RC2014 and generic Z80 SBC emulator
   Part of the Xemu project, please visit: https://github.com/lgblgblgb/xemu
   Copyright (C)2020 LGB (Gábor Lénárt) <lgblgblgb@gmail.com>

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
#include "xemu/emutools_config.h"
#include "hardware.h"
#include "console.h"
#include "uart.h"


#define FRAME_RATE	25

static const char default_rom_fn[] = "#rc2014.rom";



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
		ERROR_WINDOW("BADNUM: too low value (%d) for option \"%s\", the minimum is %d, applying that, instead." NL, ret, optname, min);
		return min;
	} else if (ret > max) {
		ERROR_WINDOW("BADNUM: too high value (%d) for option \"%s\", the maximum is %d, applying that, instead." NL, ret, optname, max);
		return max;
	} else
		return ret;
}
static double get_guarded_cfg_double ( const char *optname, double min, double max )
{
	double ret = xemucfg_get_float(optname);
	if (ret < min) {
		ERROR_WINDOW("BADNUM: too low value (%f) for option \"%s\", the minimum is %f, applying that, instead." NL, ret, optname, min);
		return min;
	} else if (ret > max) {
		ERROR_WINDOW("BADNUM: too high value (%f) for option \"%s\", the maximum is %f, applying that, instead." NL, ret, optname, max);
		return max;
	} else
		return ret;
}


void rc_shutdown_callback ( void )
{
	DEBUGPRINT("%s() is here!" NL, __func__);
}


static int load_rom ( const char *fn )
{
	if (!fn)
		return 0;
	if (!strcmp(fn, "-")) {
		use_internal_rom(1);
		DEBUGPRINT("ROM: using built-in ROM by request");
		return 0;
	}
	use_internal_rom(0);
	DEBUGPRINT("ROM: trying to load \"%s\" as ROM" NL, fn);
	int size = xemu_load_file(fn, NULL, 1, 0x8000, "Cannot load ROM");
	if (size < 0)
		return 1;
	memset(memory, 0xFF, 0x8000);
	memcpy(memory, xemu_load_buffer_p, size);
	free(xemu_load_buffer_p);
	DEBUGPRINT("ROM: loaded, %d bytes" NL, size);
	z80ex_reset();
	Z80_PC = 0x0000;
	Z80_SP = 0xFFFF;
	return 0;
}


static int load_prg ( const char *fn )
{
	if (!fn)
		return 0;
	if (!*fn)
		FATAL("Empty string for loading program.");
	DEBUGPRINT("LOAD: trying to load program \"%s\" to $8000" NL, fn);
	int size = xemu_load_file(fn, NULL, 1, 0x8000, "Cannot load program");
	if (size < 0)
		return 1;
	memset(memory + 0x8000, 0xFF, 0x8000);
	memcpy(memory + 0x8000, xemu_load_buffer_p, size);
	free(xemu_load_buffer_p);
	DEBUGPRINT("LOAD: Program loaded, %d bytes, %d bytes remained free" NL, size, 0x8000 - size);
	z80ex_reset();
	Z80_PC = 0x8000;
	Z80_SP = 0xFFFF;
	return 0;
}



int main ( int argc, char **argv )
{
	xemu_pre_init(APP_ORG, TARGET_NAME, "Generic, simple RC2014 SBC emulation from LGB");
	xemucfg_define_switch_option("fullscreen", "Start in fullscreen mode");
	xemucfg_define_switch_option("syscon", "Keep system console open (Windows-specific effect only)");
	xemucfg_define_num_option("width", 80, "Terminal width in character columns");
	xemucfg_define_num_option("height", 25, "Terminal height in character rows");
	xemucfg_define_num_option("zoom", 100, "Zoom the window by the given percentage (50%-200%)");
	xemucfg_define_float_option("clock", 4, "Select Z80 clock speed in MHz");
	xemucfg_define_float_option("baudcrystal", 1.718, "Crystal frequency for the UART chip in MHz");
	xemucfg_define_num_option("baudrate", 56400, "Initial serial baudrate (may be modified if no exact match with -baudcrystal)");
	xemucfg_define_str_option("load", NULL, "Load and run program from $8000 directly");
	xemucfg_define_str_option("rom", default_rom_fn, "Load ROM from $0000 (use file name '-' for built-in one)");
	xemucfg_define_switch_option("trace", "Trace the program, VERY spammy!");
	xemucfg_define_switch_option("besure", "Skip asking \"are you sure?\" on RESET or EXIT");
	if (xemucfg_parse_all(argc, argv))
		return 1;
	i_am_sure_override = xemucfg_get_bool("besure");
	trace = xemucfg_get_bool("trace");
	memset(memory, 0xFF, sizeof memory);
	if (console_init(
		get_guarded_cfg_num("width",  38, 120),
		get_guarded_cfg_num("height", 20,  60),
		get_guarded_cfg_num("zoom",   50, 200),
		NULL,
		NULL
	))
		return 1;
	osd_init_with_defaults();
	clear_emu_events();	// also resets the keyboard
	cpu_mhz = get_guarded_cfg_double("clock", 1.0, 33.0);
	cpu_cycles_per_frame = (1000000.0 * cpu_mhz) / (double)FRAME_RATE;
	uart_init(
		(int)(get_guarded_cfg_double("baudcrystal", 1.0, 8.0) * 1000000.0),
		get_guarded_cfg_num("baudrate", 30, 500000)
	);
	DEBUGPRINT("Z80: setting CPU speed to %.2fMHz, %d CPU cycles per refresh-rate (=%dHz)" NL, cpu_mhz, cpu_cycles_per_frame, FRAME_RATE);
	z80ex_init();
	if (!xemucfg_get_bool("syscon"))
		sysconsole_close(NULL);
	z80ex_reset();
	Z80_SP = 0xFFFF;
	if (load_rom(xemucfg_get_str("rom"))) {
		if (strcmp(xemucfg_get_str("rom"), default_rom_fn))
			return 1;
		else {
			DEBUGPRINT("ROM: default ROM could not be loaded. Using simple, built-in one." NL);
			use_internal_rom(1);
		}
	}
	if (load_prg(xemucfg_get_str("load")))
		return 1;
	xemu_set_full_screen(xemucfg_get_bool("fullscreen"));
	xemu_timekeeping_start();	// we must call this once, right before the start of the emulation
	XEMU_MAIN_LOOP(emulation_loop, FRAME_RATE, 1);
	return 0;
}
