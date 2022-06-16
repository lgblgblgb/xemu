/* F018 DMA core emulation for MEGA65
   Part of the Xemu project.  https://github.com/lgblgblgb/xemu
   Copyright (C)2016-2022 LGB (Gábor Lénárt) <lgblgblgb@gmail.com>

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
#include "memory_mapper.h"
#include "io_mapper.h"
#include "xemu/cpu65.h"
#include "rom.h"


//#define DO_DEBUG_DMA

#ifdef DO_DEBUG_DMA
#	define DEBUGDMA(...) DEBUGPRINT(__VA_ARGS__)
#else
#	define DEBUGDMA(...)   DEBUG(__VA_ARGS__)
#endif

Uint8 dma_status;
Uint8 dma_registers[16];		// The four DMA registers (with last values written by the CPU)
int   dma_default_revision;		// DMA chip revision if non-enhanced DMA mode is used
int   dma_revision;			// current DMA revision throughout a DMA job or something
// Hacky stuff:
// low byte: the transparent byte value
// bit 8: zero = transprent mode is used, 1 = no DMA transparency is in used
// This is done this way to have a single 'if' to check both of enabled transparency and the transparent value,
// since the value (being 8 bit) to be written would never match values > $FF
static unsigned int dma_transparency;

enum dma_op_types {
	COPY_OP,
	MIX_OP,
	SWAP_OP,
	FILL_OP
};

static int   length;			// DMA operation length
static Uint8 length_byte3;		// extra high byte of DMA length (bits 23-16) to allow to have >64K DMA (during optlist read, combined into length during the actual DMA execution)
static int   command;			// DMA command (-1, no command yet) byte of DMA list reading
static enum  dma_op_types dma_op;	// two lower bits of "command"
static int   chained;			// 1 = chained (read next DMA operation "descriptor")
static int   list_addr;			// Current address of the DMA list, controller will read to "execute"
static int   list_addr_policy;		// 0 = normal, 1 = list addr by CPU addr, 2 = list addr by CPU addr WITH PC-writeback (3=temporary signal to fetch CPU-PC then go back to '2')
static Uint8 minterms[4];		// Used with MIX DMA command only
static int   in_dma_update;		// signal that DMA update do something. Currently only useful to avoid PANIC when DMA would modify its own registers
static int   dma_self_write_warning;	// Warning window policy for the event in case of the happening described in the comment of the previous line :)
static int   filler_byte;		// byte used for FILL DMA command only
static int   enhanced_dma;		// MEGA65 enhanced mode DMA


// In case of MEGA65 we should support fractional steps (8 bits for the fraction part).
// DMA_ADDR_FRACT_PART() is used only in debug functions to log
#define DMA_ADDR_INTEGER_PART(p)	((p)>>8)
#define DMA_SOURCE_SKIP_RATE		((int)(source.step_fract | (source.step_int << 8)))
#define DMA_TARGET_SKIP_RATE		((int)(target.step_fract | (target.step_int << 8)))
#define DMA_ADDR_FRACT_PART(p)		((p) & 0xFF)

#define DMA_ADDRESSING(channel)		((DMA_ADDR_INTEGER_PART(channel.addr) & channel.mask) + channel.base)

// source and target DMA "channels":
static struct {
	int   addr;		// address of the current operation, it's a fixed-point math value
	int   base;		// base address for "addr", always a "pure" number! It also contains the "megabyte selection", pre-shifted by << 20
	int   mask;		// mask value to handle warp around etc, eg 0xFFFF for 64K, 0xFFF for 4K (I/O)
	int   step;		// step value, zero(HOLD)/negative/positive, this is a fixed point arithmetic value!!
	Uint8 step_fract;	// step value during option read, fractional part only
	Uint8 step_int;		// step value during option read, integer part only
	Uint8 mbyte;		// megabyte slice selected during option read
	int   is_modulo;	// modulo mode, if it's not zero
	int   is_io;		// channel access I/O instead of memory, if it's not zero
} source, target;

static struct {
	int enabled, used, col_counter, col_limit, row_counter, row_limit, value;
} modulo;

// On C65, DMA cannot cross 64K boundaries, so the right mask is 0xFFFF
// On MEGA65 it seems to be 1Mbyte, thus the mask should be 0xFFFFF
#define MEM_ADDR_MASK	0xFFFFF

#define dma_read_source()	(XEMU_UNLIKELY(source.is_io) ? io_dma_reader(DMA_ADDRESSING(source)) : memory_dma_source_mreader(DMA_ADDRESSING(source)))
#define dma_read_target()	(XEMU_UNLIKELY(target.is_io) ? io_dma_reader(DMA_ADDRESSING(target)) : memory_dma_target_mreader(DMA_ADDRESSING(target)))

static XEMU_INLINE void dma_write_source ( Uint8 data )
{
	if (XEMU_LIKELY((unsigned int)data != dma_transparency)) {
		if (XEMU_UNLIKELY(source.is_io))
			io_dma_writer(DMA_ADDRESSING(source), data);
		else
			memory_dma_source_mwriter(DMA_ADDRESSING(source), data);
	}
}

static XEMU_INLINE void dma_write_target ( Uint8 data )
{
	if (XEMU_LIKELY((unsigned int)data != dma_transparency)) {
		if (XEMU_UNLIKELY(target.is_io))
			io_dma_writer(DMA_ADDRESSING(target), data);
		else
			memory_dma_source_mwriter(DMA_ADDRESSING(target), data);
	}
}

// On MEGA65 DMA list reading address never warps around (64K or 1M boundaries) BUT, we must take account the whole 28 bit memory address space at least
#define MEM_LIST_MASK 0xFFFFFFFU

#ifdef DO_DEBUG_DMA
static int dma_list_entry_pos = 0;
#endif

static Uint8 dma_read_list_next_byte ( void )
{
	Uint8 data;
	if (list_addr_policy) {
		list_addr &= 0xFFFF;
		data = cpu65_read_callback(list_addr);
	} else {
		list_addr &= MEM_LIST_MASK;
		data = memory_dma_list_reader(list_addr);
	}
#ifdef	DO_DEBUG_DMA
	DEBUGPRINT("DMA: reading DMA (rev#%d) list from $%08X [%s] [#%d]: $%02X" NL, dma_revision, list_addr, list_addr_policy ? "CPU-addr" : "linear-addr", dma_list_entry_pos, data);
	dma_list_entry_pos++;
#endif
	list_addr++;
	return data;
}

static XEMU_INLINE void copy_next ( void )
{
	dma_write_target(dma_read_source());
	source.addr += source.step;
	target.addr += target.step;
}

static XEMU_INLINE void fill_next ( void )
{
	dma_write_target(filler_byte);
	target.addr += target.step;
}

static XEMU_INLINE void swap_next ( void )
{
	Uint8 sa = dma_read_source();
	Uint8 da = dma_read_target();
	dma_write_source(da);
	dma_write_target(sa);
	source.addr += source.step;
	target.addr += target.step;
}

static XEMU_INLINE void mix_next ( void )
{
	Uint8 sa = dma_read_source();
	Uint8 da = dma_read_target();
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
	dma_write_target(da);
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
	dma_registers[addr] = data;
	// The rule here: every cases in the "switch" statement MUST return from the function, UNLESS the DMA register
	// write is about to initiate a new DMA session!
	switch (addr) {
		case 0x3:
			if (dma_default_revision != (data & 1)) {
				DEBUGPRINT("DMA: default DMA chip revision change %d -> %d because of writing DMA register 3" NL, dma_default_revision, data & 1);
				dma_default_revision = data & 1;
			}
			return;
		case 0x2:	// for compatibility with C65, MEGA65 here resets the MB part of the DMA list address
			dma_registers[4] = 0;	// this is the "MB" part of the DMA list address (hopefully ...)
			return;
		case 0xE:	// Set low order bits of DMA list address, without starting (MEGA65 feature, but without VIC4 new mode, this reg will never addressed here anyway)
			dma_registers[0] = data;
			return;
		default:
			return;
		case 0x0:
		case 0x5:
			// Normal DMA session (0) OR enhanced DMA session (5)
			enhanced_dma = !!addr;
			dma_registers[0] = data;
			list_addr = data | (dma_registers[1] << 8) | ((dma_registers[2] & 0xF) << 16) | (dma_registers[4] << 20);
			list_addr_policy = 0;
			break;	// Do NOT return but continue the function!
		case 0x6:
			// Enhanced DMA session, list by CPU address
			enhanced_dma = 1;
			dma_registers[0] = data;
			list_addr = data | (dma_registers[1] << 8);
			list_addr_policy = 1;
			break;	// Do NOT return but continue the function!
		case 0x7:
			// Enhanced DMA session, list by CPU address _AND_ using CPU's PC register!
			enhanced_dma = 1;
			list_addr_policy = 3;	// 3 signals to pick up CPU's PC in dma_update() then using '2'!! So list_addr will be set there.
			break;	// Do NOT return but continue the function!
	}
	/* --- FROM THIS POINT, THERE IS A NEW DMA SESSION WAS INITAITED BY SOME REGISTER WRITE --- */
	if (XEMU_UNLIKELY(dma_status))
		FATAL("dma_write_reg(): new DMA op with dma_status != 0");
	// Initial values, enhanced mode DMA may overrides this later during reading enhanced option list
	dma_revision = dma_default_revision;	// initialize current revision from default revision
	dma_transparency = 0x100;		// no DMA transparency by default
	source.step_fract = 0;			// source skip rate, fraction part
	source.step_int = 1;			// source skip rate, integer part
	target.step_fract = 0;			// target skip rate, fraction part
	target.step_int = 1;			// target skip rate, integer part
	source.mbyte = 0;			// source MB
	target.mbyte = 0;			// target MB
	length_byte3 = 0;
	if (enhanced_dma)
		DEBUGDMA("DMA: initiation of ENCHANCED MODE DMA!!!!\n");
	else
		DEBUGDMA("DMA: initiation of normal mode DMA\n");
	DEBUGDMA("DMA: list address is $%07X (%s) now, just written to register %d value $%02X @ PC=$%04X" NL, list_addr, list_addr_policy ? "CPU-addr" : "linear-addr", addr, data, cpu65.pc);
	dma_status = 0x80;	// DMA is busy now, also to signal the emulator core to call dma_update() in its main loop
	command = -1;		// signal dma_update() that it's needed to fetch the DMA command, no command is fetched yet
	cpu65.multi_step_stop_trigger = 1;	// trigger stopping multi-op CPU emulation mode, otherwise DMA wouldn't be started when it should be, right after triggering!
}


int dma_is_in_use ( void )
{
	return in_dma_update;
}


int dma_get_revision ( void )
{
	return dma_default_revision;
}


/* Main emulation loop should call this function regularly, if dma_status is not zero.
   This way we have 'real' DMA, ie works while the rest of the machine is emulated too.
   Please note, that the "exact" timing of DMA and eg the CPU is still incorrect, but it's far
   better than the previous version where DMA was "blocky", ie the whole machine was "halted" while DMA worked ...
   Note, the whole topic of the DMA is a bit large for this comment, here is a quite outdated
   description from me though: http://c65.lgb.hu/dma.html */
int dma_update ( void )
{
	int cycles = 0;
	if (XEMU_UNLIKELY(!dma_status))
		FATAL("dma_update() called with no dma_status set!");
	if (XEMU_UNLIKELY(command == -1)) {
		if (XEMU_UNLIKELY(list_addr_policy == 3)) {
			list_addr = cpu65.pc;
			DEBUGDMA("DMA: adjusting list addr to CPU PC: $%04X" NL, list_addr);
			list_addr_policy = 2;
		}
		if (enhanced_dma) {
			Uint8 opt, optval;
			do {
				opt = dma_read_list_next_byte();
				DEBUGDMA("DMA: enhanced option byte $%02X read" NL, opt);
				cycles++;
				if ((opt & 0x80)) {	// all options >= 0x80 have an extra bytes as option parameter
					optval = dma_read_list_next_byte();
					DEBUGDMA("DMA: enhanced option byte parameter $%02X read" NL, optval);
					cycles++;
				}
				switch (opt) {
					case 0x00:
						DEBUGDMA("DMA: end of enhanced options" NL);
						break;
					case 0x06:	// disable transparency (setting high byte of transparency, thus will never match)
						dma_transparency |= 0x100;
						break;
					case 0x07:	// enable transparency
						dma_transparency &= 0xFF;
						break;
					case 0x0A:
					case 0x0B:
						DEBUGDMA("DMA: per-session changing DMA revision during enhanced list %d -> %d" NL, dma_revision, opt - 0x0A);
						dma_revision = opt - 0x0A;
						break;
					case 0x80:	// set MB of source
						source.mbyte = optval;
						break;
					case 0x81:	// set MB of target
						target.mbyte = optval;
						break;
					case 0x82:	// set source skip rate, fraction part
						source.step_fract = optval;
						break;
					case 0x83:	// set source skip rate, integer part
						source.step_int = optval;
						break;
					case 0x84:	// set target skip rate, fraction part
						target.step_fract = optval;
						break;
					case 0x85:	// set target skip rate, integer part
						target.step_int = optval;
						break;
					case 0x86:	// byte value to be treated as "transparent" (ie: skip writing that data), if enabled
						dma_transparency = (dma_transparency & 0x100) | (unsigned int)optval;
						break;
					case 0x90:	// extra high byte of DMA length (bits 23-16) to allow to have >64K DMA
						length_byte3 = optval;
						break;
					default:
						// maybe later we should keep this quiet ...
						if ((opt & 0x80))
							DEBUGPRINT("DMA: *unknown* enhanced option: $%02X (parameter $%02X) @ PC=$%04X" NL, opt, optval, cpu65.pc);
						else
							DEBUGPRINT("DMA: *unknown* enhanced option: $%02X @ PC=$%04X" NL, opt, cpu65.pc);
						break;
				}
			} while (opt);
		}
		// command == -1 signals the situation, that the (next) DMA command should be read!
		// This part is highly incorrect, ie fetching so many bytes in one step only of dma_update()
		// NOTE: in case of MEGA65: dma_read_list_next_byte() uses the "megabyte" part already (taken from reg#4, in case if that reg is written)
#ifdef		DO_DEBUG_DMA
		dma_list_entry_pos = 0;
#endif
		command            = dma_read_list_next_byte();
		dma_op             = (enum dma_op_types)(command & 3);
		modulo.col_limit   = dma_read_list_next_byte();
		modulo.row_limit   = dma_read_list_next_byte();
		length             = modulo.col_limit | (modulo.row_limit << 8) | (length_byte3 << 16);
		filler_byte        = dma_read_list_next_byte()      ;			// source low byte is also the filler byte in case of FILL command
		source.addr        =(dma_read_list_next_byte() <<  8) | filler_byte;	// -- "" --
		source.addr       |= dma_read_list_next_byte() << 16;
		target.addr        = dma_read_list_next_byte()      ;
		target.addr       |= dma_read_list_next_byte() <<  8;
		target.addr       |= dma_read_list_next_byte() << 16;
		Uint8 subcommand;
		if (dma_revision)	// for F018B we have an extra byte fetch here! used later in this function [making DMA list one byte longer, indeed]
			subcommand = dma_read_list_next_byte();
		else
			subcommand = 0;	// just make gcc happy not to generate warning later
		// On MEGA65, modulo is used as a fixed point arithmetic value
		modulo.value       = dma_read_list_next_byte() <<  8;
		modulo.value      |= dma_read_list_next_byte() << 16;
		if (dma_revision) {
			cycles += 12;	// FIXME: correct timing?
			// F018B ("new") behaviour
			source.step  = (subcommand & 2) ? 0 : ((command & 16) ? -DMA_SOURCE_SKIP_RATE : DMA_SOURCE_SKIP_RATE);
			target.step  = (subcommand & 8) ? 0 : ((command & 32) ? -DMA_TARGET_SKIP_RATE : DMA_TARGET_SKIP_RATE);
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
			source.step  = (source.addr & 0x100000) ? 0 : ((source.addr & 0x400000) ? -DMA_SOURCE_SKIP_RATE : DMA_SOURCE_SKIP_RATE);
			target.step  = (target.addr & 0x100000) ? 0 : ((target.addr & 0x400000) ? -DMA_TARGET_SKIP_RATE : DMA_TARGET_SKIP_RATE);
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
			source.base	= 0;				// in case of I/O, base is not interpreted in Xemu (uses pure numbers 0-$FFF, no $DXXX, not even M65-spec mapping), and must be zero ...
			source.addr	= (source.addr & 0xFFF) << 8;	// for M65, it is fixed-point arithmetic
		} else {
			source.mask	= MEM_ADDR_MASK;		// in case of memory (not I/O) access, we have again a mask, see at "MEM_ADDR_MASK" for more explanation
			// base selection for M65
			// M65 has an "mbyte part" register for source (and target too)
			// however, with F018B there are 3 bits over 1Mbyte as well, and it seems M65 (see VHDL code) add these together then. Interesting.
			if (dma_revision)
			    source.base = (source.addr & 0x0F0000) | (((source.mbyte << 20) + (source.addr & 0x700000)) & 0xFF00000);
			else
			    source.base = (source.addr & 0x0F0000) | (  source.mbyte << 20);
			source.addr	= (source.addr & 0x00FFFF) << 8;// offset from base, for M65 this *IS* fixed point arithmetic!
		}
		/* target selection - see similar lines with comments above, for source ... */
		if (target.is_io) {
			target.mask	= 0xFFF;
			target.base	= 0;
			target.addr	= (target.addr & 0xFFF) << 8;
		} else {
			target.mask	= MEM_ADDR_MASK;
			if (dma_revision)
			    target.base = (target.addr & 0x0F0000) | (((target.mbyte << 20) + (target.addr & 0x700000)) & 0xFF00000);
			else
			    target.base = (target.addr & 0x0F0000) | (  target.mbyte << 20);
			target.addr	= (target.addr & 0x00FFFF) << 8;
		}
		/* other stuff */
		chained = (command & 4);
		// FIXME: this is a debug mesg, yeah, but with fractional step on M65, the step values needs to be interpreted with keep in mind the fixed point math ...
		DEBUG("DMA: READ COMMAND: $%07X[%s%s %d:%d] -> $%07X[%s%s %d:%d] (L=$%04X) CMD=%d (%s)" NL,
			DMA_ADDRESSING(source), source.is_io ? "I/O" : "MEM", source.is_modulo ? " MOD" : "", DMA_ADDR_INTEGER_PART(source.step), DMA_ADDR_FRACT_PART(source.step),
			DMA_ADDRESSING(target), target.is_io ? "I/O" : "MEM", target.is_modulo ? " MOD" : "", DMA_ADDR_INTEGER_PART(target.step), DMA_ADDR_FRACT_PART(target.step),
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
		} else {
			DEBUGDMA("DMA: end of operation, no chained next one." NL);
			dma_status = 0;		// end of DMA command
			if (list_addr_policy == 2) {
				DEBUGDMA("DMA: adjusting CPU PC from $%04X to $%04X" NL, cpu65.pc, list_addr & 0xFFFF);
				cpu65.pc = list_addr & 0xFFFF;
			}
		}
		command = -1;	// signal for next DMA command fetch
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


void dma_init_set_rev ( const Uint8 *rom )
{
	if (!rom)
		FATAL("%s(): rom == NULL", __func__);
	rom_detect_date(rom);
	const int rom_suggested_dma_revision = (rom_date < 900000 || rom_date > 910522 || rom_is_openroms);
	if (rom_date <= 0)
		WARNING_WINDOW("ROM version cannot be detected.\nDefaulting to revision %d.\nWarning, this may cause incorrect behaviour!", rom_suggested_dma_revision);
	DEBUGPRINT("DMA: default DMA chip revision change %d -> %d based on ROM version (%d %s)" NL, dma_default_revision, rom_suggested_dma_revision, rom_date, rom_name);
	dma_default_revision = rom_suggested_dma_revision;
	dma_registers[3] = dma_default_revision;
}


void dma_init ( void )
{
	modulo.enabled = 0;		// FIXME: make it configurable (real MEGA65 does not support modulo)
	dma_default_revision = 1;	// FIXME: what should be the power-on default revision?
	DEBUGPRINT("DMA: initializing DMA engine for chip revision %d (initially, may be modified later!), modulo_support=%s." NL,
		dma_default_revision,
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
	in_dma_update = 0;
	dma_self_write_warning = 1;
	dma_registers[3] = dma_default_revision;
}


Uint8 dma_read_reg ( int addr )
{
	Uint8 data;
	switch (addr) {
		case 0:
		case 5:
			data = dma_registers[0];
			break;
		case 1:
			data = dma_registers[1];
			break;
		case 2:
			data = dma_registers[2];	// FIXME: MS bit should be "reg_dmagic_withio"?!
			break;
		case 3:
			data = (dma_status & 0xFE) | dma_default_revision;	// reg_dmagic_status(7 downto 1) & support_f018b
			break;
		case 4:
			data = dma_registers[4];	// reg_dmagic_addr(27 downto 20);
			break;
		default:
			data = 0xFF;	// FIXME: the right choice of reading "unknown" DMA register?
			break;
	}
	DEBUGDMA("DMA: register reading at addr of %d as $%02X" NL, addr, data);
	return data;
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
	//dma_chip_revision_is_dynamic	= buffer[0x82];
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
	//buffer[0x82] = dma_chip_revision_is_dynamic ? 1 : 0;
	buffer[0x83] = modulo.enabled ? 1 : 0;
	buffer[0x84] = dma_status;		// bit useless to store (see below, actually it's a problem), but to think about the future ...
	buffer[0x85] = in_dma_update ? 1 : 0;	// -- "" --
	if (dma_status)
		WARNING_WINDOW("f018_core DMA snapshot save: snapshot with DMA pending! Snapshot WILL BE incorrect on loading! FIXME!");	// FIXME!
	return xemusnap_write_sub_block(buffer, sizeof buffer);
}

#endif
