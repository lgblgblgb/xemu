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



/* NOTES ABOUT C65 DMAgic "F018", AND THIS EMULATION:

	* read_phys_mem() and write_phys_mem() handles of "invalid" addresses by wrapping around, no need to worry
	* read_phys_mem() also sees the "CPU port". This may be a problem.
	* write_phys_mem() can also modify the "CPU port". This may be a problem.
	* io_read() and io_write() can *ONLY* be used with addresses between $D000 and $DFFF
	* modulo currently not handled, and it seems there is not so much decent specification how it does work
	* INT / interrupt is not handled
	* MIX command is implemented, though without decent specification is more like a guessing only
	* SWAP command is also a guessing, but a better one, as it's kinda logical what it is used for
	* COPY/FILL should be OK
	* DMA length of ZERO probably means $10000
	* Current emulation is "blocky" ie, the whole emulation stops while DMA operation is done. This is of course highly incorrect!
	* DMA "ready" status thus is just constant "ready", as CPU won't see otherwise because of the "blocky" implementation
	* DMA sees the upper memory regions (ie in BANK 4 and above), which may have "something", though REC (Ram Expansion Controller) is not implemented yet
	* It's currently unknown that DMA registers are modified as DMA list is read, ie what happens if only reg 0 is re-written
	* C65 specification tells about "not implemented sub-commands": I am curious what "sub commands" are or planned as, etc ...
	* Reading status would resume interrupted DMA operation (?) it's not emulated, as interrupt is not emulated either ...

*/


static Uint8 dma_registers[4];		// The four DMA registers (with last values written by the CPU)
static int   source_step;		// [-1, 0, 1] step value for source (0 = hold, constant address)
static int   target_step;		// [-1, 0, 1] step value for target (0 = hold, constant address)
static int   source_addr;		// DMA source address (the low byte is also used by COPY command as the "filler byte")
static int   target_addr;		// DMA target address
static int   source_is_io;		// DMA source is I/O space (only the lower 12 bits are used of the source_addr then?)
static int   target_is_io;		// DMA target is I/O space (only the lower 12 bits are used of the target_addr then?)
static int   modulo;			// Currently not used/emulated
static int   source_uses_modulo;	// Currently not used/emulated
static int   target_uses_modulo;	// Currently not used/emulated
static int   length;			// DMA operation length
static int   command;			// DMA command
static int   chained;			// 1 = chained (read next DMA operation "descriptor")
static int   dma_list_addr;		// Current address of the DMA list, controller will read to "execute"
static Uint8 minterms[4];		// Used with MIX DMA command only




#define IO_ADDR(a)		(0xD000 | ((a) & 0xFFF))
#define IO_READ(a)		io_read(IO_ADDR(a))
#define IO_WRITE(a, d)		io_write(IO_ADDR(a), d)
#define MEM_READ(a)		read_phys_mem(a)
#define MEM_WRITE(a, d)		write_phys_mem(a, d)
#define DMA_READ(io, a)		((io) ? IO_READ(a) : MEM_READ(a))
#define DMA_WRITE(io, a, d)	do { if (io) IO_WRITE(a, d); else MEM_WRITE(a, d); } while (0)



// TODO: modulo?
static Uint8 read_source_next ( void )
{
	Uint8 result = DMA_READ(source_is_io, source_addr);
	source_addr += source_step;
	return result;
}


// TODO: modulo?
static void write_target_next ( Uint8 data )
{
	DMA_WRITE(target_is_io, target_addr, data);
	target_addr += target_step;
}


// TODO: modulo?
static void swap_next ( void )
{
	Uint8 sa = DMA_READ(source_is_io, source_addr);
	Uint8 da = DMA_READ(target_is_io, target_addr);
	DMA_WRITE(source_is_io, source_addr, da);
	DMA_WRITE(target_is_io, target_addr, sa);
	source_addr += source_step;
	target_addr += target_step;
}


// TODO: modulo?
static void mix_next ( void )
{
	Uint8 sa = DMA_READ(source_is_io, source_addr);
	Uint8 da = DMA_READ(target_is_io, target_addr);
	// NOTE: it's not clear from the specification, what MIX
	// does. I assume, that it does some kind of minterm
	// with source and target and writes the result to
	// target. I'm not even sure how the minterms are
	// interpreted on the bits of two bytes too much. FIXME!!!
	da =
		(( sa) & ( da) & minterms[3]) |
		(( sa) & (~da) & minterms[2]) |
		((~sa) & ( da) & minterms[1]) |
		((~sa) & (~da) & minterms[0]) ;
	DMA_WRITE(target_is_io, target_addr, da);
	source_addr += source_step;
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
	// OF COURSE IT IS HIGHLY INCORRECT!!!! FIXME stuff ...
	addr &= 3;
	dma_registers[addr] = data;
	if (addr)
		return;	// Only writing register 0 starts the DMA operation
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
		source_step  = (source_addr & 0x100000) ? 0 : ((source_addr & 0x400000) ? -1 : 1);
		target_step  = (target_addr & 0x100000) ? 0 : ((target_addr & 0x400000) ? -1 : 1);
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
		minterms[0] = (command &  16) ? 0xFF : 0x00;
		minterms[1] = (command &  32) ? 0xFF : 0x00;
		minterms[2] = (command &  64) ? 0xFF : 0x00;
		minterms[3] = (command & 128) ? 0xFF : 0x00;
		if (!length)
			length = 0x10000;
		switch (command & 3) {
			case 0:			// COPY command
				while (length--)
					write_target_next(read_source_next());
				break;
			case 1:			// MIX command
				while (length--)
					mix_next();
				break;
			case 2:			// SWAP command
				while (length--)
					swap_next();
				break;
			case 3:			// FILL command (SRC LO is the filler byte!)
				while (length--)
					write_target_next(source_addr & 0xFF);
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
		return 0xFF;	// other registers are (??????) writeonly? FIXME?
	return 0;	// status is always zero (ready) as we emulate DMA with stopping the CPU while it works ... :-/
}
