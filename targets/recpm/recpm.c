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

#include "xemu/emutools.h"
#include "xemu/emutools_files.h"
#include "xemu/emutools_config.h"
#include "xemu/z80.h"
#include "xemu/z80_dasm.h"
#include "recpm.h"
#include "bios.h"
#include "bdos.h"
#include "console.h"

#define FRAME_RATE 25

Z80EX_CONTEXT z80ex;
Uint8 memory[0x10000];
Uint8 modded[0x10000];	// purpose: to try to filter if someone tries to read/exec uninitialized code/data
int emu_cost_cycles = 0, emu_cost_usecs = 0;

static int cpu_cycles = 0;
static int cpu_cycles_per_frames;
static int cpu_mhz;
static int trace;


Z80EX_BYTE z80ex_mread_cb ( Z80EX_WORD addr, int m1_state )
{
	return memory[addr];
}

void z80ex_mwrite_cb ( Z80EX_WORD addr, Z80EX_BYTE value )
{
	//if (addr >= bdos_entry_addr)
	
	memory[addr] = value;
	modded[addr] = 1;
}

void emu_mem_write ( int addr, int data )
{
	if (addr < 0 || addr > 0xFFFF)
		FATAL("emu_mem_write(): invalid address %d" NL, addr);
	if (data < 0 || data > 0xFF)
		FATAL("emu_mem_write(): invalid data %d" NL, data);
	memory[addr] = data;
	modded[addr] = 1;
}

int emu_mem_read ( int addr  )
{
	if (addr < 0 || addr > 0xFFFF)
		FATAL("emu_mem_write(): invalid address %d" NL, addr);
	return memory[addr];
}



Z80EX_BYTE z80ex_pread_cb ( Z80EX_WORD port16 )
{
	return 0xFF;
}

void z80ex_pwrite_cb ( Z80EX_WORD port16, Z80EX_BYTE value )
{
	int addr = (Z80_PC - 2) & 0xFFFF;
	//emu_cost_cycles = 0;
	//emu_cost_usecs = 0;
	//DEBUGPRINT("Z80: I/O write at $%04X" NL, addr);
	if (bios_handle(addr))
		goto end;
	if (bdos_handle(addr))
		goto end;
	DEBUGPRINT("Z80-HOOK: unknown I/O operand at $%04X" NL, addr);
end:
	if (emu_cost_cycles) {
		z80ex_w_states(emu_cost_cycles);
		emu_cost_cycles = 0;
	}
	if (emu_cost_usecs) {
		z80ex_w_states(emu_cost_usecs / cpu_mhz);
		emu_cost_usecs = 0;
	}
}

Z80EX_BYTE z80ex_intread_cb ( void )
{
	return 0xFF;
}

void z80ex_reti_cb ( void )
{
}


static XEMU_INLINE Z80EX_BYTE disasm_mreader ( Z80EX_WORD addr )
{
	return memory[addr & 0xFFFF];
}

int z80_custom_disasm ( int addr, char *buf, int buf_size )
{
	int t1, t2;
	char o_head[256];
	char o_dasm[256];
	int oplen = z80ex_dasm(o_dasm, sizeof o_dasm, 0, &t1, &t2, disasm_mreader, addr & 0xFFFF);
	char *p = strchr(o_dasm, ' ');
	if (p) {
		*p++ = '\0';
		while (*p == ' ')
			p++;
	}
	for (int a = 0; a < oplen; a++)
		sprintf(o_head + a * 3, "%02X ", disasm_mreader(addr + a));
	if (p)
		snprintf(buf, buf_size, "%04X %-12s %-4s %s", addr, o_head, o_dasm, p);
	else
		snprintf(buf, buf_size, "%04X %-12s %s", addr, o_head, o_dasm);
	return oplen;
}



static void emulation_loop ( void )
{
	if (XEMU_UNLIKELY(trace)) {
		char disasm_buffer[128];
		while (cpu_cycles < cpu_cycles_per_frames) {
			z80_custom_disasm(Z80_PC, disasm_buffer, sizeof disasm_buffer);
			puts(disasm_buffer);
			cpu_cycles += z80ex_step();
		}
	} else
		while (cpu_cycles < cpu_cycles_per_frames)
			cpu_cycles += z80ex_step();
	cpu_cycles -= cpu_cycles_per_frames;
	if (emu_cost_cycles) {
		cpu_cycles += emu_cost_cycles;
		emu_cost_cycles = 0;
	}
	if (emu_cost_usecs) {
		cpu_cycles += emu_cost_usecs / cpu_mhz;
		emu_cost_usecs = 0;
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


static int load ( const char *fn )
{
	if (!fn)
		return 0;
	if (!*fn)
		FATAL("Empty string for loading program.");
	int size = memory[6] + (memory[7] << 8) - 0x100;
	DEBUGPRINT("LOAD: trying to load program \"%s\", max allowed size = %d bytes." NL, fn, size);
	size = xemu_load_file(fn, memory + 0x100, 1, size, "Cannot load program");
	if (size < 0)
		return 1;
	Z80_PC = 0x100;
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
	xemucfg_define_num_option("baud", 9600, "Emulate serial terminal with about the given baud rate [0=disable]");
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
	osd_init_with_defaults();
	/* Intialize memory and load ROMs */
	clear_emu_events();	// also resets the keyboard
	cpu_mhz = get_guarded_cfg_num("clock", 1, 33);
	cpu_cycles_per_frames = (1000000 * cpu_mhz) / FRAME_RATE;
	DEBUGPRINT("Z80: setting CPU speed to %dMHz, %d CPU cycles per refresh-rate (=%dHz)" NL, cpu_mhz, cpu_cycles_per_frames, FRAME_RATE);
	z80ex_init();
	if (!xemucfg_get_bool("syscon"))
		sysconsole_close(NULL);
	Z80_PC = 0;
	Z80_SP = 0x100;
	if (load(xemucfg_get_str("load")))
		return 1;
	xemu_set_full_screen(xemucfg_get_bool("fullscreen"));
	xemu_timekeeping_start();	// we must call this once, right before the start of the emulation
	XEMU_MAIN_LOOP(emulation_loop, FRAME_RATE, 1);
	return 0;
}
