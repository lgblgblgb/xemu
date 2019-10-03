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
#include "bios.h"
#include "hardware.h"
#include "console.h"
#include "bdos.h"


static int bios_start;

// Addr must be 256 byte aligned! This is not a need by the func too much, but in general CP/M apps
// may depend on that (?).
void bios_install ( int addr )
{
	bios_start = addr;
	for (int tab_addr = addr, tab_end = addr + 99, fnc_addr = tab_end; tab_addr < tab_end;) {
		// We could put the "dispatch" points here directly. But some tricky software may expect to have real addresses here, as a true JMP table ...
		emu_mem_write(tab_addr++, 0xC3);		// opcode of "JP"
		emu_mem_write(tab_addr++, fnc_addr & 0xFF);	// jump addr, low byte
		emu_mem_write(tab_addr++, fnc_addr >>   8);	// jump addr, high byte
		// the dispatches
		emu_mem_write(fnc_addr++, 0xD3);	// opcode of "OUT (n),A", we use this as only a dispatch point
		emu_mem_write(fnc_addr++, 0xD3);	// argument of the OUT, let's use the same as opcode if someone managed to jump on the wrong place ;-P
		emu_mem_write(fnc_addr++, 0xC9);	// opcode of "RET"
	}
	// write BIOS call to the "zero page"
	// it must point to WBOOT, and kinda important, some software use this, to find out the BIOS JUMP table address
	emu_mem_write(0, 0xC3);				// opcode of "JP"
	emu_mem_write(1, (bios_start + 3) & 0xFF);	// low byte for BIOS WBOOT
	emu_mem_write(2, (bios_start + 3) >>   8);	// high byte for BIOS WBOOT
	DEBUGPRINT("BIOS: installed from $%04X, entry point is $%04X" NL, bios_start, bios_start + 3);
}

// dispatch addr (OUT emulation with the PC ...)
int bios_handle ( int addr )
{
	addr = (addr - bios_start - 99) / 3;
	if (addr < 0 || addr > 32)
		return 0;	// not BIOS call dispatch area, tell the caller about this
	DEBUG("BIOS: calling function #%d" NL, addr);
	switch (addr) {
		case 0:	// BOOT
			conputs("<<BOOT BIOS vector>>");
			stop_emulation = 1;
			break;
		case 1: // WBOOT
			conputs("<<WBOOT BIOS vector>>");
			stop_emulation = 1;
			break;
		case 2:	// CONST
			Z80_A = console_status();
			break;
		case 3: // CONIN
			Z80_A = console_input();
			if (Z80_A == 0)
				Z80_PC -= 2;	// re-execute, if no char is read (CONIN should *wait* for character)
			break;
		case 4:	// CONOUT
			console_output(Z80_C);
			break;
		case 5:	// LIST, it should wait while printer is ready. But we don't have printer, so who cares to wait ...
			break;
		case 6:	// PUNCH/AUXOUT, it should wait while paper tape punch / aux device is ready. But we don't have those, so who cares ...
			break;
		case 7: // READER, paper tape reader :-O
			Z80_A = 26;	// ^Z should be returned if device is not implemented, which is our case exactly.
			break;
		case 8: // HOME
			break;	// move current drive to track 0, we don't have any low-level disk access, just ignore it
		case 9:	// SELDSK, output HL=Disk parameter header
			Z80_HL = 0;	// FIXME?
			break;
		case 10:	// SETTRK, set tack of current drive
			break;		// ignore
		case 11:	// SETSEC, set sector of current drive
			break;		// ignore
		case 12:	// SETDMA
			cpm_dma = Z80_BC;	// FIXME: is BDOS and BIOS call of setting DMA is the same?!
			DEBUGPRINT("BIOS: setting DMA to $%04X" NL, cpm_dma);
			break;
		case 13:	// READ
			Z80_A = 1;	// report unrecoverable error, as we don't emulate low level disk access
			break;
		case 14:	// WRITE
			Z80_A = 1;	// report unrecoverable error, as we don't emulate low level disk access
			break;
		case 15:	// LISTST - status of printer
			Z80_A = 0;	// report not ready, we don't emulate printer
			break;
		case 16:	// SECTRAN, translate sector for skewing
			Z80_HL = Z80_BC;	// we on't emulate low level disk access, but do what others do here as well, without software-skewing used
			break;
		// The rest is for CP/M 3 BIOS ... Not so much supported ...
		default:
			DEBUGPRINT("BIOS: unknown BIOS call #%d, triggering JMP 0" NL, addr);
			Z80_PC = 0;
			break;
	}
	return 1;	// it WAS BIOS call dispatch area!
}
