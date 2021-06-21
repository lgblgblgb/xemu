/* F018 DMA core emulation for Commodore 65.
   Part of the Xemu project. https://github.com/lgblgblgb/xemu
   Copyright (C)2016-2021 LGB (Gábor Lénárt) <lgblgblgb@gmail.com>

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
#include "dma65.h"
#include "xemu/cpu65.h"


/* NOTES ABOUT C65 DMAgic "F018", AND THIS EMULATION:

	* this emulation part uses externally supplied functions and it depends on the target emulator how to implement read/write ops by DMA!
	* modulo currently not handled, and it seems there is not so much decent specification how it does work
	* INT / interrupt is not handled
	* MIX command is implemented, though without decent specification is more like a guessing only
	* SWAP command is also a guessing, but a better one, as it's kinda logical what it is used for
	* COPY/FILL should be OK
	* DMA length of ZERO probably means $10000 [but not so much info on this ...] FIXME?
	* Current emulation timing is incorrect
	* Speed of DMA in a real C65 would be affected by many factors, kind of op, or even VIC-3 fetchings etc ... "Of course" it's not emulated ...
	* DMA sees the upper memory regions (ie in BANK 4 and above), which may have "something", though REC (Ram Expansion Controller) is not implemented yet
	* It's currently unknown that DMA registers are modified as DMA list is read, ie what happens if only reg 0 is re-written
	* C65 specification tells about "not implemented sub-commands": I am curious what "sub commands" are or planned as, etc ...
	* Reading status would resume interrupted DMA operation (?) it's not emulated
	* FIXME: I am not sure what's the truth: DMA session "warps" within the 1Mbyte address range or within the 64K address set?!
*/


//#define DO_DEBUG_DMA

#ifdef DO_DEBUG_DMA
#	define DEBUGDMA(...) DEBUGPRINT(__VA_ARGS__)
#else
#	define DEBUGDMA(...)   DEBUG(__VA_ARGS__)
#endif

Uint8 dma_status;
Uint8 dma_registers[16];		// The four DMA registers (with last values written by the CPU)
int   dma_chip_revision;		// revision of DMA chip
int   dma_chip_initial_revision;
int   rom_date = 0;


enum dma_op_types {
	COPY_OP,
	MIX_OP,
	SWAP_OP,
	FILL_OP
};


static int   length;			// DMA operation length
static int   command;			// DMA command (-1, no command yet) byte of DMA list reading
static enum  dma_op_types dma_op;	// two lower bits of "command"
static int   chained;			// 1 = chained (read next DMA operation "descriptor")
static int   list_addr;			// Current address of the DMA list, controller will read to "execute"
static Uint8 minterms[4];		// Used with MIX DMA command only
static int   in_dma_update;		// signal that DMA update do something. Currently only useful to avoid PANIC when DMA would modify its own registers
static int   dma_self_write_warning;	// Warning window policy for the event in case of the happening described in the comment of the previous line :)
static int   list_base;			// base address of the DMA list fetch, like base in the source/target struct later, see there
static int   filler_byte;		// byte used for FILL DMA command only

#define DMA_ADDRESSING(channel)		((channel.addr & channel.mask) | channel.base)

// source and target DMA "channels":
static struct {
	int addr;	// address of the current operation.
	int base;	// base address for "addr", always a "pure" number!
	int mask;	// mask value to handle warp around etc, eg 0xFFFF for 64K, 0xFFF for 4K (I/O)
	int step;	// step value, zero(HOLD)/negative/positive.
	int is_modulo;	// modulo mode, if it's not zero
	int is_io;	// channel access I/O instead of memory, if it's not zero
} source, target;

static struct {
	int enabled, used, col_counter, col_limit, row_counter, row_limit, value;
} modulo;




#define DMA_READ_SOURCE()	(XEMU_UNLIKELY(source.is_io) ? DMA_SOURCE_IOREADER_FUNC(DMA_ADDRESSING(source)) : DMA_SOURCE_MEMREADER_FUNC(DMA_ADDRESSING(source)))
#define DMA_READ_TARGET()	(XEMU_UNLIKELY(target.is_io) ? DMA_TARGET_IOREADER_FUNC(DMA_ADDRESSING(target)) : DMA_TARGET_MEMREADER_FUNC(DMA_ADDRESSING(target)))
#define DMA_WRITE_SOURCE(data)	do { \
					if (XEMU_UNLIKELY(source.is_io)) \
						DMA_SOURCE_IOWRITER_FUNC(DMA_ADDRESSING(source), data); \
					else \
						DMA_SOURCE_MEMWRITER_FUNC(DMA_ADDRESSING(source), data); \
				} while (0)
#define DMA_WRITE_TARGET(data)	do { \
					if (XEMU_UNLIKELY(target.is_io)) \
						DMA_TARGET_IOWRITER_FUNC(DMA_ADDRESSING(target), data); \
					else \
						DMA_TARGET_MEMWRITER_FUNC(DMA_ADDRESSING(target), data); \
				} while (0)

// Unlike the functions above, DMA list read is always memory (not I/O)
// FIXME: I guess here, that reading DMA list also warps within a 64K area
#ifndef DO_DEBUG_DMA
#define DMA_READ_LIST_NEXT_BYTE()	DMA_LIST_READER_FUNC(((list_addr++) & 0xFFFF) | list_base)
#else
static int dma_list_entry_pos = 0;

static Uint8 DMA_READ_LIST_NEXT_BYTE ( void )
{
	int addr = ((list_addr++) & 0xFFFF) | list_base;
	Uint8 data = DMA_LIST_READER_FUNC(addr);
	DEBUGPRINT("DMA: reading DMA (rev#%d) list from $%08X ($%02X) [#%d]: $%02X" NL, dma_chip_revision, addr, (list_addr-1) & 0xFFFF, dma_list_entry_pos++, data);
	return data;
}
#endif


static XEMU_INLINE void copy_next ( void )
{
	DMA_WRITE_TARGET(DMA_READ_SOURCE());
	source.addr += source.step;
	target.addr += target.step;
}

static XEMU_INLINE void fill_next ( void )
{
	DMA_WRITE_TARGET(filler_byte);
	target.addr += target.step;
}

static XEMU_INLINE void swap_next ( void )
{
	Uint8 sa = DMA_READ_SOURCE();
	Uint8 da = DMA_READ_TARGET();
	DMA_WRITE_SOURCE(da);
	DMA_WRITE_TARGET(sa);
	source.addr += source.step;
	target.addr += target.step;
}

static XEMU_INLINE void mix_next ( void )
{
	Uint8 sa = DMA_READ_SOURCE();
	Uint8 da = DMA_READ_TARGET();
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
	DMA_WRITE_TARGET(da);
	source.addr += source.step;
	target.addr += target.step;
}






void dma_write_reg ( int addr, Uint8 data )
{
	// The following condition is commented out for now. FIXME: how it is handled for real?!
	//if (vic_iomode != VIC4_IOMODE)
	//	addr &= 3;
	if (XEMU_UNLIKELY(in_dma_update)) {
		// this is just an emergency stuff to disallow DMA to update its own registers ... FIXME: what would be the correct policy?
		// NOTE: without this, issuing a DMA transfer updating DMA registers would affect an on-going DMA transfer!
		if (dma_self_write_warning) {
			dma_self_write_warning = 0;
			ERROR_WINDOW("DMA writes its own registers, ignoring!\nThere will be no more warning on this!");
		}
		DEBUG("DMA: WARNING: tries to write own register by DMA reg#%d with value of $%02X" NL, addr, data);
		return;
	}
	if (addr > 3) {	// in case of C65, the extended registers cannot be written
		DEBUG("DMA: trying to write M65-specific DMA register (%d) with a C65 ..." NL, addr);
		return;
	}
	dma_registers[addr] = data;
	if (addr)
		return;	// Only writing register 0 starts the DMA operation, otherwise just return from this function (reg write already happened)
	if (XEMU_UNLIKELY(dma_status))
		FATAL("dma_write_reg(): new DMA op with dma_status != 0");
	list_addr = dma_registers[0] | (dma_registers[1] << 8); // | ((dma_registers[2] & 0xF) << 16);
	list_base = (dma_registers[2] & 0xF) << 16;
	DEBUGDMA("DMA: list address is $%06X now, just written to register %d value $%02X @ PC=$%04X" NL, list_base | list_addr, addr, data, cpu65.pc);
	dma_status = 0x80;	// DMA is busy now, also to signal the emulator core to call dma_update() in its main loop
	command = -1;		// signal dma_update() that it's needed to fetch the DMA command, no command is fetched yet
	cpu65.multi_step_stop_trigger = 1;	// trigger stopping multi-op CPU emulation mode, otherwise "delayed DMAs" would overlap each other resulting "panic"
}



/* Main emulation loop should call this function regularly, if dma_status is not zero.
   This way we have 'real' DMA, ie works while the rest of the machine is emulated too.
   Please note, that the "exact" timing of DMA and eg the CPU is still incorrect, but it's far
   better than the previous version where DMA was "blocky", ie the whole machine was "halted" while DMA worked ...
   NOTE: there was an ugly description here about the differences between F018A,F018B. it's a too big topic
   for a comment here, so it has been deleted. See here: http://c65.lgb.hu/dma.html */
int dma_update ( void )
{
	Uint8 subcommand;
	int cycles = 0;
	if (XEMU_UNLIKELY(!dma_status))
		FATAL("dma_update() called with no dma_status set!");
	if (XEMU_UNLIKELY(command == -1)) {
		// command == -1 signals the situation, that the (next) DMA command should be read!
		// This part is highly incorrect, ie fetching so many bytes in one step only of dma_update()
		// NOTE: in case of MEGA65: DMA_READ_LIST_NEXT_BYTE() uses the "megabyte" part already (taken from reg#4, in case if that reg is written)
#ifdef DO_DEBUG_DMA
		dma_list_entry_pos = 0;
#endif
		command            = DMA_READ_LIST_NEXT_BYTE();
		dma_op             = (enum dma_op_types)(command & 3);
		modulo.col_limit   = DMA_READ_LIST_NEXT_BYTE();
		modulo.row_limit   = DMA_READ_LIST_NEXT_BYTE();
		length             = modulo.col_limit | (modulo.row_limit << 8);
		filler_byte        = DMA_READ_LIST_NEXT_BYTE()      ;			// source low byte is also the filler byte in case of FILL command
		source.addr        =(DMA_READ_LIST_NEXT_BYTE() <<  8) | filler_byte;	// -- "" --
		source.addr       |= DMA_READ_LIST_NEXT_BYTE() << 16;
		target.addr        = DMA_READ_LIST_NEXT_BYTE()      ;
		target.addr       |= DMA_READ_LIST_NEXT_BYTE() <<  8;
		target.addr       |= DMA_READ_LIST_NEXT_BYTE() << 16;
		if (dma_chip_revision)	// for F018B we have an extra byte fetch here! used later in this function [making DMA list one byte longer, indeed]
			subcommand = DMA_READ_LIST_NEXT_BYTE();
		else
			subcommand = 0;	// just make gcc happy not to generate warning later
		modulo.value       = DMA_READ_LIST_NEXT_BYTE()      ;
		modulo.value      |= DMA_READ_LIST_NEXT_BYTE() <<  8;
		if (dma_chip_revision) {
			cycles += 12;	// FIXME: correct timing?
			// F018B ("new") behaviour
			source.step  = (subcommand & 2) ? 0 : ((command & 16) ? -1 : 1);
			target.step  = (subcommand & 8) ? 0 : ((command & 32) ? -1 : 1);
			source.is_modulo = (subcommand & 1);
			target.is_modulo = (subcommand & 4);
			if (dma_op == MIX_OP) {	// if it's a MIX command
				// FIXME: what about minterms in F018B?! Maybe upper bits of subcommand, but we can't [?] know ... Try that theory for now ...
				minterms[0] = (subcommand &  16) ? 0xFF : 0x00;
				minterms[1] = (subcommand &  32) ? 0xFF : 0x00;
				minterms[2] = (subcommand &  64) ? 0xFF : 0x00;
				minterms[3] = (subcommand & 128) ? 0xFF : 0x00;
			}
		} else {
			cycles += 11;	// FIXME: correct timing?
			// F018A ("old") behaviour
			source.step  = (source.addr & 0x100000) ? 0 : ((source.addr & 0x400000) ? -1 : 1);
			target.step  = (target.addr & 0x100000) ? 0 : ((target.addr & 0x400000) ? -1 : 1);
			source.is_modulo = (source.addr & 0x200000);
			target.is_modulo = (target.addr & 0x200000);
			if (dma_op == MIX_OP) {	// if it's a MIX command
				minterms[0] = (command &  16) ? 0xFF : 0x00;
				minterms[1] = (command &  32) ? 0xFF : 0x00;
				minterms[2] = (command &  64) ? 0xFF : 0x00;
				minterms[3] = (command & 128) ? 0xFF : 0x00;
			}
		}
		// use modulo mode if:
		//   * dma_init() is used with this feature to be enabled
		//   * any of source or target has the MODulo bit set
		// FIXME: logically, in case of FILL command, source uses modulo setting does not make sense, and we don't want to use modulo length counting at all
		if (XEMU_UNLIKELY(source.is_modulo || target.is_modulo)) {
			if (modulo.enabled) {
				modulo.used = 1;
				modulo.col_counter = 0;
				modulo.row_counter = 0;
				// GUESSING/FIXME/TODO: with modulo, col/row counters = 0 means $100 for real, as with non-modulo mode counter = 0 means $10000 (16 bit counter there, 8 bit wides here!)
				if (!modulo.col_limit)
					modulo.col_limit = 0x100;
				if (!modulo.row_limit)
					modulo.row_limit = 0x100;
			} else {
				modulo.used = 0;
				DEBUGPRINT("DMA: warning, MODulo mode wanted to be used, but not enabled by you!" NL);
			}
		} else
			modulo.used = 0;
		// if modulo.used is zero, source.is_modulo and target.is_modulo is never used (but maybe for logging)
		// so commenting the part below:
#if 0
		if (!modulo.used) {
			if (source.is_modulo || target.is_modulo)
				DEBUG("DMA: denying using MODulo ..." NL);
			source.is_modulo = 0;
			target.is_modulo = 0;
		}
#endif
		// It *seems* I/O stuff is still in the place even with F018B. FIXME: is it true?
		source.is_io = (source.addr & 0x800000);
		target.is_io = (target.addr & 0x800000);
		/* source selection */
		if (source.is_io) {
			source.mask	= 0xFFF;			// 4K I/O size (warps within 4K only)
			source.base	= 0;				// in case of I/O, base is not interpreted in Xemu (uses pure numbers 0-$FFF, no $DXXX), and must be zero ...
			source.addr	= (source.addr &    0xFFF);	// for C65, it is pure number, no fixed-point arith. here
		} else {
			source.mask	= 0xFFFF;			// warp around within 64K. I am still not sure, it happens with DMA on 64K or 1M. M65 VHDL does 64K, so I switched that too.
			if (dma_chip_revision)
			    source.base = (source.addr & 0x7F0000);	// base selection for C65, in case of F018B (3 more bits of addresses)
			else
			    source.base = (source.addr & 0x0F0000);	// base selection for C65, in case of F018A
			source.addr	= (source.addr & 0x00FFFF);	// offset from base, for C65 this is pure number, *NO* fixed point arithmetic here!
		}
		/* target selection - see similar lines with comments above, for source ... */
		if (target.is_io) {
			target.mask	= 0xFFF;
			target.base	= 0;
			target.addr	= (target.addr &    0xFFF);
		} else {
			target.mask	= 0xFFFF;
			if (dma_chip_revision)
			    target.base = (target.addr & 0x7F0000);
			else
			    target.base = (target.addr & 0x0F0000);
			target.addr	= (target.addr & 0x00FFFF);
		}
		/* other stuff */
		chained = (command & 4);
		DEBUG("DMA: READ COMMAND: $%07X[%s%s %d] -> $%07X[%s%s %d] (L=$%04X) CMD=%d (%s)" NL,
			DMA_ADDRESSING(source), source.is_io ? "I/O" : "MEM", source.is_modulo ? " MOD" : "", source.step,
			DMA_ADDRESSING(target), target.is_io ? "I/O" : "MEM", target.is_modulo ? " MOD" : "", target.step,
			length, dma_op, chained ? "CHAINED" : "LAST"
		);
		if (!length)
			length = 0x10000;			// I *think* length of zero means 64K. Probably it's not true!!
		return cycles;
	}
	// We have valid command to be executed, or continue to execute
	//DEBUG("DMA: EXECUTING: command=%d length=$%04X" NL, command & 3, length);
	in_dma_update = 1;
	switch (dma_op) {
		case COPY_OP:		// COPY command (0)
			copy_next();
			cycles = 2;	// FIXME: correct timing?
			break;
		case MIX_OP:		// MIX  command (1)
			mix_next();
			cycles = 3;	// FIXME: correct timing?
			break;
		case SWAP_OP:		// SWAP command (2)
			swap_next();
			cycles = 4;	// FIXME: correct timing?
			break;
		case FILL_OP:		// FILL command (3)
			fill_next();
			cycles = 1;	// FIXME: correct timing?
			break;
	}
	// Maintain length counter or modulo-related counters (in the second case, also add modulo value if needed)
	if (XEMU_UNLIKELY(modulo.used)) {
		// modulo mode. we don't use the usual "length" but own counters.
		// but we DO reset "length" to zero if modulo transfer is done, so
		// it will be detected by the 'if' at the bottom of this huge function as the end of the DMA op
		if (modulo.col_counter == modulo.col_limit) {
			if (modulo.row_counter == modulo.row_limit) {
				length = 0;	// just to fullfish end-of-operation condition
			} else {
				// end of modulo "columns", but there are "rows" left, reset columns counter, increment row counter,
				// also add the modulo value to source and/or target channels, if the given channel is in modulo mode
				modulo.col_counter = 0;
				modulo.row_counter++;
				if (source.is_modulo)
					source.addr += modulo.value;
				if (target.is_modulo)
					target.addr += modulo.value;
			}
		} else {
			modulo.col_counter++;
		}
	} else {
		length--;	// non-MODulo mode, we simply decrement length counter ...
	}
	if (length <= 0) {			// end of DMA operation for the current DMA list entry?
		if (chained) {			// chained?
			DEBUGDMA("DMA: end of operation, but chained!" NL);
			dma_status = 0x81;	// still busy then, with also bit0 set (chained)
			command = -1;		// signal for next DMA command fetch
		} else {
			DEBUGDMA("DMA: end of operation, no chained next one." NL);
			dma_status = 0;		// end of DMA command
			command = -1;
		}
	}
	in_dma_update = 0;
	return cycles;
}



int dma_update_multi_steps ( int do_for_cycles )
{
	int cycles = 0;
	do {
		cycles += dma_update();
	} while (cycles <= do_for_cycles && dma_status);
	return cycles;
}


void detect_rom_date ( Uint8 *p )
{
	if (p == NULL) {
		DEBUGPRINT("ROM: version check is disabled (NULL pointer), previous version info: %d" NL, rom_date);
	} else if (p[0] == 0x56) {     // 'V'
		rom_date = 0;
		for (int a = 0; a < 6; a++) {
			p++;
			if (*p >= '0' && *p <= '9')
				rom_date = rom_date * 10 + *p - '0';
			else {
				rom_date = -1;
				DEBUGPRINT("ROM: version check failed (num-numberic character)" NL);
				return;
			}
		}
		DEBUGPRINT("ROM: version check succeeded, detected version: %d" NL, rom_date);
	} else {
		DEBUGPRINT("ROM: version check failed (no leading 'V')" NL);
		rom_date = -1;
	}
}


void dma_init_set_rev ( unsigned int revision, Uint8 *rom_ver_signature )
{
	detect_rom_date(rom_ver_signature);
	int rom_suggested_dma_revision = (rom_date < 900000 || rom_date > 910522);
	DEBUGPRINT("ROM: version check suggests DMA revision %d" NL, rom_suggested_dma_revision);
	revision &= 0xFF;
	if (revision > 2) {
		FATAL("Unknown DMA revision value tried to be set (%d)!", revision);
	} else if (revision == 2) {
		if (!rom_ver_signature)
			FATAL("dma_ini_set_rev(): revision == 2 (auto-detect) but rom_ver_signature == NULL (cannot auto-detect)");
		if (rom_date <= 0)
			WARNING_WINDOW("ROM version cannot be detected, and DMA revision auto-detection was requested.\nDefaulting to revision %d.\nWarning, this may cause incorrect behaviour!", rom_suggested_dma_revision);
		dma_chip_revision = rom_suggested_dma_revision;
		dma_chip_initial_revision = rom_suggested_dma_revision;
		DEBUGPRINT("DMA: setting chip revision to #%d based on the ROM auto-detection" NL, dma_chip_initial_revision);
	} else {
		dma_chip_revision = revision;
		dma_chip_initial_revision = revision;
		if (dma_chip_revision != rom_suggested_dma_revision && rom_date > 0)
			WARNING_WINDOW("DMA revision is forced to be %d, while ROM version (%d)\nsuggested revision is %d. Using the forced revision %d.\nWarning, this may cause incorrect behaviour!", dma_chip_revision, rom_date, rom_suggested_dma_revision, dma_chip_revision);
		DEBUGPRINT("DMA: setting chip revision to #%d based on configuration/command line request (forced). Suggested revision by ROM date: #%d" NL, dma_chip_initial_revision, rom_suggested_dma_revision);
	}
}


void dma_init ( unsigned int revision )
{
	modulo.enabled = (revision & DMA_FEATURE_MODULO);
	revision &= ~DMA_FEATURE_MODULO;
	if (revision & DMA_FEATURE_DYNMODESET)
		FATAL("DMA feature DMA_FEATRUE_DYNMODESET is not supported on C65!");
	if (revision == 2)
		revision = 1;	// in case of "auto-detect" we use rev1, let it be for dma_init_set_rev() called by target later to refine this
	if (revision > 1) {
		FATAL("Unknown DMA revision value tried to be set (%d)!", revision);
	} else {
		dma_chip_revision = revision;
		dma_chip_initial_revision = revision;
	}
	DEBUGPRINT("DMA: initializing DMA engine for chip revision %d (initially, may be modified later!), dyn_mode=%s, modulo_support=%s." NL,
		dma_chip_revision,
		"NEVER(C65)",
		modulo.enabled ? "ENABLED" : "DISABLED"
	);
	dma_reset();
}


void dma_reset ( void )
{
	command = -1;	// no command is fetched yet
	dma_status = 0;
	memset(dma_registers, 0, sizeof dma_registers);
	source.base = 0;
	target.base = 0;
	list_base = 0;
	in_dma_update = 0;
	dma_self_write_warning = 1;
	dma_chip_revision = dma_chip_initial_revision;
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


/* --- SNAPSHOT RELATED --- */

#ifdef XEMU_SNAPSHOT_SUPPORT

// Note: currently state is not saved "within" a DMA operation. It's only a problem, if a DMA
// operation is not handled fully here, but implemented as an iterating update method from the
// emulator code. FIXME.

#include <string.h>

#define SNAPSHOT_DMA_BLOCK_VERSION	2
#define SNAPSHOT_DMA_BLOCK_SIZE		0x100


int dma_snapshot_load_state ( const struct xemu_snapshot_definition_st *def, struct xemu_snapshot_block_st *block )
{
	Uint8 buffer[SNAPSHOT_DMA_BLOCK_SIZE];
	int a;
	if (block->block_version != SNAPSHOT_DMA_BLOCK_VERSION || block->sub_counter || block->sub_size != sizeof buffer)
		RETURN_XSNAPERR_USER("Bad C65 block syntax");
	a = xemusnap_read_file(buffer, sizeof buffer);
	if (a) return a;
	/* loading state ... */
	memcpy(dma_registers, buffer, sizeof dma_registers);
	dma_chip_revision		= buffer[0x80];
	dma_chip_initial_revision	= buffer[0x81];
	modulo.enabled			= buffer[0x83];
	dma_status			= buffer[0x84];
	in_dma_update			= buffer[0x85];
	return 0;
}


int dma_snapshot_save_state ( const struct xemu_snapshot_definition_st *def )
{
	Uint8 buffer[SNAPSHOT_DMA_BLOCK_SIZE];
	int a = xemusnap_write_block_header(def->idstr, SNAPSHOT_DMA_BLOCK_VERSION);
	if (a) return a;
	memset(buffer, 0xFF, sizeof buffer);
	/* saving state ... */
	memcpy(buffer, dma_registers, sizeof dma_registers);
	buffer[0x80] = dma_chip_revision;
	buffer[0x81] = dma_chip_initial_revision;
	buffer[0x83] = modulo.enabled ? 1 : 0;
	buffer[0x84] = dma_status;		// bit useless to store (see below, actually it's a problem), but to think about the future ...
	buffer[0x85] = in_dma_update ? 1 : 0;	// -- "" --
	if (dma_status)
		WARNING_WINDOW("f018_core DMA snapshot save: snapshot with DMA pending! Snapshot WILL BE incorrect on loading! FIXME!");	// FIXME!
	return xemusnap_write_sub_block(buffer, sizeof buffer);
}

#endif
