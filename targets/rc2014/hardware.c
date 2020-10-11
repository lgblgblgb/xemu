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
#include "hardware.h"
#include "console.h"
#include "fake_rom.h"
#include <string.h>

#define OPCODE_RET 0xC9


Z80EX_CONTEXT z80ex;
Uint8 memory[0x10000];
int stop_emulation = 0;

int cpu_cycles = 0;
int cpu_cycles_per_frame;
int cpu_mhz;
int trace;

enum ed_trap_types {
	ED_TRAP_RESET = 0xBC, ED_TRAP_SERVICE_BEGIN, ED_TRAP_SERVICE_RUN, ED_TRAP_RST_08, ED_TRAP_RST_10,
	ED_TRAP_RST_18, ED_TRAP_RST_20, ED_TRAP_RST_28, ED_TRAP_RST_30, ED_TRAP_RST_38,
	ED_TRAP_QUIT
};


static int internal_rom_is_in_use = 0;


static inline void place_ed_trap ( Uint16 addr, enum ed_trap_types trap_no ) {
	memory[addr] = 0xED;
	memory[addr+1] = trap_no;
}
static inline void place_ed_trap_with_ret ( Uint16 addr, enum ed_trap_types trap_no ) {
	place_ed_trap(addr, trap_no);
	memory[addr + 2] = OPCODE_RET;
}

void use_internal_rom ( int yes )
{
	if (yes) {
		memset(memory, 0x00, 0x8000);
		place_ed_trap(0, ED_TRAP_RESET);
		place_ed_trap(2, ED_TRAP_SERVICE_BEGIN);
		place_ed_trap(4, ED_TRAP_SERVICE_RUN);
		place_ed_trap_with_ret(0x08, ED_TRAP_RST_08);
		place_ed_trap_with_ret(0x10, ED_TRAP_RST_10);
		place_ed_trap_with_ret(0x18, ED_TRAP_RST_18);
		place_ed_trap_with_ret(0x20, ED_TRAP_RST_20);
		place_ed_trap_with_ret(0x28, ED_TRAP_RST_28);
		place_ed_trap_with_ret(0x30, ED_TRAP_RST_30);
		place_ed_trap_with_ret(0x38, ED_TRAP_RST_38);
	        internal_rom_is_in_use = 1;
		Z80_PC = 0;
		Z80_SP = 0xFFFF;
	} else {
		internal_rom_is_in_use = 0;
	}
}

int z80ex_ed_cb ( Z80EX_BYTE opcode )
{
	Uint16 addr = Z80_PC - 2;
	if (addr > 0xFF || !internal_rom_is_in_use) {
		return 1;
	}
	//DEBUGPRINT("Z80: trap $%02X @ PC=$%04X" NL, opcode, addr);
	switch ((enum ed_trap_types)opcode) {
		case ED_TRAP_RESET:
			xrcrom_rst(0);
			break;
		case ED_TRAP_SERVICE_BEGIN:
			xrcrom_begin();
			break;
		case ED_TRAP_SERVICE_RUN:
			xrcrom_run();
			break;
		case ED_TRAP_RST_08:
		case ED_TRAP_RST_10:
		case ED_TRAP_RST_18:
		case ED_TRAP_RST_20:
		case ED_TRAP_RST_28:
		case ED_TRAP_RST_30:
		case ED_TRAP_RST_38:
			xrcrom_rst(opcode - ED_TRAP_RST_08 + 1);
			break;
		case ED_TRAP_QUIT:
			XEMUEXIT(0);
			break;
		default:
			DEBUGPRINT("ROM: unknown ED trap $%02X at PC $%04X" NL, opcode, addr);
			return 1;
	}
	return 0;
}


Z80EX_BYTE z80ex_mread_cb ( Z80EX_WORD addr, int m1_state )
{
	return memory[addr];
}

void z80ex_mwrite_cb ( Z80EX_WORD addr, Z80EX_BYTE value )
{
	if (XEMU_UNLIKELY(addr < 0x8000)) {
		DEBUG("MEM: trying to write ROM at $%04X" NL, addr);
		return;
	}
	memory[addr] = value;
}

void emu_mem_write ( int addr, int data )
{
	if (addr < 0 || addr > 0xFFFF)
		FATAL("emu_mem_write(): invalid address %d" NL, addr);
	if (data < 0 || data > 0xFF)
		FATAL("emu_mem_write(): invalid data %d" NL, data);
	memory[addr] = data;
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
#if 0
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
#endif
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
