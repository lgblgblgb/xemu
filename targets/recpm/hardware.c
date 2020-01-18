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
#include "hardware.h"
#include "bios.h"
#include "bdos.h"
#include "console.h"

#include <string.h>

Z80EX_CONTEXT z80ex;
Uint8 memory[0x10000];
Uint8 modded[0x10000];	// purpose: to try to filter if someone tries to read/exec uninitialized code/data
int emu_cost_cycles = 0, emu_cost_usecs = 0;
int stop_emulation = 0;

int cpu_cycles = 0;
int cpu_cycles_per_frame;
int cpu_mhz;
int trace;


Z80EX_BYTE z80ex_mread_cb ( Z80EX_WORD addr, int m1_state )
{
	return memory[addr];
}

void z80ex_mwrite_cb ( Z80EX_WORD addr, Z80EX_BYTE value )
{
	if (XEMU_UNLIKELY(addr < 8 || addr >= bdos_start)) {
		FATAL("Tampering system memory addr=$%04X PC=$%04X" NL, addr, Z80_PC);
	}
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
	//DEBUGPRINT("Z80: I/O OUT write at $%04X" NL, addr);
	if (bios_handle(addr))
		goto end;
	if (bdos_handle(addr))
		goto end;
	DEBUGPRINT("Z80-HOOK: unknown I/O operand at $%04X" NL, addr);
end:
	if (emu_cost_cycles) {
		cpu_cycles += emu_cost_cycles;
		emu_cost_cycles = 0;
	}
	if (emu_cost_usecs) {
		cpu_cycles += emu_cost_usecs * cpu_mhz;
		emu_cost_usecs = 0;
	}
	if (stop_emulation) {
		cpu_cycles += cpu_cycles_per_frame;	// to trick emulation loop seeing end of enough cycles emulated ...
		conputs("\n\rPress SPACE to exit");
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
	if (z80ex.prefix) {
		//snprintf(buf, buf_size, "%04X  %02X-PREFIX", addr, z80ex.prefix);
		*buf = 0;
		return 0;
	}
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
