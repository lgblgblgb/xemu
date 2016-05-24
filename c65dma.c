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

static int source_step;		// <-1, 0 (hold), 1> step value for source
static int target_step;		// <-1, 0 (hold), 1> step value for target
static int source_addr;		// DMA source address
static int target_addr;		// DMA target address
static int source_is_io;	// DMA source is I/O space
static int target_is_io;	// DMA target is I/O space
//static int source_modulo;	// not used currently
//static int target_modulo;	// not used currently
static int modulo;
static int source_uses_modulo;
static int target_uses_modulo;
static Uint16 length;		// DMA operation length
static int command;		// DMA command
static int chained;		// 1 = chained (read next DMA operation "descriptor")
static int dma_list_addr;	// address of the DMA list, controller will read to "execute"



// TODO: modulo?
static Uint8 read_source_next ( void )
{
	Uint8 result;
	if (source_is_io)
		result = io_read(0xD000 | (source_addr & 0xFFF));
	else
		result = read_phys_mem(source_addr);
	source_addr += source_step;
	return result;
}


// TODO: modulo?
static void write_target_next ( Uint8 data )
{
	if (target_is_io)
		io_write(0xD000 | (target_addr & 0xFFF), data);
	else
		write_phys_mem(target_addr, data);
	target_addr += target_step;
}


static inline Uint8 read_dma_list_next ( void )
{
	return read_phys_mem(dma_list_addr++);
}


void dma_write_reg ( int addr, Uint8 data )
{
	// DUNNO about DMAgic too much. It's merely guessing from my own ROM assembly tries, C65gs/Mega65 VHDL, and my ideas :)
	// Also, it DOES things while everything other (ie CPU) emulation is stopped ...
	addr &= 3;
	dma_registers[addr] = data;
	if (addr)
		return;
	dma_list_addr = dma_registers[0] | (dma_registers[1] << 8) | ((dma_registers[2] & 15) << 16);
	printf("DMA: list address is $%06X now, just written to register %d value $%02X" NL, dma_list_addr, addr, data);
	do {
		command      = read_dma_list_next()      ;
		length       = read_dma_list_next()      ;
		length      |= read_dma_list_next() <<  8;
		source_addr  = read_dma_list_next()      ;
		source_addr |= read_dma_list_next() <<  8;
		source_addr |= read_dma_list_next() << 16;
		target_addr  = read_dma_list_next()      ;
		target_addr |= read_dma_list_next() <<  8;
		target_addr |= read_dma_list_next() << 16;
		modulo       = read_dma_list_next()      ;	// modulo is not so much handled yet, maybe it's not even a 16 bit value
		modulo      |= read_dma_list_next() <<  8;	// ... however since it's currently not used, it does not matter too much
		if (source_addr & 0x100000)
			source_step = 0;
		else
			source_step = (source_addr & 0x400000) ? -1 : 1;
		if (target_addr & 0x100000)
			target_step = 0;
		else
			target_step = (target_addr & 0x400000) ? -1 : 1;
		source_is_io = (source_addr & 0x800000);
		target_is_io = (target_addr & 0x800000);
		source_uses_modulo = (source_addr & 0x200000);
		target_uses_modulo = (target_addr & 0x200000);
		source_addr &= 0xFFFFF;
		target_addr &= 0xFFFFF;
		chained = (command & 4);
		printf("DMA: $%05X[%s%s %d] -> $%05X[%s%s %d] (L=$%04X) CMD=%d (%s)" NL,
			source_addr, source_is_io ? "I/O" : "MEM", source_uses_modulo ? " MOD" : "", source_step,
			target_addr, target_is_io ? "I/O" : "MEM", target_uses_modulo ? " MOD" : "", target_step,
			length, command, chained ? "chain" : "last"
		);
		switch (command & 3) {
			case 3:			// fill command
				while (length--)
					write_target_next(source_addr & 0xFF);
				break;
			case 0:			// copy command
				while (length--)
					write_target_next(read_source_next());
				break;
			default:
				printf("DMA: unimplemented command: %d" NL, command & 3);
				break;
		}
	} while (chained);	// chained? continue if so!
}



void dma_init ( void )
{
	memset(dma_registers, 0, sizeof dma_registers);
}


Uint8 dma_read_reg ( int addr )
{
	if ((addr & 3) != 3)
		return 0xFF;
	return 0;
}
