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
#include "bdos.h"
#include "hardware.h"
#include "console.h"
#include "cpmfs.h"


int bdos_start;
int cpm_dma;


// Addr must be 256 byte aligned! This is not a need by the func too much, but in general CP/M apps
// may depend on that (?).
// ALSO, this must be BELOW the BIOS, since this is the address also which is the FIRST allocated
// memory byte, cannot be used by a program!
void bdos_install ( int addr )
{
	bdos_start = addr;
	// the dispatch for BDOS entry
	emu_mem_write(bdos_start + 0, 0xD3);	// opcode of "OUT (n),A", we use this as only a dispatch point
	emu_mem_write(bdos_start + 1, 0xC9);	// argument of the OUT, let's use the same as opcode as RET, just for fun, never mind
	emu_mem_write(bdos_start + 2, 0xC9);	// opcode of "RET"
	// buffered console input ("line editor") dispatch point
	emu_mem_write(bdos_start + 3, 0xD3);
	emu_mem_write(bdos_start + 4, 0xC9);
	emu_mem_write(bdos_start + 5, 0xC9);
	// write BDOS call to the "zero page"
	emu_mem_write(5, 0xC3);			// opcode of "JP"
	emu_mem_write(6, bdos_start & 0xFF);	// low byte for BDOS entry
	emu_mem_write(7, bdos_start >>   8);	// high byte for BDOS entry
	DEBUGPRINT("BDOS: installed from $%04X" NL, bdos_start);
}


static void buffered_console_input_editor ( void )
{
	Uint8 c = console_input();
}

int bdos_find_first ( const char *dirpath, int fcb_address )
{
	//validate_memory_access(fcb_address, FCB_SIZE);
	//validate_memory_access(cpm_dma, 128);
	if (cpmfs_search_file_setup(-1, memory + fcb_address, CPMFS_SF_JOKERY|CPMFS_SF_STORE_IN_DMA0|CPMFS_SF_INPUT_IS_FCB)) {
		return 0xFF;
	} else {
		int ret = cpmfs_search_file();
		return (ret >= 0 && ret <= 3) ? ret : 0xFF;
	}
}


int bdos_find_next ( void )
{
	int ret = cpmfs_search_file();
	return (ret >= 0 && ret <= 3) ? ret : 0xFF;
}


// dispatch addr (OUT emulation with the PC ...)
int bdos_handle ( int addr )
{
	if (addr == bdos_start + 3) {	// our buffered console input ("line editor") thinggy ...
		buffered_console_input_editor();
		Z80_PC -= 2;	// re-execute
		return 1;	// it IS handled!
	}
	if (addr != bdos_start)
		return 0;	// not BDOS call dispatch single point
	DEBUG("BDOS: calling function #%d" NL, Z80_C);
	switch (Z80_C) {
		case 0:		// P_TERMCPM, system reset
			Z80_PC = 0;	// no idea, let continue with BIOS WBOOT ;-P
			break;
		case 1:		// C_READ, console input, wait for character, also echo it
			Z80_A = console_input();
			if (Z80_A == 0)
				Z80_PC -= 2;	// re-execute for waiting ...
			else {
				Z80_L = Z80_A;
				console_output(Z80_A);
			}
			break;
		case 2:		// C_WRITE - console output
			console_output(Z80_E);
			break;
		case 6:		// C_RAWIO
			if (Z80_E == 0xFF) {
				Z80_A = console_input();
				Z80_L = Z80_A;	// ????
			} else
				console_output(Z80_E);
			break;
		case  9:	// C_WRITESTR, send string (@ DE) to the console
			while (emu_mem_read(Z80_DE) != '$')
				console_output(emu_mem_read(Z80_DE++));
			break;
		case 11:	// C_STAT, console status
			Z80_A = console_status();
			Z80_L = Z80_A;
			break;
		case 12:	// S_BDOSVER, version
			Z80_B = 0;    Z80_H = 0;	// system type
			Z80_A = 0x22; Z80_L = 0x22;	// CP/M version, 2.2 here
			break;
		case 17:	// F_SFIRST - find first
			DEBUGPRINT("FIXME F_SFIRST [FIRST_BYTE=$%02X]: ", emu_mem_read(Z80_DE));
			for (int a = 1; a < 12; a++)
				DEBUGPRINT("%c", emu_mem_read(Z80_DE + a));
			DEBUGPRINT(NL);
			Z80_A = Z80_L = bdos_find_first(".", Z80_DE);
			break;
		case 18:	// F_SNEXT - find next
			DEBUGPRINT("FIXME F_SNEXT" NL);
			Z80_A = Z80_L = bdos_find_next();
			break;
		case 25:	// DRV_GET, get current drive
			Z80_A = 0;	// TODO now we support only drive A:
			break;
		case 26:	// F_DMAOFF, set DMA address
			cpm_dma = Z80_DE;	// FIXME: check DMA
			DEBUGPRINT("BDOS: setting DMA to $%04X" NL, cpm_dma);
			break;
		default:
			DEBUGPRINT("BDOS: sorry, function #%d is not implemented" NL, Z80_C);
			Z80_A = 0;	// ??? FIXME: on unknown function ...
			break;
	}
	return 1;	// it WAS the BDOS call dispatch!
}
