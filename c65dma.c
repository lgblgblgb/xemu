/* Test-case for a very simple, inaccurate, work-in-progress Commodore 65 emulator.
   Copyright (C)2016 LGB (Gábor Lénárt) <lgblgblgb@gmail.com>

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

#include <stdio.h>

#include <SDL.h>

#include "c65dma.h"
#include "commodore_65.h"
#include "emutools.h"



static Uint8 dma_registers[4];



// IO: redirect to IO space? if 1, and $D000 area (??)
// DIR: direction if 0 -> increment
// MOD: UNKNOWN details (modulo)
// HLD: hold address if 0 -> don't hold (so: inc/dec based on DIR)
#define DMA_IO(p)  ((p) & 0x800000)
#define DMA_DIR(p) ((p) & 0x400000)
#define DMA_MOD(p) ((p) & 0x200000)
#define DMA_HLD(p) ((p) & 0x100000)

#define DMA_NEXT_BYTE(p,ad) \
	if (DMA_HLD(p) == 0) { \
		ad += DMA_DIR(p) ? -1 : 1; \
	}


void dma_write_reg ( int addr, Uint8 data )
{
	// DUNNO about DMAgic too much. It's merely guessing from my own ROM assembly tries, C65gs/Mega65 VHDL, and my ideas :)
	// Also, it DOES things while everything other (ie CPU) emulation is stopped ...
	Uint8 command; // DMAgic command
	int dma_list;
	dma_registers[addr & 3] = data;
	if (addr & 3)
		return;
	dma_list = dma_registers[0] | (dma_registers[1] << 8) | (dma_registers[2] << 16);
	printf("DMA: list address is $%06X now, just written to register %d value $%02X" NL, dma_list, addr & 3, data);
	do {
		int source, target, length, spars, tpars;
		command = memory[dma_list++]      ;
		length  = memory[dma_list++]      ;
		length |= memory[dma_list++] <<  8;
		source	= memory[dma_list++]      ;
		source |= memory[dma_list++] <<  8;
		source |= memory[dma_list++] << 16;
		target  = memory[dma_list++]      ;
		target |= memory[dma_list++] <<  8;
		target |= memory[dma_list++] << 16;
		spars 	= source;
		tpars 	= target;
		source &= 0xFFFFF;
		target &= 0xFFFFF;
		printf("DMA: $%05X[%c%c%c%c] -> $%05X[%c%c%c%c] (L=$%04X) CMD=%d (%s)" NL,
			source, DMA_IO(spars) ? 'I' : 'i', DMA_DIR(spars) ? 'D' : 'd', DMA_MOD(spars) ? 'M' : 'm', DMA_HLD(spars) ? 'H' : 'h',
			target, DMA_IO(tpars) ? 'I' : 'i', DMA_DIR(tpars) ? 'D' : 'd', DMA_MOD(tpars) ? 'M' : 'm', DMA_HLD(tpars) ? 'H' : 'h',
			length, command & 3, (command & 4) ? "chain" : "last"
		);
		switch (command & 3) {
			case 3:			// fill command
				while (length--) {
					if (target < 0x20000 && target >= 0) {
						//DEBUG_WRITE_ACCESS(target, data);
						memory[target] = source & 0xFF;
					}
					//DMA_NEXT_BYTE(spars, source);	// DOES it have any sense? Maybe to write linear pattern of bytes? :-P
					DMA_NEXT_BYTE(tpars, target);
				}
				break;
			case 0:			// copy command
				while (length--) {
					Uint8 data = ((source < 0x40000 && source >= 0) ? memory[source] : 0xFF);
					DMA_NEXT_BYTE(spars, source);
					if (target < 0x20000 && target >= 0) {
						//DEBUG_WRITE_ACCESS(target, data);
						memory[target] = data;
					}
					DMA_NEXT_BYTE(tpars, target);
				}
				break;
			default:
				printf("DMA: unimplemented command: %d" NL, command & 3);
				break;
		}
	} while (command & 4);	// chained? continue if so!
}



void dma_init ( void )
{
	memset(dma_registers, 0, sizeof dma_registers);
}


Uint8 dma_read_reg ( int addr )
{
	return 0;
}
