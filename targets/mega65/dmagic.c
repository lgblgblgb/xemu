/* Very primitive emulator of Commodore 65 + sub-set (!!) of Mega65 fetures.
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

#include "emutools.h"
#include "dmagic.h"
#include "mega65.h"
#include "vic3.h"



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
	* Current emulation timing is incorrect
	* It seems even the ROM issues new DMA command before the last finished. Workaround: in this case we run dma_update() first till it's ready. FIXME!
	* Speed of DMA in a real C65 would be affected by many factors, kind of op, or even VIC-3 fetchings etc ... "Of course" it's not emulated ...
	* DMA sees the upper memory regions (ie in BANK 4 and above), which may have "something", though REC (Ram Expansion Controller) is not implemented yet
	* It's currently unknown that DMA registers are modified as DMA list is read, ie what happens if only reg 0 is re-written
	* C65 specification tells about "not implemented sub-commands": I am curious what "sub commands" are or planned as, etc ...
	* Reading status would resume interrupted DMA operation (?) it's not emulated

  Mega65 specific NOTES:

	* Only a slight modification (which is not so correct) to be able to use hypervisor memory for DMA list/source/target
          The real solution will be, to use real 28 bit addressess (as on Mega-65) throughout the whole emulator!
*/


Uint8 dma_registers[16];		// The four DMA registers (with last values written by the CPU)
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
       Uint8 dma_status;


#define dma_write_phys_mem(addr,data)	write_phys_mem(addr,data)
#define dma_read_phys_mem(addr)		read_phys_mem(addr)


#define IO_ADDR(a)		(0xD000 | ((a) & 0xFFF))
#define IO_READ(a)		io_read(IO_ADDR(a))
#define IO_WRITE(a, d)		io_write(IO_ADDR(a), d)
#define MEM_READ(a)		dma_read_phys_mem(a)
#define MEM_WRITE(a, d)		dma_write_phys_mem(a, d)
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
	return dma_read_phys_mem(dma_list_addr++);
}


static void dma_update_all ( void )
{
	int limit = 0;
	while (dma_status) {
		dma_update();
		limit++;
		if (limit > 256 * 1024)
			FATAL("FATAL: Run-away DMA session, still blocking the emulator after %d iterations, exiting!", limit);
	}
}



void dma_write_reg ( int addr, Uint8 data )
{
	// DUNNO about DMAgic too much. It's merely guessing from my own ROM assembly tries, C65gs/Mega65 VHDL, and my ideas :)
	// The following condition is commented out for now. FIXME: how it is handled for real?!
	//if (vic_iomode != VIC4_IOMODE)
	//	addr &= 3;
	dma_registers[addr] = data;
	switch (addr) {
		case 0x2:	// for compatibility with C65, Mega65 here resets the MB part of the DMA list address
			dma_registers[4] = 0;	// this is the "MB" part of the DMA list address (hopefully ...)
			break;
		case 0xE:	// Set low order bits of DMA list address, without starting (Mega65 feature, but without VIC4 new mode, this reg will never addressed here anyway)
			dma_registers[0] = data;
			break;
	}
	if (addr)
		return;	// Only writing register 0 starts the DMA operation, otherwise just return from this function (reg write already happened)
	if (dma_status) {
		DEBUG("DMA: WARNING: previous operation is in progress, WORKAROUND: finishing first." NL);
		// Ugly hack: it seems even the C65 ROM issues new DMA commands while the previous is in-progress
		// It's possible the fault of timing of my emulation.
		// The current workaround: in this situation we run the DMA to finish the previous operation first.
		// Note, that there is a possible two PROBLEMS with this solution:
		// * Extremly long DMA command (ie chained) blocks the emulator to refresh screen etc for a long time
		// * I/O redirection as target affecting the DMA registers can create a stack overflow in the emulator code :)
		dma_update_all();
	}
	dma_list_addr = dma_registers[0] | (dma_registers[1] << 8) | ((dma_registers[2] & 15) << 16);
	dma_list_addr |= dma_registers[4] << 20;	// add the "MB" part to select MegaByte range for the DMA list reading
	DEBUG("DMA: list address is $%06X now, just written to register %d value $%02X" NL, dma_list_addr, addr, data);
	dma_status = 0x80;	// DMA is busy now, also to signal the emulator core to call dma_update() in its main loop
	command = -1;		// signal dma_update() that it's needed to fetch the DMA command, no command is fetched yet
	dma_update_all();	// DMA _stops_ CPU, however FIXME: interrupts can (???) occur, so we need to emulate that somehow later?
}



// Main emulation loop should call this function regularly, if dma_status is not zero.
// This way we have 'real' DMA, ie works while the rest of the machine is emulated too.
// Please note, that the "exact" timing of DMA and eg the CPU is still incorrect, but it's far
// better than the previous version where DMA was "blocky", ie the whole machine was "halted" while DMA worked ...
void dma_update ( void )
{
	if (!dma_status)
		return;
	if (command == -1) {
		// command == -1 signals the situation, that the (next) DMA command should be read!
		// This part is highly incorrect, ie fetching so many bytes in one step only of dma_update()
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
		source_addr &= 0xFFFFF;	// C65 1-mbyte range, chop bits used for other purposes off
		target_addr &= 0xFFFFF; // C65 1-mbyte range, chop bits used for other purposes off
		// add "MB" part of the addresses, in case of Mega65, that is, selects MegaByte (MB)
		source_addr |= dma_registers[5] << 20;
		target_addr |= dma_registers[6] << 20;
		chained = (command & 4);
		DEBUG("DMA: READ COMMAND: $%05X[%s%s %d] -> $%05X[%s%s %d] (L=$%04X) CMD=%d (%s)" NL,
			source_addr, source_is_io ? "I/O" : "MEM", source_uses_modulo ? " MOD" : "", source_step,
			target_addr, target_is_io ? "I/O" : "MEM", target_uses_modulo ? " MOD" : "", target_step,
			length, command, chained ? "chain" : "last"
		);
		minterms[0] = (command &  16) ? 0xFF : 0x00;
		minterms[1] = (command &  32) ? 0xFF : 0x00;
		minterms[2] = (command &  64) ? 0xFF : 0x00;
		minterms[3] = (command & 128) ? 0xFF : 0x00;
		if (!length)
			length = 0x10000;			// I *think* length of zero means 64K. Probably it's not true!!
		return;
	}
	// We have valid command to be executed, or continue to execute
	//DEBUG("DMA: EXECUTING: command=%d length=$%04X" NL, command & 3, length);
	switch (command & 3) {
		case 0:			// COPY command
			write_target_next(read_source_next());
			break;
		case 1:			// MIX command
			mix_next();
			break;
		case 2:			// SWAP command
			swap_next();
			break;
		case 3:			// FILL command (SRC LO is the filler byte!)
			write_target_next(source_addr & 0xFF);
			break;
	}
	// Check the situation of end of the operation
	length--;
	if (length <= 0) {
		if (chained) {			// chained?
			DEBUG("DMA: end of operation, but chained!" NL);
			dma_status = 0x81;	// still busy then, with also bit0 set (chained)
			command = -1;		// signal for next DMA command fetch
		} else {
			DEBUG("DMA: end of operation, no chained next one." NL);
			dma_status = 0;		// end of DMA command
			command = -1;
		}
	}
}



void dma_init ( void )
{
	command = -1;	// no command is fetched yet
	dma_status = 0;
	memset(dma_registers, 0, sizeof dma_registers);
}


Uint8 dma_read_reg ( int addr )
{
	// FIXME: status on ALL registers when read?!
	DEBUG("DMA: register reading at addr of %d" NL, addr);
#if 0
	if ((addr & 3) != 3)
		return 0xFF;	// other registers are (??????) writeonly? FIXME?
#endif
	return dma_status;
}
