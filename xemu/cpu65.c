/* Xemu - Somewhat lame emulation (running on Linux/Unix/Windows/OSX, utilizing
   SDL2) of some 8 bit machines, including the Commodore LCD and Commodore 65
   and MEGA65 as well.
   Copyright (C)2016-2020 LGB (Gábor Lénárt) <lgblgblgb@gmail.com>

   THIS IS AN UGLY PIECE OF SOURCE REALLY.

   Quite confusing comment section even at the beginning, from this point ...

   | This file tries to implement a 65C02, 65CE02 (as the form used in 4510 in C65,
   | also with its extension in the Mega65) and the NMOS 6502. This is kinda buggy
   | overal-behavioural emulation, ie there is some on-going tries for correct number
   | of cycles execution, but not for in-opcode timing.
   |
   | Note: the original solution was *generated* source, that can explain the structure.
   | Note: it was written in JavaScript, but the conversion to C and custom modification
   | Note: does not use this generation scheme anymore.
   |
   | BUGS/TODO:
   |
   | * Unimplemented illegal NMOS opcodes
   | * Incorrect timings in many cases
   | * Future plan to support NMOS CPU persona for Mega65 in C64 mode
   | * At many cases, emulation should be tested for correct opcode emulation behaviour

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

/* Original information/copyright (also written by me):
 * Commodore LCD emulator, C version.
 * (C)2013,2014 LGB Gabor Lenart
 * Visit my site (the better, JavaScript version of the emu is here too): http://commodore-lcd.lgb.hu/
 * Can be distributed/used/modified under the terms of GNU/GPL 2 (or later), please see file COPYING
 * or visit this page: http://www.gnu.org/licenses/gpl-2.0.html
 */

#include "xemu/emutools_basicdefs.h"
#ifndef CPU_CUSTOM_INCLUDED
#include "xemu/cpu65.h"
#endif

#ifdef MEGA65
#include "hypervisor.h"
#endif

#ifdef DEBUG_CPU
#include "xemu/cpu65ce02_disasm_tables.c"
//#include "cpu65ce02_disasm.c
#endif


struct cpu65_st CPU65;


#ifdef CPU_65CE02
#define	SP_HI CPU65.sphi
#define	ZP_HI CPU65.bphi
#define	ZERO_REG	CPU65.z
#define CPU_TYPE "65CE02"
#else
#define SP_HI	0x100
#define ZP_HI	0
#ifdef CPU_6502_NMOS_ONLY
#define CPU_TYPE "6502"
#else
#define CPU_TYPE "65C02"
#endif
#define ZERO_REG	0
#endif

#define A_OP(op,dat) CPU65.a = CPU65.a op dat


#ifdef MEGA65
//#warning "Compiling for MEGA65, hacky stuff!"
#define IS_FLAT32_DATA_OP() XEMU_UNLIKELY(CPU65.previous_op == 0xEA && cpu_linear_memory_addressing_is_enabled)
#endif

#ifdef CPU_65CE02
#	ifdef DEBUG_CPU
#		define OPC_65CE02(w) DEBUG("CPU: 65CE02 opcode: %s" NL, w)
#	else
#		define OPC_65CE02(w)
#	endif
#endif

#define TIMINGS_65CE02_	{7,5,2,2,4,3,4,4,3,2,1,1,5,4,5,4,2,5,5,3,4,3,4,4,1,4,1,1,5,4,5,4,5,5,7,7,3,3,4,4,3,2,1,1,4,4,5,4,2,5,5,3,3,3,4,4,1,4,1,1,4,4,5,4,5,5,2,2,4,3,4,4,3,2,1,1,3,4,5,4,2,5,5,3,4,3,4,4,2,4,3,1,4,4,5,4,4,5,7,5,3,3,4,4,3,2,1,1,5,4,5,4,2,5,5,3,3,3,4,4,2,4,3,1,5,4,5,4,2,5,6,3,3,3,3,4,1,2,1,4,4,4,4,4,2,5,5,3,3,3,3,4,1,4,1,4,4,4,4,4,2,5,2,2,3,3,3,4,1,2,1,4,4,4,4,4,2,5,5,3,3,3,3,4,1,4,1,4,4,4,4,4,2,5,2,6,3,3,4,4,1,2,1,7,4,4,5,4,2,5,5,3,3,3,4,4,1,4,3,3,4,4,5,4,2,5,6,6,3,3,4,4,1,2,1,7,4,4,5,4,2,5,5,3,5,3,4,4,1,4,3,3,7,4,5,4} // 65CE02 timing (my findings)
#define TIMINGS_65CE02	{7,5,2,2,4,3,4,4,3,2,1,1,5,4,5,4,2,5,5,3,4,3,4,4,1,4,1,1,5,4,5,4,2,5,7,7,4,3,4,4,3,2,1,1,5,4,4,4,2,5,5,3,4,3,4,4,1,4,1,1,5,4,5,4,5,5,2,2,4,3,4,4,3,2,1,1,3,4,5,4,2,5,5,3,4,3,4,4,1,4,3,3,4,4,5,4,4,5,7,5,3,3,4,4,3,2,1,1,5,4,5,4,2,5,5,3,3,3,4,4,2,4,3,1,5,4,5,4,2,5,6,3,3,3,3,4,1,2,1,4,4,4,4,4,2,5,5,3,3,3,3,4,1,4,1,4,4,4,4,4,2,5,2,2,3,3,3,4,1,2,1,4,4,4,4,4,2,5,5,3,3,3,3,4,1,4,1,4,4,4,4,4,2,5,2,6,3,3,4,4,1,2,1,7,4,4,5,4,2,5,5,3,3,3,4,4,1,4,3,3,4,4,5,4,2,5,6,6,3,3,4,4,1,2,1,6,4,4,5,4,2,5,5,3,5,3,4,4,1,4,3,3,7,4,5,4} // 65CE02 timing (from gs4510.vhdl)
#define TIMINGS_6502C65	{7,6,2,8,3,3,5,5,3,2,2,2,4,4,6,6,2,5,5,8,4,4,6,6,2,4,2,7,4,4,7,7,6,6,7,8,3,3,5,5,4,2,2,2,4,4,6,6,2,5,5,8,4,4,6,6,2,4,2,7,4,4,7,7,6,6,2,8,3,3,5,5,3,2,2,2,3,4,6,6,2,5,5,8,4,4,6,6,2,4,2,7,4,4,7,7,6,6,7,8,3,3,5,5,4,2,2,2,5,4,6,6,2,5,5,8,4,4,6,6,2,4,2,7,4,4,7,7,2,6,2,6,3,3,3,3,2,2,2,2,4,4,4,4,2,6,5,6,4,4,4,4,2,5,2,5,5,5,5,5,2,6,2,6,3,3,3,3,2,2,2,2,4,4,4,4,2,5,5,5,4,4,4,4,2,4,2,4,4,4,4,4,2,6,2,8,3,3,5,5,2,2,2,2,4,4,6,6,2,5,5,8,4,4,6,6,2,4,2,7,4,4,7,7,2,6,2,8,3,3,5,5,2,2,2,2,4,4,6,6,2,5,5,8,4,4,6,6,2,4,2,7,4,4,7,7} // 65CE02 with "dead cycles" to mimic NMOS 6502
#define TIMINGS_6502MOS	{7,6,0,8,3,3,5,5,3,2,2,2,4,4,6,6,2,5,0,8,4,4,6,6,2,4,2,7,4,4,7,7,6,6,0,8,3,3,5,5,4,2,2,2,4,4,6,6,2,5,0,8,4,4,6,6,2,4,2,7,4,4,7,7,6,6,0,8,3,3,5,5,3,2,2,2,3,4,6,6,2,5,0,8,4,4,6,6,2,4,2,7,4,4,7,7,6,6,0,8,3,3,5,5,4,2,2,2,5,4,6,6,2,5,0,8,4,4,6,6,2,4,2,7,4,4,7,7,2,6,2,6,3,3,3,3,2,2,2,2,4,4,4,4,2,6,0,6,4,4,4,4,2,5,2,5,5,5,5,5,2,6,2,6,3,3,3,3,2,2,2,2,4,4,4,4,2,5,0,5,4,4,4,4,2,4,2,4,4,4,4,4,2,6,2,8,3,3,5,5,2,2,2,2,4,4,6,6,2,5,0,8,4,4,6,6,2,4,2,7,4,4,7,7,2,6,2,8,3,3,5,5,2,2,2,2,4,4,6,6,2,5,0,8,4,4,6,6,2,4,2,7,4,4,7,7} // NMOS timing
#define TIMINGS_65C02	{7,6,2,2,5,3,5,5,3,2,2,2,6,4,6,2,2,5,5,2,5,4,6,5,2,4,2,2,6,4,7,2,6,6,2,2,3,3,5,5,4,2,2,2,4,4,6,2,2,5,5,2,4,4,6,5,2,4,2,2,4,4,7,2,6,6,2,2,3,3,5,5,3,2,2,2,3,4,6,2,2,5,5,2,4,4,6,5,2,4,3,2,2,4,7,2,6,6,2,2,3,3,5,5,4,2,2,2,5,4,6,2,2,5,5,2,4,4,6,5,2,4,4,2,6,4,7,2,3,6,2,2,3,3,3,5,2,2,2,2,4,4,4,2,2,6,5,2,4,4,4,5,2,5,2,2,4,5,5,2,2,6,2,2,3,3,3,5,2,2,2,2,4,4,4,2,2,5,5,2,4,4,4,5,2,4,2,2,4,4,4,2,2,6,2,2,3,3,5,5,2,2,2,2,4,4,6,2,2,5,5,2,4,4,6,5,2,4,3,2,2,4,7,2,2,6,2,2,3,3,5,5,2,2,2,2,4,4,6,2,2,5,5,2,4,4,6,5,2,4,4,2,2,4,7,2} // 65C02 timing

#ifdef CPU_65CE02
#	ifdef CPU65_65CE02_6502NMOS_TIMING_EMULATION
		static const Uint8 opcycles_slow_mode[0x100] = TIMINGS_6502C65;
		// static const Uint8 opcycles_fast_mode[0x100] = TIMINGS_65CE02_;
		static const Uint8 opcycles_fast_mode[0x100] = TIMINGS_65CE02;
		static const Uint8 *opcycles = opcycles_fast_mode;
		void cpu65_set_ce_timing ( int is_ce ) {
			opcycles = is_ce ? opcycles_fast_mode : opcycles_slow_mode;
		}
#	else
		//static const Uint8 opcycles[0x100] = TIMINGS_65CE02_;
		static const Uint8 opcycles[0x100] = TIMINGS_65CE02;
#endif
#else
#	ifdef CPU_6502_NMOS_ONLY
		static const Uint8 opcycles[0x100] = TIMINGS_6502MOS;
#	else
		static const Uint8 opcycles[0x100] = TIMINGS_65C02;
#	endif
#endif

#ifndef CPU65_DISCRETE_PF_NZ
#define VALUE_TO_PF_ZERO(a) ((a) ? 0 : CPU65_PF_Z)
#endif

#ifdef CPU65_DISCRETE_PF_NZ
#	define ASSIGN_PF_Z_BY_COND(a)	CPU65.pf_z = (a)
#	define ASSIGN_PF_N_BY_COND(a)	CPU65.pf_n = (a)
#else
#	define ASSIGN_PF_Z_BY_COND(a)	do { if (a) CPU65.pf_nz |= CPU65_PF_Z; else CPU65.pf_nz &= ~CPU65_PF_Z; } while(0)
#	define ASSIGN_PF_N_BY_COND(a)	do { if (a) CPU65.pf_nz |= CPU65_PF_N; else CPU65.pf_nz &= ~CPU65_PF_N; } while(0)
#endif


#define writeFlatAddressedByte(d)	cpu65_write_linear_opcode_callback(d)
#define readFlatAddressedByte()		cpu65_read_linear_opcode_callback()
#define writeFlatAddressedLong(d)	cpu65_write_linear_long_opcode_callback(d)
#define readFlatAddressedLong()		cpu65_read_linear_long_opcode_callback()
#ifdef CPU65_NO_RMW_EMULATION
#define writeByteTwice(a,od,nd)		cpu65_write_callback(a,nd)
#else
#define writeByteTwice(a,od,nd)		cpu65_write_rmw_callback(a,od,nd)
#endif
#define writeByte(a,d)			cpu65_write_callback(a,d)
#define readByte(a)			cpu65_read_callback(a)


/* Three possible behaviour in term of operating mode:
   * NMOS 6502 CPU core only (CPU_6502_NMOS_ONLY is defined)
   * 65C02/65CE02 mode only (real 65CE02 - Commodore 65 - or 65C02 - Commodore LCD) depending to CPU_65CE02 defined or not
   * Mega65, having NMOS persona _AS_WELL_ (CPU_65CE02 and MEGA65 macros are defined) */

#ifdef CPU_6502_NMOS_ONLY
/* NMOS CPU mode only, for emulators like VIC-20, etc. We define IS_CPU_NMOS as a constant 1, hopefully the C compiler is smart enough
   to see, that is an always true statement used with 'if', thus it won't generate any condition when used that way */
#define IS_CPU_NMOS			1
#define NMOS_JAM_OPCODE()		cpu65_illegal_opcode_callback()
#define HAS_NMOS_BUG_JMP_INDIRECT	1
#define HAS_NMOS_BUG_NO_PFD_RES_ON_INT	1
#define HAS_NMOS_BUG_BCD		1
#else
#ifdef MEGA65
/* Mega65 supports NMOS CPU persona as well, however the main performance bottleneck is more like the 50MHz 4510 mode, thus it's hinted as "unlikely" for the C compiler */
#define	IS_CPU_NMOS			XEMU_UNLIKELY(CPU65.nmos_mode)
#define	NMOS_JAM_OPCODE()		cpu65_illegal_opcode_callback()
#define HAS_NMOS_BUG_JMP_INDIRECT	M65_CPU_ALWAYS_BUG_JMP_INDIRECT || (M65_CPU_NMOS_ONLY_BUG_JMP_INDIRECT && CPU65.nmos_mode)
#define HAS_NMOS_BUG_NO_PFD_RES_ON_INT	M65_CPU_ALWAYS_BUG_NO_RESET_PFD_ON_INT || (M65_CPU_NMOS_ONLY_BUG_NO_RESET_PFD_ON_INT && CPU65.nmos_mode)
//#define HAS_NMOS_BUG_BCD		M65_CPU_ALWAYS_BUG_BCD || (M65_CPU_NMOS_ONLY_BUG_BCD && CPU65.nmos_mode)
// Note: it seems Mega65 is special, and would have the 6502-semantic of BCD even in native, etc mode!! This is NOT true for C65, for example!
#define HAS_NMOS_BUG_BCD		1
#else
/* 65C02 mode, eg for Commodore LCD or 65CE02 mode for Commodore 65 (based on the macro of CPU_65CE02). We define IS_CPU_NMOS as a constant 0, hopefully the C compiler is smart enough
   to see, that is an always true statement used with 'if', thus it won't generate any condition when used that way.
   NMOS_JAM_OPCODE() is a null-op, as it won't be ever referenced. */
#define IS_CPU_NMOS			0
#define NMOS_JAM_OPCODE()
#define HAS_NMOS_BUG_JMP_INDIRECT	0
#define HAS_NMOS_BUG_NO_PFD_RES_ON_INT	0
#define HAS_NMOS_BUG_BCD		0
#endif
#endif



static XEMU_INLINE Uint16 readWord(Uint16 addr) {
	return readByte(addr) | (readByte(addr + 1) << 8);
}


#ifdef MEGA65
static Uint32 readLong ( Uint16 addr ) {
	return
		 readByte(addr    )        |
		(readByte(addr + 1) << 8 ) |
		(readByte(addr + 2) << 16) |
		(readByte(addr + 3) << 24)
	;
}

static void writeLong ( Uint16 addr, Uint32 data ) {
	writeByte(addr    ,  data        & 0xFF);
	writeByte(addr + 1, (data >>  8) & 0xFF);
	writeByte(addr + 2, (data >> 16) & 0xFF);
	writeByte(addr + 3, (data >> 24) & 0xFF);
}
#endif


#ifdef CPU_65CE02
/* The stack pointer is a 16 bit register that has two modes. It can be programmed to be either an 8-bit page Programmable pointer, or a full 16-bit pointer.
   The processor status E bit selects which mode will be used. When set, the E bit selects the 8-bit mode. When reset, the E bit selects the 16-bit mode. */

static XEMU_INLINE void push ( Uint8 data )
{
	writeByte(CPU65.s | CPU65.sphi, data);
	CPU65.s--;
	if (XEMU_UNLIKELY(CPU65.s == 0xFF && (!CPU65.pf_e))) {
		CPU65.sphi -= 0x100;
#ifdef DEBUG_CPU
		DEBUG("CPU: 65CE02: SPHI changed to $%04X" NL, CPU65.sphi);
#endif
	}
}
static XEMU_INLINE Uint8 pop ( void )
{
	CPU65.s++;
	if (XEMU_UNLIKELY(CPU65.s == 0 && (!CPU65.pf_e))) {
		CPU65.sphi += 0x100;
#ifdef DEBUG_CPU
		DEBUG("CPU: 65CE02: SPHI changed to $%04X" NL, CPU65.sphi);
#endif
	}
	return readByte(CPU65.s | CPU65.sphi);
}
#else
#define push(data) writeByte(((Uint8)(CPU65.s--)) | SP_HI, data)
#define pop() readByte(((Uint8)(++CPU65.s)) | SP_HI)
#endif

static XEMU_INLINE void  pushWord(Uint16 data) { push(data >> 8); push(data & 0xFF); }
static XEMU_INLINE Uint16 popWord() { Uint16 temp = pop(); return temp | (pop() << 8); }


#ifdef CPU_65CE02
// FIXME: remove this, if we don't need!
// NOTE!! Interesting, it seems PHW opcodes pushes the word the OPPOSITE direction as eg JSR would push the PC ...
#define PUSH_FOR_PHW pushWord_rev
static XEMU_INLINE void  pushWord_rev(Uint16 data) { push(data & 0xFF); push(data >> 8); }
#endif


void cpu65_set_pf(Uint8 st) {
#ifdef CPU65_DISCRETE_PF_NZ
	CPU65.pf_n = st & CPU65_PF_N;
	CPU65.pf_z = st & CPU65_PF_Z;
#else
	CPU65.pf_nz = st & (CPU65_PF_N | CPU65_PF_Z);
#endif
	CPU65.pf_v = st & CPU65_PF_V;
#ifdef CPU_65CE02
	// Note: E bit cannot be changed by PLP/RTI, so it's commented out here ...
	// At least *I* think :) FIXME?
	// CPU65.pf_e = st & CPU65_PF_E;
#endif
	CPU65.pf_d = st & CPU65_PF_D;
	CPU65.pf_i = st & CPU65_PF_I;
	CPU65.pf_c = st & CPU65_PF_C;
}

Uint8 cpu65_get_pf() {
	return
#ifdef CPU65_DISCRETE_PF_NZ
	(CPU65.pf_n ? CPU65_PF_N : 0) | (CPU65.pf_z ? CPU65_PF_Z : 0)
#else
	CPU65.pf_nz
#endif
	|
	(CPU65.pf_v ?  CPU65_PF_V : 0) |
#ifdef CPU_65CE02
	// FIXME: for M65 in NMOS-persona, some should always force '1' here,
	// however, that's a bit cryptic for me, as it would be simplier to actual E flag
	// would be 1, as for NMOS, the stack should be not in 16 bits mode, I guess ...
	(CPU65.pf_e ?  CPU65_PF_E : 0) |
#else
	CPU65_PF_E |
#endif
	(CPU65.pf_d ? CPU65_PF_D : 0) |
	(CPU65.pf_i ? CPU65_PF_I : 0) |
	(CPU65.pf_c ? CPU65_PF_C : 0);
}

void cpu65_reset() {
	cpu65_set_pf(0x34);
	CPU65.s = 0xFF;
	CPU65.irqLevel = CPU65.nmiEdge = 0;
	CPU65.op_cycles = 0;
	CPU65.a = 0;
	CPU65.x = 0;
	CPU65.y = 0;
#ifdef CPU_65CE02
	CPU65.pf_e = 1;
	CPU65.z = 0;
	CPU65.bphi = 0x0000;
	CPU65.sphi = 0x0100;
	CPU65.cpu_inhibit_interrupts = 0;
#endif
#ifdef MEGA65
	CPU65.nmos_mode = 0;
	CPU65.previous_op = 0;
	CPU65.neg_neg_prefix = 0;
#endif
	CPU65.pc = readWord(0xFFFC);
	DEBUGPRINT("CPU[" CPU_TYPE "]: RESET, PC=%04X, BCD_behaviour=%s" NL,
			CPU65.pc, // FIXME
			HAS_NMOS_BUG_BCD ? "NMOS-6502" : "65C02+"
	);
}


static XEMU_INLINE void SET_NZ(Uint8 st) {
#ifdef CPU65_DISCRETE_PF_NZ
	CPU65.pf_n = st & CPU65_PF_N;
	CPU65.pf_z = !st;
#else
	CPU65.pf_nz = (st & CPU65_PF_N) | VALUE_TO_PF_ZERO(st);
#endif
}
#ifdef CPU_65CE02
static XEMU_INLINE void SET_NZ16(Uint16 st) {
#ifdef CPU65_DISCRETE_PF_NZ
	CPU65.pf_n = st & 0x8000;
	CPU65.pf_z = !st;
#else
	CPU65.pf_nz = ((st & 0x8000) >> 8) | VALUE_TO_PF_ZERO(st);
#endif
}
#endif

#define _imm() (CPU65.pc++)
static XEMU_INLINE Uint16 _abs() {
	Uint16 o = readByte(CPU65.pc++);
	return o | (readByte(CPU65.pc++) << 8);
}
#define _absx() ((Uint16)(_abs() + CPU65.x))
#define _absy() ((Uint16)(_abs() + CPU65.y))
#define _absi() readWord(_abs())
#define _absxi() readWord(_absx())
#define _zp() (readByte(CPU65.pc++) | ZP_HI)

static XEMU_INLINE Uint16 _zpi() {
	Uint8 a = readByte(CPU65.pc++);
#ifdef CPU_65CE02
	return (readByte(a | ZP_HI) | (readByte(((a + 1) & 0xFF) | ZP_HI) << 8)) + CPU65.z;
#else
	return  readByte(a | ZP_HI) | (readByte(((a + 1) & 0xFF) | ZP_HI) << 8);
#endif
}

static XEMU_INLINE Uint16 _zpiy() {
	Uint8 a = readByte(CPU65.pc++);
	return (readByte(a | ZP_HI) | (readByte(((a + 1) & 0xFF) | ZP_HI) << 8)) + CPU65.y;
}


#define _zpx() (((readByte(CPU65.pc++) + CPU65.x) & 0xFF) | ZP_HI)
#define _zpy() (((readByte(CPU65.pc++) + CPU65.y) & 0xFF) | ZP_HI)

static XEMU_INLINE Uint16 _zpxi() {
	Uint8 a = readByte(CPU65.pc++) + CPU65.x;
	return readByte(a | ZP_HI) | (readByte(((a + 1) & 0xFF) | ZP_HI) << 8);
}

static XEMU_INLINE void _BRA(int cond) {
	 if (cond) {
		int temp = readByte(CPU65.pc);
		if (temp & 128) temp = CPU65.pc - (temp ^ 0xFF);
		else temp = CPU65.pc + temp + 1;
		if ((temp & 0xFF00) != (CPU65.pc & 0xFF00)) CPU65.op_cycles++;
		CPU65.pc = temp;
		CPU65.op_cycles++;
	} else
		CPU65.pc++;
}
#ifdef CPU_65CE02
static XEMU_INLINE void _BRA16(int cond) {
	if (cond) {
		// Note: 16 bit PC relative stuffs works a bit differently as 8 bit ones, not the same base of the offsets!
		CPU65.pc += 1 + (Sint16)(readByte(CPU65.pc) | (readByte(CPU65.pc + 1) << 8));
		CPU65.op_cycles++;
	} else
		CPU65.pc += 2;
}
// Used by LDA/STA (nn,SP), Y opcodes
/* Big fat NOTE/FIXME/TODO:
   See the question #1/2, even two places where stack 'warping around' effect can
   be an interesting question in 8 bit stack mode.
*/
static XEMU_INLINE Uint16 _GET_SP_INDIRECT_ADDR ( void )
{
	int tmp2;
	int tmp = CPU65.s + readByte(CPU65.pc++);
	if (CPU65.pf_e)		// FIXME: question #1: is E flag affects this addressing mode this way
		tmp &= 0xFF;
	tmp2 = readByte((CPU65.sphi + tmp) & 0xFFFF);
	tmp++;
	if (CPU65.pf_e)		// FIXME: question #2: what happens if lo/hi bytes would be used at exactly at 'wrapping the stack' around case, with 8 bit stack mode?
		tmp &= 0xFF;
	tmp2 |= readByte((CPU65.sphi + tmp) & 0xFFFF) << 8;
	return (Uint16)(tmp2 + CPU65.y);
}
#endif
static XEMU_INLINE void _CMP(Uint8 reg, Uint8 data) {
	Uint16 temp = reg - data;
	CPU65.pf_c = temp < 0x100;
	SET_NZ(temp);
}
static XEMU_INLINE void _TSB(int addr) {
	Uint8 m = readByte(addr);
#ifdef CPU65_DISCRETE_PF_NZ
	CPU65.pf_z = (!(m & CPU65.a));
#else
	if (m & CPU65.a) CPU65.pf_nz &= (~CPU65_PF_Z); else CPU65.pf_nz |= CPU65_PF_Z;
#endif
	writeByte(addr, m | CPU65.a);
}
static XEMU_INLINE void _TRB(int addr) {
	Uint8 m = readByte(addr);
#ifdef CPU65_DISCRETE_PF_NZ
	CPU65.pf_z = (!(m & CPU65.a));
#else
	if (m & CPU65.a) CPU65.pf_nz &= (~CPU65_PF_Z); else CPU65.pf_nz |= CPU65_PF_Z;
#endif
	writeByte(addr, m & (255 - CPU65.a));
}
static XEMU_INLINE void _ASL(int addr) {
	Uint8 t = (addr == -1 ? CPU65.a : readByte(addr));
	Uint8 o = t;
	CPU65.pf_c = t & 128;
	//t = (t << 1) & 0xFF;
	t <<= 1;
	SET_NZ(t);
	if (addr == -1) CPU65.a = t; else writeByteTwice(addr, o, t);
}
static XEMU_INLINE void _LSR(int addr) {
	Uint8 t = (addr == -1 ? CPU65.a : readByte(addr));
	Uint8 o = t;
	CPU65.pf_c = t & 1;
	//t = (t >> 1) & 0xFF;
	t >>= 1;
	SET_NZ(t);
	if (addr == -1) CPU65.a = t; else writeByteTwice(addr, o, t);
}
#ifdef CPU_65CE02
static XEMU_INLINE void _ASR(int addr) {
	Uint8 t = (addr == -1 ? CPU65.a : readByte(addr));
	Uint8 o = t;
	CPU65.pf_c = t & 1;
	t = (t >> 1) | (t & 0x80);
	SET_NZ(t);
	if (addr == -1) CPU65.a = t; else writeByteTwice(addr, o, t);
}
#endif
static XEMU_INLINE void _BIT(Uint8 data) {
	CPU65.pf_v = data & 64;
#ifdef CPU65_DISCRETE_PF_NZ
	CPU65.pf_n = data & CPU65_PF_N;
	CPU65.pf_z = (!(CPU65.a & data));
#else
	CPU65.pf_nz = (data & CPU65_PF_N) | VALUE_TO_PF_ZERO(CPU65.a & data);
#endif
}
static XEMU_INLINE void _ADC(unsigned int data) {
	if (XEMU_UNLIKELY(CPU65.pf_d)) {
		if (HAS_NMOS_BUG_BCD) {
			/* This algorithm was written according the one found in VICE: 6510core.c */
			unsigned int temp = (CPU65.a & 0xF) + (data & 0xF) + (CPU65.pf_c ? 1 : 0);
			if (temp > 0x9)
				temp += 6;
			if (temp <= 0x0F)
				temp = (temp & 0xF) + (CPU65.a & 0xF0) + (data & 0xF0);
			else
				temp = (temp & 0xF) + (CPU65.a & 0xF0) + (data & 0xF0) + 0x10;
			ASSIGN_PF_Z_BY_COND(!((CPU65.a + data + (CPU65.pf_c ? 1 : 0)) & 0xFF));
			ASSIGN_PF_N_BY_COND(temp & 0x80);
			CPU65.pf_v = (((CPU65.a ^ temp) & 0x80)  && !((CPU65.a ^ data) & 0x80));
			if ((temp & 0x1F0) > 0x90)
				temp += 0x60;
			CPU65.pf_c = ((temp & 0xFF0) > 0xF0);
			CPU65.a = temp & 0xFF;
		} else {
			/* This algorithm was written according the one found in VICE: 65c02core.c */
			unsigned int temp  = (CPU65.a & 0x0F) + (data & 0x0F) + (CPU65.pf_c ? 1 : 0);
			unsigned int temp2 = (CPU65.a & 0xF0) + (data & 0xF0);
			if (temp > 9) { temp2 += 0x10; temp += 6; }
			CPU65.pf_v = (~(CPU65.a ^ data) & (CPU65.a ^ temp) & 0x80);
			if (temp2 > 0x90) temp2 += 0x60;
			CPU65.pf_c = (temp2 & 0xFF00);
			CPU65.a = (temp & 0x0F) + (temp2 & 0xF0);
			SET_NZ(CPU65.a);
		}
	} else {
		unsigned int temp = data + CPU65.a + (CPU65.pf_c ? 1 : 0);
		CPU65.pf_c = temp > 0xFF;
		CPU65.pf_v = (!((CPU65.a ^ data) & 0x80) && ((CPU65.a ^ temp) & 0x80));
		CPU65.a = temp & 0xFF;
		SET_NZ(CPU65.a);
	}
}
static XEMU_INLINE void _SBC(Uint16 data) {
	if (XEMU_UNLIKELY(CPU65.pf_d)) {
		if (HAS_NMOS_BUG_BCD) {
			/* This algorithm was written according the one found in VICE: 6510core.c */
			Uint16 temp = CPU65.a - data - (CPU65.pf_c ? 0 : 1);
			unsigned int temp_a = (CPU65.a & 0xF) - (data & 0xF) - (CPU65.pf_c ? 0 : 1);
			if (temp_a & 0x10)
				temp_a = ((temp_a - 6) & 0xf) | ((CPU65.a & 0xf0) - (data & 0xf0) - 0x10);
			else
				temp_a = (temp_a & 0xf) | ((CPU65.a & 0xf0) - (data & 0xf0));
			if (temp_a & 0x100)
				temp_a -= 0x60;
			CPU65.pf_c = (temp < 0x100);
			SET_NZ(temp & 0xFF);
			CPU65.pf_v = (((CPU65.a ^ temp) & 0x80) && ((CPU65.a ^ data) & 0x80));
			CPU65.a = temp_a & 0xFF;
		} else {
			/* This algorithm was written according the one found in VICE: 65c02core.c */
			Uint16 temp = CPU65.a - (data & 0x0F) - (CPU65.pf_c ? 0 : 1);
			if ((temp & 0x0F) > (CPU65.a & 0x0F)) temp -= 6;
			temp -= (data & 0xF0);
			if ((temp & 0xF0) > (CPU65.a & 0xF0)) temp -= 0x60;
			CPU65.pf_v = (!(temp > CPU65.a));
			CPU65.pf_c = (!(temp > CPU65.a));
			CPU65.a = temp & 0xFF;
			SET_NZ(CPU65.a);
		}
	} else {
		Uint16 temp = CPU65.a - data - (CPU65.pf_c ? 0 : 1);
		CPU65.pf_c = temp < 0x100;
		CPU65.pf_v = ((CPU65.a ^ temp) & 0x80) && ((CPU65.a ^ data) & 0x80);
		CPU65.a = temp & 0xFF;
		SET_NZ(CPU65.a);
	}
}
static XEMU_INLINE void _ROR(int addr) {
	Uint16 t = ((addr == -1) ? CPU65.a : readByte(addr));
	Uint8  o = t;
	if (CPU65.pf_c) t |= 0x100;
	CPU65.pf_c = t & 1;
	t >>= 1;
	SET_NZ(t);
	if (addr == -1) CPU65.a = t; else writeByteTwice(addr, o, t);
}
static XEMU_INLINE void _ROL(int addr) {
	Uint16 t = ((addr == -1) ? CPU65.a : readByte(addr));
	Uint8  o = t;
	t = (t << 1) | (CPU65.pf_c ? 1 : 0);
	CPU65.pf_c = t & 0x100;
	t &= 0xFF;
	SET_NZ(t);
	if (addr == -1) CPU65.a = t; else writeByteTwice(addr, o, t);
}



int cpu65_step (
#ifdef CPU_STEP_MULTI_OPS
	int run_for_cycles
#endif
) {
#ifdef CPU_STEP_MULTI_OPS
	int all_cycles = 0;
	do {
#endif
	if (XEMU_UNLIKELY(CPU65.nmiEdge
#ifdef CPU_65CE02
		&& CPU65.op_cycles != 1 && !CPU65.cpu_inhibit_interrupts
#endif
#ifdef MEGA65
		&& !in_hypervisor
#endif
	)) {
#ifdef DEBUG_CPU
		DEBUG("CPU: serving NMI on NMI edge at PC $%04X" NL, CPU65.pc);
#endif
		CPU65.nmiEdge = 0;
		pushWord(CPU65.pc);
		push(cpu65_get_pf());	// no CPU65_PF_B is pushed!
		CPU65.pf_i = 1;
		CPU65.pf_d = 0;			// NOTE: D flag clearing was not done on the original 6502 I guess, but indeed on the 65C02 already
		CPU65.pc = readWord(0xFFFA);
#ifdef CPU_STEP_MULTI_OPS
		all_cycles += 7;
		continue;
#else
		return 7;
#endif
	}
	if (XEMU_UNLIKELY(CPU65.irqLevel && (!CPU65.pf_i)
#ifdef CPU_65CE02
		&& CPU65.op_cycles != 1 && !CPU65.cpu_inhibit_interrupts
#endif
#ifdef MEGA65
		&& !in_hypervisor
#endif
	)) {
#ifdef DEBUG_CPU
		DEBUG("CPU: servint IRQ on IRQ level at PC $%04X" NL, CPU65.pc);
#endif
		pushWord(CPU65.pc);
		push(cpu65_get_pf());	// no CPU65_PF_B is pushed!
		CPU65.pf_i = 1;
		CPU65.pf_d = 0;
		CPU65.pc = readWord(0xFFFE);
#ifdef CPU_STEP_MULTI_OPS
		all_cycles += 7;
		continue;
#else
		return 7;
#endif
	}
	CPU65.old_pc = CPU65.pc;
#ifdef DEBUG_CPU
	if (CPU65.pc == 0)
		DEBUG("CPU: WARN: PC at zero!" NL);
#endif
#ifdef MEGA65
	CPU65.previous_op = CPU65.op;
#endif
	CPU65.op = readByte(CPU65.pc++);
#ifdef DEBUG_CPU
	DEBUG("CPU: at $%04X opcode = $%02X %s %s A=%02X X=%02X Y=%02X Z=%02X SP=%02X" NL, (CPU65.pc - 1) & 0xFFFF, CPU65.op, opcode_names[CPU65.op], opcode_adm_names[opcode_adms[CPU65.op]],
		CPU65.a, CPU65.x, CPU65.y, CPU65.z, CPU65.s
	);
	if (CPU65.op == 0x60)
		DEBUG("CPU: SP before RTS is (SPHI=$%04X) SP=$%02X" NL, CPU65.sphi, CPU65.s);
#endif
#ifdef CPU65_TRAP_OPCODE
	if (XEMU_UNLIKELY(CPU65.op == CPU65_TRAP_OPCODE)) {
		int ret = cpu65_trap_callback(CPU65_TRAP_OPCODE);
		if (ret > 0)
			return ret;
	}
#endif
	CPU65.op_cycles = opcycles[CPU65.op];
	switch (CPU65.op) {
	case 0x00:	/* BRK Implied */
#ifdef DEBUG_CPU
			DEBUG("CPU: WARN: BRK is about executing at PC=$%04X" NL, (CPU65.pc - 1) & 0xFFFF);
#endif
			pushWord(CPU65.pc + 1);
			push(cpu65_get_pf() | CPU65_PF_B);	// BRK always pushes 'B' bit set (like PHP too, unlike hardware interrupts
			CPU65.pf_d = 0;				// actually, NMOS CPU does not do this for real, only 65C02+
			CPU65.pf_i = 1;
			CPU65.pc = readWord(0xFFFE);
			break;
	case 0x01:	/* ORA (Zero_Page,X) */
			SET_NZ(A_OP(|,readByte(_zpxi())));
			break;
	case 0x02:	/* 65C02: NOP imm (non-std NOP with addr mode), 65CE02: CLE
			   NMOS: KIL */
			if (IS_CPU_NMOS) { NMOS_JAM_OPCODE(); } else {
#ifdef CPU_65CE02
			OPC_65CE02("CLE");
			CPU65.pf_e = 0;	// 65CE02: CLE
#ifdef DEBUG_CPU
			DEBUG("CPU: WARN: E flag is cleared!" NL);
#endif
#else
			CPU65.pc++; /* NOP imm (non-std NOP with addr mode) */
#endif
			}
			break;
	case 0x03:	/* $03 65C02: NOP (nonstd loc, implied), 65CE02: SEE
			   NMOS: SLO ($00,X) -> TODO */
			if (IS_CPU_NMOS) { NMOS_JAM_OPCODE(); } else {
#ifdef CPU_65CE02
			OPC_65CE02("SEE");
			CPU65.pf_e = 1;	// 65CE02: SEE
#endif
			}
			break;
	case 0x04:	/* TSB Zero_Page */
			if (IS_CPU_NMOS) { NMOS_JAM_OPCODE(); } else { _TSB(_zp()); }
			break;
	case 0x05:	/* ORA Zero_Page */
			SET_NZ(A_OP(|,readByte(_zp())));
			break;
	case 0x06:	/* ASL Zero_Page */
			_ASL(_zp());
			break;
	case 0x07:	/* RMB Zero_Page */
			if (IS_CPU_NMOS) { NMOS_JAM_OPCODE(); } else {
			int a = _zp();
			writeByte(a, readByte(a) & 254);
			}
			break;
	case 0x08:	/* PHP Implied */
			push(cpu65_get_pf() | CPU65_PF_B);
			break;
	case 0x09:	/* ORA Immediate */
			SET_NZ(A_OP(|,readByte(_imm())));
			break;
	case 0x0A:	/* ASL Accumulator */
			_ASL(-1);
			break;
	case 0x0B:	/* 65C02: NOP (nonstd loc, implied), 65CE02: TSY */
			if (IS_CPU_NMOS) { NMOS_JAM_OPCODE(); } else {
#ifdef CPU_65CE02
			OPC_65CE02("TSY");
			SET_NZ(CPU65.y = (CPU65.sphi >> 8));	// TSY
#endif
			}
			break;
	case 0x0C:	/* TSB Absolute */
			if (IS_CPU_NMOS) { NMOS_JAM_OPCODE(); } else {
			_TSB(_abs());
			}
			break;
	case 0x0D:	/* ORA Absolute */
			SET_NZ(A_OP(|,readByte(_abs())));
			break;
	case 0x0E:	/* ASL Absolute */
			_ASL(_abs());
			break;
	case 0x0F:	/* BBR Relative */
			if (IS_CPU_NMOS) { NMOS_JAM_OPCODE(); } else {
			_BRA(!(readByte(_zp()) & 1));
			}
			break;
	case 0x10:	/* BPL Relative */
#ifdef CPU65_DISCRETE_PF_NZ
			_BRA(! CPU65.pf_n);
#else
			_BRA(!(CPU65.pf_nz & CPU65_PF_N));
#endif
			break;
	case 0x11:	/* ORA (Zero_Page),Y */
			SET_NZ(A_OP(|,readByte(_zpiy())));
			break;
	case 0x12:	/* ORA (Zero_Page) or (ZP),Z on 65CE02 */
			if (IS_CPU_NMOS) { NMOS_JAM_OPCODE(); } else {
#ifdef MEGA65
			if (IS_FLAT32_DATA_OP())
				SET_NZ(A_OP(|,readFlatAddressedByte()));
			else
#endif
				SET_NZ(A_OP(|,readByte(_zpi())));
			}
			break;
	case 0x13:	/* 65C02: NOP (nonstd loc, implied), 65CE02: BPL 16 bit relative */
			if (IS_CPU_NMOS) { NMOS_JAM_OPCODE(); } else {
#ifdef CPU_65CE02
			OPC_65CE02("BPL16");
#ifdef CPU65_DISCRETE_PF_NZ
			_BRA16(!CPU65.pf_n);
#else
			_BRA16(!(CPU65.pf_nz & CPU65_PF_N));
#endif
#endif
			}
			break;
	case 0x14:	/* TRB Zero_Page */
			if (IS_CPU_NMOS) { NMOS_JAM_OPCODE(); } else {
			_TRB(_zp());
			}
			break;
	case 0x15:	/* ORA Zero_Page,X */
			SET_NZ(A_OP(|,readByte(_zpx())));
			break;
	case 0x16:	/* ASL Zero_Page,X */
			_ASL(_zpx());
			break;
	case 0x17:	/* RMB Zero_Page */
			if (IS_CPU_NMOS) { NMOS_JAM_OPCODE(); } else {
			int a = _zp(); writeByte(a, readByte(a) & 253);
			}
			break;
	case 0x18:	/* CLC Implied */
			CPU65.pf_c = 0;
			break;
	case 0x19:	/* ORA Absolute,Y */
			SET_NZ(A_OP(|,readByte(_absy())));
			break;
	case 0x1A:	/* INA Accumulator */
			if (IS_CPU_NMOS) { NMOS_JAM_OPCODE(); } else {
			SET_NZ(++CPU65.a);
			}
			break;
	case 0x1B:	/* 65C02: NOP (nonstd loc, implied), 65CE02: INZ */
			if (IS_CPU_NMOS) { NMOS_JAM_OPCODE(); } else {
#ifdef CPU_65CE02
			OPC_65CE02("INZ");
			SET_NZ(++CPU65.z);
#endif
			}
			break;
	case 0x1C:	/* TRB Absolute */
			if (IS_CPU_NMOS) { NMOS_JAM_OPCODE(); } else {
			_TRB(_abs());
			}
			break;
	case 0x1D:	/* ORA Absolute,X */
			SET_NZ(A_OP(|,readByte(_absx())));
			break;
	case 0x1E:	/* ASL Absolute,X */
			_ASL(_absx());
			break;
	case 0x1F:	/* BBR Relative */
			if (IS_CPU_NMOS) { NMOS_JAM_OPCODE(); } else {
			_BRA(!(readByte(_zp()) & 2));
			}
			break;
	case 0x20:	/* JSR Absolute */
			pushWord(CPU65.pc + 1);
			CPU65.pc = _abs();
			break;
	case 0x21:	/* AND (Zero_Page,X) */
			SET_NZ(A_OP(&,readByte(_zpxi())));
			break;
	case 0x22:	/* 65C02 NOP imm (non-std NOP with addr mode), 65CE02: JSR (nnnn) */
			if (IS_CPU_NMOS) { NMOS_JAM_OPCODE(); } else {
#ifdef CPU_65CE02
			OPC_65CE02("JSR (nnnn)");
			// 65CE02 JSR ($nnnn)
			pushWord(CPU65.pc + 1);
			CPU65.pc = _absi();
#else
			CPU65.pc++;
#endif
			}
			break;
	case 0x23:	/* 65C02 NOP (nonstd loc, implied), 65CE02: JSR (nnnn,X) */
			if (IS_CPU_NMOS) { NMOS_JAM_OPCODE(); } else {
#ifdef CPU_65CE02
			OPC_65CE02("JSR (nnnn,X)");
			pushWord(CPU65.pc + 1);
			CPU65.pc = _absxi();
#endif
			}
			break;
	case 0x24:	/* BIT Zero_Page */
			_BIT(readByte(_zp()));
			break;
	case 0x25:	/* AND Zero_Page */
			SET_NZ(A_OP(&,readByte(_zp())));
			break;
	case 0x26:	/* ROL Zero_Page */
			_ROL(_zp());
			break;
	case 0x27:	/* RMB Zero_Page */
			if (IS_CPU_NMOS) { NMOS_JAM_OPCODE(); } else {
			int a = _zp();
			writeByte(a, readByte(a) & 251);
			}
			break;
	case 0x28:	/* PLP Implied */
			cpu65_set_pf(pop());
			break;
	case 0x29:	/* AND Immediate */
			SET_NZ(A_OP(&,readByte(_imm())));
			break;
	case 0x2A:	/* ROL Accumulator */
			_ROL(-1);
			break;
	case 0x2B:	/* 65C02: NOP (nonstd loc, implied), 65CE02: TYS */
			if (IS_CPU_NMOS) { NMOS_JAM_OPCODE(); } else {
#ifdef CPU_65CE02
			OPC_65CE02("TYS");
			CPU65.sphi = CPU65.y << 8;	// 65CE02 TYS
#ifdef DEBUG_CPU
			if (CPU65.sphi != 0x100)
				DEBUG("CPU: WARN: stack page is set non-0x100: $%04X" NL, CPU65.sphi);
#endif
#endif
			}
			break;
	case 0x2C:	/* BIT Absolute */
			_BIT(readByte(_abs()));
			break;
	case 0x2D:	/* AND Absolute */
			SET_NZ(A_OP(&,readByte(_abs())));
			break;
	case 0x2E:	/* ROL Absolute */
			_ROL(_abs());
			break;
	case 0x2F:	/* BBR Relative */
			if (IS_CPU_NMOS) { NMOS_JAM_OPCODE(); } else {
			_BRA(!(readByte(_zp()) & 4));
			}
			break;
	case 0x30:	/* BMI Relative */
#ifdef CPU65_DISCRETE_PF_NZ
			_BRA(CPU65.pf_n);
#else
			_BRA(CPU65.pf_nz & CPU65_PF_N);
#endif
			break;
	case 0x31:	/* AND (Zero_Page),Y */
			SET_NZ(A_OP(&,readByte(_zpiy())));
			break;
	case 0x32:	/* 65C02: AND (Zero_Page), 65CE02: AND (ZP),Z */
			if (IS_CPU_NMOS) { NMOS_JAM_OPCODE(); } else {
#ifdef MEGA65
			if (IS_FLAT32_DATA_OP())
				SET_NZ(A_OP(&,readFlatAddressedByte()));
			else
#endif
				SET_NZ(A_OP(&,readByte(_zpi())));
			}
			break;
	case 0x33:	/* 65C02: NOP (nonstd loc, implied), 65CE02: BMI 16-bit relative */
			if (IS_CPU_NMOS) { NMOS_JAM_OPCODE(); } else {
#ifdef CPU_65CE02
			OPC_65CE02("BMI16");
#ifdef CPU65_DISCRETE_PF_NZ
			_BRA16(CPU65.pf_n);
#else
			_BRA16(CPU65.pf_nz & CPU65_PF_N);
#endif
#endif
			}
			break;
	case 0x34:	/* BIT Zero_Page,X */
			if (IS_CPU_NMOS) { NMOS_JAM_OPCODE(); } else {
			_BIT(readByte(_zpx()));
			}
			break;
	case 0x35:	/* AND Zero_Page,X */
			SET_NZ(A_OP(&,readByte(_zpx())));
			break;
	case 0x36:	/* ROL Zero_Page,X */
			_ROL(_zpx());
			break;
	case 0x37:	/* RMB Zero_Page */
			if (IS_CPU_NMOS) { NMOS_JAM_OPCODE(); } else {
			int a = _zp();
			writeByte(a, readByte(a) & 247);
			}
			break;
	case 0x38:	/* SEC Implied */
			CPU65.pf_c = 1;
			break;
	case 0x39:	/* AND Absolute,Y */
			SET_NZ(A_OP(&,readByte(_absy())));
			break;
	case 0x3A:	/* DEA Accumulator */
			if (IS_CPU_NMOS) { NMOS_JAM_OPCODE(); } else {
			SET_NZ(--CPU65.a);
			}
			break;
	case 0x3B:	/* 65C02: NOP (nonstd loc, implied), 65CE02: DEZ */
			if (IS_CPU_NMOS) { NMOS_JAM_OPCODE(); } else {
#ifdef CPU_65CE02
			OPC_65CE02("DEZ");
			SET_NZ(--CPU65.z);
#endif
			}
			break;
	case 0x3C:	/* BIT Absolute,X */
			if (IS_CPU_NMOS) { NMOS_JAM_OPCODE(); } else {
			_BIT(readByte(_absx()));
			}
			break;
	case 0x3D:	/* AND Absolute,X */
			SET_NZ(A_OP(&,readByte(_absx())));
			break;
	case 0x3E:	/* ROL Absolute,X */
			_ROL(_absx());
			break;
	case 0x3F:	/* BBR Relative */
			if (IS_CPU_NMOS) { NMOS_JAM_OPCODE(); } else {
			_BRA(!(readByte(_zp()) & 8));
			}
			break;
	case 0x40:	/* RTI Implied */
			cpu65_set_pf(pop());
			CPU65.pc = popWord();
			break;
	case 0x41:	/* EOR (Zero_Page,X) */
			SET_NZ(A_OP(^,readByte(_zpxi())));
			break;
	case 0x42:	/* 65C02: NOP imm (non-std NOP with addr mode), 65CE02: NEG */
			if (IS_CPU_NMOS) { NMOS_JAM_OPCODE(); } else {
#ifdef CPU_65CE02
#ifdef MEGA65
			if (XEMU_UNLIKELY(CPU65.previous_op == 0x42)) {
				CPU65.neg_neg_prefix = 1;
				// 65GS02 extension for "32 bit opcodes" (not to be confused with 32 bit addressing ...)
				// we continue with NEG execution though, since it restores the original A then also the NZ
				// flags, so no need to remember what was the NZ flags and A values before the first NEG :)
				OPC_65CE02("NEG-NEG");
				SET_NZ(CPU65.a = -CPU65.a);
				goto do_not_reset_neg_neg_prefix;
			}
#endif
			OPC_65CE02("NEG");
			SET_NZ(CPU65.a = -CPU65.a);	// 65CE02: NEG	FIXME: flags etc are correct?
#else
			CPU65.pc++;	/* 0x42 NOP imm (non-std NOP with addr mode) */
#endif
			}
			break;
	case 0x43:	/* 65C02: NOP (nonstd loc, implied), 65CE02: ASR A */
			if (IS_CPU_NMOS) { NMOS_JAM_OPCODE(); } else {
#ifdef CPU_65CE02
			OPC_65CE02("ASR A");
			_ASR(-1);
			//CPU65.pf_c = CPU65.a & 1;
			//CPU65.a = (CPU65.a >> 1) | (CPU65.a & 0x80);
			//SET_NZ(CPU65.a);
#endif
			}
			break;
	case 0x44:	/* 65C02: NOP zp (non-std NOP with addr mode), 65CE02: ASR $nn */
			if (IS_CPU_NMOS) { NMOS_JAM_OPCODE(); } else {
#ifdef CPU_65CE02
			OPC_65CE02("ASR nn");
			_ASR(_zp());				// 65CE02: ASR $nn
#else
			CPU65.pc++;	// 0x44 NOP zp (non-std NOP with addr mode)
#endif
			}
			break;
	case 0x45:	/* EOR Zero_Page */
			SET_NZ(A_OP(^,readByte(_zp())));
			break;
	case 0x46:	/* LSR Zero_Page */
			_LSR(_zp());
			break;
	case 0x47:	/* RMB Zero_Page */
			if (IS_CPU_NMOS) { NMOS_JAM_OPCODE(); } else {
			int a = _zp();
			writeByte(a, readByte(a) & 239);
			}
			break;
	case 0x48:	/* PHA Implied */
			push(CPU65.a);
			break;
	case 0x49:	/* EOR Immediate */
			SET_NZ(A_OP(^,readByte(_imm())));
			break;
	case 0x4A:	/* LSR Accumulator */
			_LSR(-1);
			break;
	case 0x4B:	/* 65C02: NOP (nonstd loc, implied), 65CE02: TAZ */
			if (IS_CPU_NMOS) { NMOS_JAM_OPCODE(); } else {
#ifdef CPU_65CE02
			OPC_65CE02("TAZ");
			SET_NZ(CPU65.z = CPU65.a);	// 65CE02: TAZ
#endif
			}
			break;
	case 0x4C:	/* JMP Absolute */
			CPU65.pc = _abs();
			break;
	case 0x4D:	/* EOR Absolute */
			SET_NZ(A_OP(^,readByte(_abs())));
			break;
	case 0x4E:	/* LSR Absolute */
			_LSR(_abs());
			break;
	case 0x4F:	/* BBR Relative */
			if (IS_CPU_NMOS) { NMOS_JAM_OPCODE(); } else {
			_BRA(!(readByte(_zp()) & 16));
			}
			break;
	case 0x50:	/* BVC Relative */
			_BRA(!CPU65.pf_v);
			break;
	case 0x51:	/* EOR (Zero_Page),Y */
			SET_NZ(A_OP(^,readByte(_zpiy())));
			break;
	case 0x52:	/* EOR (Zero_Page) or (ZP),Z on 65CE02 */
			if (IS_CPU_NMOS) { NMOS_JAM_OPCODE(); } else {
#ifdef MEGA65
			if (IS_FLAT32_DATA_OP())
				SET_NZ(A_OP(^,readFlatAddressedByte()));
			else
#endif
				SET_NZ(A_OP(^,readByte(_zpi())));
			}
			break;
	case 0x53:	/* 65C02: NOP (nonstd loc, implied), 65CE02: BVC 16-bit-relative */
			if (IS_CPU_NMOS) { NMOS_JAM_OPCODE(); } else {
#ifdef CPU_65CE02
			OPC_65CE02("BVC16");
			_BRA16(!CPU65.pf_v);
#endif
			}
			break;
	case 0x54:	/* 65C02: NOP zpx (non-std NOP with addr mode), 65CE02: ASR $nn,X */
			if (IS_CPU_NMOS) { NMOS_JAM_OPCODE(); } else {
#ifdef CPU_65CE02
			OPC_65CE02("ASR nn,X");
			_ASR(_zpx());
#else
			CPU65.pc++;
#endif
			}
			break;
	case 0x55:	/* EOR Zero_Page,X */
			SET_NZ(A_OP(^,readByte(_zpx())));
			break;
	case 0x56:	/* LSR Zero_Page,X */
			_LSR(_zpx());
			break;
	case 0x57:	/* RMB Zero_Page */
			if (IS_CPU_NMOS) { NMOS_JAM_OPCODE(); } else {
			int a = _zp();
			writeByte(a, readByte(a) & 223);
			}
			break;
	case 0x58:	/* CLI Implied */
			CPU65.pf_i = 0;
			break;
	case 0x59:	/* EOR Absolute,Y */
			SET_NZ(A_OP(^,readByte(_absy())));
			break;
	case 0x5A:	/* PHY Implied */
			if (IS_CPU_NMOS) { NMOS_JAM_OPCODE(); } else {
			push(CPU65.y);
			}
			break;
	case 0x5B:	/* 65C02: NOP (nonstd loc, implied), 65CE02: TAB */
			if (IS_CPU_NMOS) { NMOS_JAM_OPCODE(); } else {
#ifdef CPU_65CE02
			OPC_65CE02("TAB");
			CPU65.bphi = CPU65.a << 8;
#ifdef DEBUG_CPU
			if (CPU65.bphi)
				DEBUG("CPU: WARN base page is non-zero now with value of $%04X" NL, CPU65.bphi);
#endif
#endif
			}
			break;
	case 0x5C:	/* 65C02: NOP (nonstd loc, implied FIXME or absolute?!), 65CE02: AUG/MAP */
			if (IS_CPU_NMOS) { NMOS_JAM_OPCODE(); } else {
#ifdef CPU_65CE02
			OPC_65CE02("MAP");
			cpu65_do_aug_callback();	/* $5C on 65CE02: this is the "AUG" opcode. It must be handled by the emulator, on 4510 (C65) it's redefined as MAP for MMU functionality */
#else
			CPU65.pc += 2;
#endif
			}
			break;
	case 0x5D:	/* EOR Absolute,X */
			SET_NZ(A_OP(^,readByte(_absx())));
			break;
	case 0x5E:	/* LSR Absolute,X */
			_LSR(_absx());
			break;
	case 0x5F:	/* BBR Relative */
			if (IS_CPU_NMOS) { NMOS_JAM_OPCODE(); } else {
			_BRA(!(readByte(_zp()) & 32));
			}
			break;
	case 0x60:	/* RTS Implied */
			CPU65.pc = popWord() + 1;
			break;
	case 0x61:	/* ADC (Zero_Page,X) */
			_ADC(readByte(_zpxi()));
			break;
	case 0x62:	/* 65C02: NOP imm (non-std NOP with addr mode), 65CE02: RTS #$nn */
			/* 65CE02 FIXME TODO : what this opcode does _exactly_? Guess: correcting stack pointer with a given value? Also some docs says it's RTN ... */
			if (IS_CPU_NMOS) { NMOS_JAM_OPCODE(); } else {
#ifdef CPU_65CE02
			OPC_65CE02("RTS #nn");
			{	// 65CE02 RTS #$nn TODO: what this opcode does _exactly_? Guess: correcting stack pointer with a given value? Also some docs says it's RTN ...
			int temp = readByte(CPU65.pc);
			CPU65.pc = popWord() + 1;
			if (CPU65.s + temp > 0xFF && (!CPU65.pf_e))
				CPU65.sphi += 0x100;
			CPU65.s += temp; // SP was already incremented by two by popWord, we need only the extra stuff here
			}
#else
			CPU65.pc++;	// NOP imm (non-std NOP with addr mode)
#endif
			}
			break;
	case 0x63:	/* 65C02: NOP (nonstd loc, implied), 65CE02: BSR16 */
			/* FIXME TODO: BSR $nnnn Interesting 65C02-only? does this opcode exist before 65CE02 as well?! */
			if (IS_CPU_NMOS) { NMOS_JAM_OPCODE(); } else {
#ifdef CPU_65CE02
			OPC_65CE02("BSR16");
			// 65C02 ?! BSR $nnnn Interesting 65C02-only? FIXME TODO: does this opcode exist before 65CE02 as well?!
			pushWord(CPU65.pc + 1);
			_BRA16(1);
#endif
			}
			break;
	case 0x64:	/* STZ Zero_Page */
			if (IS_CPU_NMOS) { NMOS_JAM_OPCODE(); } else {
			writeByte(_zp(), ZERO_REG);
			}
			break;
	case 0x65:	/* ADC Zero_Page */
			_ADC(readByte(_zp()));
			break;
	case 0x66:	/* ROR Zero_Page */
			_ROR(_zp());
			break;
	case 0x67:	/* RMB Zero_Page */
			if (IS_CPU_NMOS) { NMOS_JAM_OPCODE(); } else {
			int a = _zp();
			writeByte(a, readByte(a) & 191);
			}
			break;
	case 0x68:	/* PLA Implied */
			SET_NZ(CPU65.a = pop());
			break;
	case 0x69:	/* ADC Immediate */
			_ADC(readByte(_imm()));
			break;
	case 0x6A:	/* ROR Accumulator */
			_ROR(-1);
			break;
	case 0x6B:	/* 65C02: NOP (nonstd loc, implied), 65CE02: TZA */
			if (IS_CPU_NMOS) { NMOS_JAM_OPCODE(); } else {
#ifdef CPU_65CE02
			OPC_65CE02("TZA");
			SET_NZ(CPU65.a = CPU65.z);	// 65CE02 TZA
#endif
			}
			break;
	case 0x6C:	/* JMP (Absolute) */
			if (HAS_NMOS_BUG_JMP_INDIRECT) {
				int t = _abs();
				CPU65.pc = readByte(t) | (readByte((t & 0xFF00) | ((t + 1) & 0xFF)) << 8);
			} else
				CPU65.pc = _absi();
			break;
	case 0x6D:	/* ADC Absolute */
			_ADC(readByte(_abs()));
			break;
	case 0x6E:	/* ROR Absolute */
			_ROR(_abs());
			break;
	case 0x6F:	/* BBR Relative */
			if (IS_CPU_NMOS) { NMOS_JAM_OPCODE(); } else {
			_BRA(!(readByte(_zp()) & 64));
			}
			break;
	case 0x70:	/* BVS Relative */
			_BRA(CPU65.pf_v);
			break;
	case 0x71:	/* ADC (Zero_Page),Y */
			_ADC(readByte(_zpiy()));
			break;
	case 0x72:	/* 0x72 ADC (Zero_Page) or (ZP),Z on 65CE02 */
			if (IS_CPU_NMOS) { NMOS_JAM_OPCODE(); } else {
#ifdef MEGA65
			if (IS_FLAT32_DATA_OP())
				_ADC(readFlatAddressedByte());
			else
#endif
				_ADC(readByte(_zpi()));
			}
			break;
	case 0x73:	/* 65C02: NOP (nonstd loc, implied), 65CE02: BVS 16 bit relative */
			if (IS_CPU_NMOS) { NMOS_JAM_OPCODE(); } else {
#ifdef CPU_65CE02
			OPC_65CE02("BVS16");
			_BRA16(CPU65.pf_v);
#endif
			}
			break;
	case 0x74:	/* STZ Zero_Page,X */
			if (IS_CPU_NMOS) { NMOS_JAM_OPCODE(); } else {
			writeByte(_zpx(), ZERO_REG);
			}
			break;
	case 0x75:	/* ADC Zero_Page,X */
			_ADC(readByte(_zpx()));
			break;
	case 0x76:	/* ROR Zero_Page,X */
			_ROR(_zpx());
			break;
	case 0x77:	/* RMB Zero_Page */
			if (IS_CPU_NMOS) { NMOS_JAM_OPCODE(); } else {
			int a = _zp();
			writeByte(a, readByte(a) & 127);
			}
			break;
	case 0x78:	/* SEI Implied */
			CPU65.pf_i = 1;
			break;
	case 0x79:	/* ADC Absolute,Y */
			_ADC(readByte(_absy()));
			break;
	case 0x7A:	/* PLY Implied */
			if (IS_CPU_NMOS) { NMOS_JAM_OPCODE(); } else {
			SET_NZ(CPU65.y = pop());
			}
			break;
	case 0x7B:	/* 65C02: NOP (nonstd loc, implied), 65CE02: TBA */
			if (IS_CPU_NMOS) { NMOS_JAM_OPCODE(); } else {
#ifdef CPU_65CE02
			OPC_65CE02("TBA");
			SET_NZ(CPU65.a = (CPU65.bphi >> 8));	// 65C02 TBA
#endif
			}
			break;
	case 0x7C:	/* JMP (Absolute,X) */
			if (IS_CPU_NMOS) { NMOS_JAM_OPCODE(); } else {
			CPU65.pc = _absxi();
			}
			break;
	case 0x7D:	/* ADC Absolute,X */
			_ADC(readByte(_absx()));
			break;
	case 0x7E:	/* ROR Absolute,X */
			_ROR(_absx());
			break;
	case 0x7F:	/* BBR Relative */
			if (IS_CPU_NMOS) { NMOS_JAM_OPCODE(); } else {
			_BRA(!(readByte(_zp()) & 128));
			}
			break;
	case 0x80:	/* BRA Relative */
			if (IS_CPU_NMOS) { NMOS_JAM_OPCODE(); } else {
			_BRA(1);
			}
			break;
	case 0x81:	/* STA (Zero_Page,X) */
			writeByte(_zpxi(), CPU65.a);
			break;
	case 0x82:	/* 65C02: NOP imm (non-std NOP with addr mode), 65CE02: STA ($nn,SP),Y */
			if (IS_CPU_NMOS) { NMOS_JAM_OPCODE(); } else {
#ifdef CPU_65CE02
			OPC_65CE02("STA (nn,S),Y");
			writeByte(_GET_SP_INDIRECT_ADDR(), CPU65.a);	// 65CE02 STA ($nn,SP),Y
#else
			CPU65.pc++;	// NOP imm (non-std NOP with addr mode)
#endif
			}
			break;
	case 0x83:	/* 65C02: NOP (nonstd loc, implied), 65CE02: BRA $nnnn 16-bit-pc-rel? */
			if (IS_CPU_NMOS) { NMOS_JAM_OPCODE(); } else {
#ifdef CPU_65CE02
			OPC_65CE02("BRA16");
			_BRA16(1);	// 65CE02 BRA $nnnn 16-bit-pc-rel?
#endif
			}
			break;
	case 0x84:	/* STY Zero_Page */
			writeByte(_zp(), CPU65.y);
			break;
	case 0x85:	/* STA Zero_Page */
			writeByte(_zp(), CPU65.a);
			break;
	case 0x86:	/* STX Zero_Page */
			writeByte(_zp(), CPU65.x);
			break;
	case 0x87:	/* SMB Zero_Page */
			if (IS_CPU_NMOS) { NMOS_JAM_OPCODE(); } else {
			int a = _zp();
			writeByte(a, readByte(a) | 1);
			}
			break;
	case 0x88:	/* DEY Implied */
			SET_NZ(--CPU65.y);
			break;
	case 0x89:	/* BIT+ Immediate */
			if (IS_CPU_NMOS) { NMOS_JAM_OPCODE(); } else {
#ifdef CPU65_DISCRETE_PF_NZ
			CPU65.pf_z = (!(CPU65.a & readByte(_imm())));
#else
			if (CPU65.a & readByte(_imm())) CPU65.pf_nz &= (~CPU65_PF_Z); else CPU65.pf_nz |= CPU65_PF_Z;
#endif
			}
			break;
	case 0x8A:	/* TXA Implied */
			SET_NZ(CPU65.a = CPU65.x);
			break;
	case 0x8B:	/* 65C02: NOP (nonstd loc, implied), 65CE02: STY $nnnn,X */
			if (IS_CPU_NMOS) { NMOS_JAM_OPCODE(); } else {
#ifdef CPU_65CE02
			OPC_65CE02("STY nnnn,X");
			writeByte(_absx(), CPU65.y); // 65CE02 STY $nnnn,X
#endif
			}
			break;
	case 0x8C:	/* STY Absolute */
			writeByte(_abs(), CPU65.y);
			break;
	case 0x8D:	/* STA Absolute */
			writeByte(_abs(), CPU65.a);
			break;
	case 0x8E:	/* STX Absolute */
			writeByte(_abs(), CPU65.x);
			break;
	case 0x8F:	/* BBS Relative */
			if (IS_CPU_NMOS) { NMOS_JAM_OPCODE(); } else {
			_BRA( readByte(_zp()) & 1 );
			}
			break;
	case 0x90:	/* BCC Relative */
			_BRA(!CPU65.pf_c);
			break;
	case 0x91:	/* STA (Zero_Page),Y */
			writeByte(_zpiy(), CPU65.a);
			break;
	case 0x92:	/* STA (Zero_Page) or (ZP),Z on 65CE02 */
			if (IS_CPU_NMOS) { NMOS_JAM_OPCODE(); } else {
#ifdef MEGA65
			if (IS_FLAT32_DATA_OP())
				writeFlatAddressedByte(CPU65.a);
			else
#endif
				writeByte(_zpi(), CPU65.a);
			}
			break;
	case 0x93:	/* 65C02: NOP (nonstd loc, implied), 65CE02: BCC $nnnn */
			if (IS_CPU_NMOS) { NMOS_JAM_OPCODE(); } else {
#ifdef CPU_65CE02
			OPC_65CE02("BCC16");
			_BRA16(!CPU65.pf_c);	// 65CE02  BCC $nnnn
#endif
			}
			break;
	case 0x94:	/* STY Zero_Page,X */
			writeByte(_zpx(), CPU65.y);
			break;
	case 0x95:	/* STA Zero_Page,X */
			writeByte(_zpx(), CPU65.a);
			break;
	case 0x96:	/* STX Zero_Page,Y */
			writeByte(_zpy(), CPU65.x);
			break;
	case 0x97:	/* SMB Zero_Page */
			if (IS_CPU_NMOS) { NMOS_JAM_OPCODE(); } else {
			int a = _zp();
			writeByte(a, readByte(a) | 2);
			}
			break;
	case 0x98:	/* TYA Implied */
			SET_NZ(CPU65.a = CPU65.y);
			break;
	case 0x99:	/* STA Absolute,Y */
			writeByte(_absy(), CPU65.a);
			break;
	case 0x9A:	/* TXS Implied */
			CPU65.s = CPU65.x;
			break;
	case 0x9B:	/* 65C02: NOP (nonstd loc, implied), 65CE02: STX $nnnn,Y */
			if (IS_CPU_NMOS) { NMOS_JAM_OPCODE(); } else {
#ifdef CPU_65CE02
			OPC_65CE02("STX nnnn,Y");
			writeByte(_absy(), CPU65.x);	// 65CE02 STX $nnnn,Y
#endif
			}
			break;
	case 0x9C:	/* STZ Absolute */
			if (IS_CPU_NMOS) { NMOS_JAM_OPCODE(); } else {
			writeByte(_abs(), ZERO_REG);
			}
			break;
	case 0x9D:	/* STA Absolute,X */
			writeByte(_absx(), CPU65.a);
			break;
	case 0x9E:	/* STZ Absolute,X */
			if (IS_CPU_NMOS) { NMOS_JAM_OPCODE(); } else {
			writeByte(_absx(), ZERO_REG);
			}
			break;
	case 0x9F:	/* BBS Relative */
			if (IS_CPU_NMOS) { NMOS_JAM_OPCODE(); } else {
			_BRA( readByte(_zp()) & 2 );
			}
			break;
	case 0xA0:	/* LDY Immediate */
			SET_NZ(CPU65.y = readByte(_imm()));
			break;
	case 0xA1:	/* LDA (Zero_Page,X) */
			SET_NZ(CPU65.a = readByte(_zpxi()));
			break;
	case 0xA2:	/* LDX Immediate */
			SET_NZ(CPU65.x = readByte(_imm()));
			break;
	case 0xA3:	/* 65C02: NOP (nonstd loc, implied), 65CE02: LDZ #$nn */
			if (IS_CPU_NMOS) { NMOS_JAM_OPCODE(); } else {
#ifdef CPU_65CE02
			OPC_65CE02("LDZ #nn");
			SET_NZ(CPU65.z = readByte(_imm()));
#endif
			}
			break;
	case 0xA4:	/* LDY Zero_Page */
			SET_NZ(CPU65.y = readByte(_zp()));
			break;
	case 0xA5:	/* LDA Zero_Page */
			SET_NZ(CPU65.a = readByte(_zp()));
			break;
	case 0xA6:	/* LDX Zero_Page */
			SET_NZ(CPU65.x = readByte(_zp()));
			break;
	case 0xA7:	/* SMB Zero_Page */
			if (IS_CPU_NMOS) { NMOS_JAM_OPCODE(); } else {
			int a = _zp();
			writeByte(a, readByte(a) | 4);
			}
			break;
	case 0xA8:	/* TAY Implied */
			SET_NZ(CPU65.y = CPU65.a);
			break;
	case 0xA9:	/* LDA Immediate */
			SET_NZ(CPU65.a = readByte(_imm()));
			break;
	case 0xAA:	/* TAX Implied */
			SET_NZ(CPU65.x = CPU65.a);
			break;
	case 0xAB:	/* 65C02: NOP (nonstd loc, implied), 65CE02: LDZ $nnnn */
			if (IS_CPU_NMOS) { NMOS_JAM_OPCODE(); } else {
#ifdef CPU_65CE02
			OPC_65CE02("LDZ nnnn");
			SET_NZ(CPU65.z = readByte(_abs()));
#endif
			}
			break;
	case 0xAC:	/* LDY Absolute */
			SET_NZ(CPU65.y = readByte(_abs()));
			break;
	case 0xAD:	/* LDA Absolute */
			SET_NZ(CPU65.a = readByte(_abs()));
			break;
	case 0xAE:	/* LDX Absolute */
			SET_NZ(CPU65.x = readByte(_abs()));
			break;
	case 0xAF:	/* BSS Relative */
			if (IS_CPU_NMOS) { NMOS_JAM_OPCODE(); } else {
			_BRA( readByte(_zp()) & 4 );
			}
			break;
	case 0xB0:	/* BCS Relative */
			_BRA(CPU65.pf_c);
			break;
	case 0xB1:	/* LDA (Zero_Page),Y */
			SET_NZ(CPU65.a = readByte(_zpiy()));
			break;
	case 0xB2:	/* LDA (Zero_Page) or (ZP),Z on 65CE02 */
			if (IS_CPU_NMOS) { NMOS_JAM_OPCODE(); } else {
#ifdef MEGA65
			if (IS_FLAT32_DATA_OP())
				SET_NZ(CPU65.a = readFlatAddressedByte());
			else
#endif
				SET_NZ(CPU65.a = readByte(_zpi()));
			}
			break;
	case 0xB3:	/* 65C02: NOP (nonstd loc, implied), 65CE02: BCS $nnnn */
			if (IS_CPU_NMOS) { NMOS_JAM_OPCODE(); } else {
#ifdef CPU_65CE02
			OPC_65CE02("BCS16");
			_BRA16(CPU65.pf_c);
#endif
			}
			break;
	case 0xB4:	/* LDY Zero_Page,X */
			SET_NZ(CPU65.y = readByte(_zpx()));
			break;
	case 0xB5:	/* LDA Zero_Page,X */
			SET_NZ(CPU65.a = readByte(_zpx()));
			break;
	case 0xB6:	/* LDX Zero_Page,Y */
			SET_NZ(CPU65.x = readByte(_zpy()));
			break;
	case 0xB7:	/* SMB Zero_Page */
			if (IS_CPU_NMOS) { NMOS_JAM_OPCODE(); } else {
			int a = _zp();
			writeByte(a, readByte(a) | 8);
			}
			break;
	case 0xB8:	/* CLV Implied */
			CPU65.pf_v = 0;
			break;
	case 0xB9:	/* LDA Absolute,Y */
			SET_NZ(CPU65.a = readByte(_absy()));
			break;
	case 0xBA:	/* TSX Implied */
			SET_NZ(CPU65.x = CPU65.s);
			break;
	case 0xBB:	/* 65C02: NOP (nonstd loc, implied), 65CE02: LDZ $nnnn,X */
			if (IS_CPU_NMOS) { NMOS_JAM_OPCODE(); } else {
#ifdef CPU_65CE02
			OPC_65CE02("LDZ nnnn,X");
			SET_NZ(CPU65.z = readByte(_absx()));
#endif
			}
			break;
	case 0xBC:	/* LDY Absolute,X */
			SET_NZ(CPU65.y = readByte(_absx()));
			break;
	case 0xBD:	/* LDA Absolute,X */
			SET_NZ(CPU65.a = readByte(_absx()));
			break;
	case 0xBE:	/* LDX Absolute,Y */
			SET_NZ(CPU65.x = readByte(_absy()));
			break;
	case 0xBF:	/* BBS Relative */
			if (IS_CPU_NMOS) { NMOS_JAM_OPCODE(); } else {
			_BRA( readByte(_zp()) & 8 );
			}
			break;
	case 0xC0:	/* CPY Immediate */
			_CMP(CPU65.y, readByte(_imm()));
			break;
	case 0xC1:	/* CMP (Zero_Page,X) */
			_CMP(CPU65.a, readByte(_zpxi()));
			break;
	case 0xC2:	/* 65C02: imm (non-std NOP with addr mode), 65CE02: CPZ #$nn */
			if (IS_CPU_NMOS) { NMOS_JAM_OPCODE(); } else {
#ifdef CPU_65CE02
			OPC_65CE02("CPZ #nn");
			_CMP(CPU65.z, readByte(_imm()));
#else
			CPU65.pc++; // imm (non-std NOP with addr mode)
#endif
			}
			break;
	case 0xC3:	/* 65C02: NOP (nonstd loc, implied), 65CE02: DEW $nn */
			/* DEW $nn 65CE02  C3  Decrement Word (maybe an error in 64NET.OPC ...) ANOTHER FIXME: this is zero (errr, base ...) page!!! */
			if (IS_CPU_NMOS) { NMOS_JAM_OPCODE(); } else {
#ifdef CPU_65CE02
			OPC_65CE02("DEW nn");
			int alo = _zp();
			int ahi = (alo & 0xFF00) | ((alo + 1) & 0xFF);
			Uint16 data = (readByte(alo) | (readByte(ahi) << 8)) - 1;
			SET_NZ16(data);
			writeByte(alo, data & 0xFF);
			writeByte(ahi, data >> 8);
#endif
			}
			break;
	case 0xC4:	/* CPY Zero_Page */
			_CMP(CPU65.y, readByte(_zp()));
			break;
	case 0xC5:	/* CMP Zero_Page */
			_CMP(CPU65.a, readByte(_zp()));
			break;
	case 0xC6:	/* DEC Zero_Page */
			{
			int addr = _zp();
			Uint8 data = readByte(addr) - 1;
			SET_NZ(data);
			writeByte(addr, data);
			}
			break;
	case 0xC7:	/* SMB Zero_Page */
			if (IS_CPU_NMOS) { NMOS_JAM_OPCODE(); } else {
			int a = _zp();
			writeByte(a, readByte(a) | 16);
			}
			break;
	case 0xC8:	/* INY Implied */
			SET_NZ(++CPU65.y);
			break;
	case 0xC9:	/* CMP Immediate */
			_CMP(CPU65.a, readByte(_imm()));
			break;
	case 0xCA:	/* DEX Implied */
			SET_NZ(--CPU65.x);
			break;
	case 0xCB:	/* 65C02: NOP (nonstd loc, implied), 65CE02: ASW $nnnn ("Arithmetic Shift Left Word") */
			if (IS_CPU_NMOS) { NMOS_JAM_OPCODE(); } else {
#ifdef CPU_65CE02
			OPC_65CE02("ASW nnnn");
			int addr = _abs();
			Uint16 data = readByte(addr) | (readByte(addr + 1) << 8);
			CPU65.pf_c = data & 0x8000;
			data <<= 1;
			SET_NZ16(data);
			writeByte(addr, data & 0xFF);
			writeByte(addr + 1, data >> 8);
#endif
			}
			break;
	case 0xCC:	/* CPY Absolute */
			_CMP(CPU65.y, readByte(_abs()));
			break;
	case 0xCD:	/* CMP Absolute */
			_CMP(CPU65.a, readByte(_abs()));
			break;
	case 0xCE:	/* DEC Absolute */
			{
			int addr = _abs();
			Uint8 data = readByte(addr) - 1;
			SET_NZ(data);
			writeByte(addr, data);
			}
			break;
	case 0xCF:	/* BBS Relative */
			if (IS_CPU_NMOS) { NMOS_JAM_OPCODE(); } else {
			_BRA( readByte(_zp()) & 16 );
			}
			break;
	case 0xD0:	/* BNE Relative */
#ifdef CPU65_DISCRETE_PF_NZ
			_BRA( !CPU65.pf_z);
#else
			_BRA(!(CPU65.pf_nz & CPU65_PF_Z));
#endif
			break;
	case 0xD1:	/* CMP (Zero_Page),Y */
			_CMP(CPU65.a, readByte(_zpiy()));
			break;
	case 0xD2:	/* CMP (Zero_Page) or (ZP),Z on 65CE02 */
			if (IS_CPU_NMOS) { NMOS_JAM_OPCODE(); } else {
#ifdef MEGA65
			if (IS_FLAT32_DATA_OP())	// NOTE: this was not mentioned in Paul's blog-post, but this op should have this property as well, IMHO!
				_CMP(CPU65.a, readFlatAddressedByte());
			else
#endif
				_CMP(CPU65.a, readByte(_zpi()));
			}
			break;
	case 0xD3:	/* 65C02: NOP (nonstd loc, implied), 65CE02: BNE16 */
			if (IS_CPU_NMOS) { NMOS_JAM_OPCODE(); } else {
#ifdef CPU_65CE02
			OPC_65CE02("BNE16");
#ifdef CPU65_DISCRETE_PF_NZ
			_BRA16( !CPU65.pf_z);
#else
			_BRA16(!(CPU65.pf_nz & CPU65_PF_Z));
#endif
#endif
			}
			break;
	case 0xD4:	/* 65C02: NOP zpx (non-std NOP with addr mode), 65CE02: CPZ $nn */
			if (IS_CPU_NMOS) { NMOS_JAM_OPCODE(); } else {
#ifdef CPU_65CE02
			OPC_65CE02("CPZ nn");
			_CMP(CPU65.z, readByte(_zp()));
#else
			CPU65.pc++;	// NOP zpx (non-std NOP with addr mode)
#endif
			}
			break;
	case 0xD5:	/* CMP Zero_Page,X */
			 _CMP(CPU65.a, readByte(_zpx()));
			break;
	case 0xD6:	/* DEC Zero_Page,X */
			{
			int addr = _zpx();
			Uint8 data = readByte(addr) - 1;
			SET_NZ(data); writeByte(addr, data);
			}
			break;
	case 0xD7:	/* SMB Zero_Page */
			if (IS_CPU_NMOS) { NMOS_JAM_OPCODE(); } else {
			int a = _zp();
			writeByte(a, readByte(a) | 32);
			}
			break;
	case 0xD8:	/* CLD Implied */
			CPU65.pf_d = 0;
			break;
	case 0xD9:	/* CMP Absolute,Y */
			_CMP(CPU65.a, readByte(_absy()));
			break;
	case 0xDA:	/* PHX Implied */
			if (IS_CPU_NMOS) { NMOS_JAM_OPCODE(); } else {
			push(CPU65.x);
			}
			break;
	case 0xDB:	/* 65C02: NOP (nonstd loc, implied), 65CE02: PHZ */
			if (IS_CPU_NMOS) { NMOS_JAM_OPCODE(); } else {
#ifdef CPU_65CE02
			OPC_65CE02("PHZ");
			push(CPU65.z);
#endif
			}
			break;
	case 0xDC:	/* 65C02: NOP (nonstd loc, implied) FIXME: bugfix NOP absolute!, 65CE02: CPZ $nnnn */
			if (IS_CPU_NMOS) { NMOS_JAM_OPCODE(); } else {
#ifdef CPU_65CE02
			OPC_65CE02("CPZ nnnn");
			_CMP(CPU65.z, readByte(_abs()));
#else
			CPU65.pc += 2;
#endif
			}
			break;
	case 0xDD:	/* CMP Absolute,X */
			_CMP(CPU65.a, readByte(_absx()));
			break;
	case 0xDE:	/* DEC Absolute,X */
			{
			int addr = _absx();
			Uint8 data = readByte(addr) - 1;
			SET_NZ(data);
			writeByte(addr, data);
			}
			break;
	case 0xDF:	/* BBS Relative */
			if (IS_CPU_NMOS) { NMOS_JAM_OPCODE(); } else {
			_BRA( readByte(_zp()) & 32 );
			}
			break;
	case 0xE0:	/* CPX Immediate */
			_CMP(CPU65.x, readByte(_imm()));
			break;
	case 0xE1:	/* SBC (Zero_Page,X) */
			_SBC(readByte(_zpxi()));
			break;
	case 0xE2:	/* 65C02: NOP imm (non-std NOP with addr mode), 65CE02: LDA (nn,S),Y */
			if (IS_CPU_NMOS) { NMOS_JAM_OPCODE(); } else {
#ifdef CPU_65CE02
			OPC_65CE02("LDA (nn,S),Y");
			// 65CE02 LDA ($nn,SP),Y
			// REALLY IMPORTANT: please read the comment at _GET_SP_INDIRECT_ADDR()!
			SET_NZ(CPU65.a = readByte(_GET_SP_INDIRECT_ADDR()));
			//DEBUG("CPU: LDA (nn,S),Y returned: A = $%02X, P before last IRQ was: $%02X" NL, CPU65.a, last_p);
#else
			CPU65.pc++; // 0xe2 NOP imm (non-std NOP with addr mode)
#endif
			}
			break;
	case 0xE3:	/* 65C02: NOP (nonstd loc, implied), 65CE02: Increment Word (maybe an error in 64NET.OPC ...) ANOTHER FIXME: this is zero (errr, base ...) page!!! */
			if (IS_CPU_NMOS) { NMOS_JAM_OPCODE(); } else {
#ifdef CPU_65CE02
			OPC_65CE02("INW nn");
			int alo = _zp();
			int ahi = (alo & 0xFF00) | ((alo + 1) & 0xFF);
			Uint16 data = (readByte(alo) | (readByte(ahi) << 8)) + 1;
			SET_NZ16(data);
			//cpu_pfz = (data == 0);
			writeByte(alo, data & 0xFF);
			writeByte(ahi, data >> 8);
#endif
			}
			break;
	case 0xE4:	/* CPX Zero_Page */
			_CMP(CPU65.x, readByte(_zp()));
			break;
	case 0xE5:	/* SBC Zero_Page */
			_SBC(readByte(_zp()));
			break;
	case 0xE6:	/* INC Zero_Page */
			{
			int addr = _zp();
			Uint8 data = readByte(addr) + 1;
			SET_NZ(data);
			writeByte(addr, data);
			}
			break;
	case 0xE7:	/* SMB Zero_Page */
			if (IS_CPU_NMOS) { NMOS_JAM_OPCODE(); } else {
			int a = _zp();
			writeByte(a, readByte(a) | 64);
			}
			break;
	case 0xE8:	/* INX Implied */
			SET_NZ(++CPU65.x);
			break;
	case 0xE9:	/* SBC Immediate */
			_SBC(readByte(_imm()));
			break;
	case 0xEA:	/* NOP, 65CE02: it's not special, but in C65 (4510) it is (EOM). It's up the emulator though (in the the second case) ... */
#ifdef CPU_65CE02
#ifdef MEGA65
			if (XEMU_UNLIKELY(CPU65.neg_neg_prefix)) {
				OPC_65CE02("NEG-NEG-NOP");
				goto do_not_reset_neg_neg_prefix;	// FIXME: NEG NEG NOP sequence, should we treat ALSO as EOM that NOP and execute the nop callback above in that case?
			}
#endif
			OPC_65CE02("EOM");
			cpu65_do_nop_callback();
#endif
			break;
	case 0xEB:	/* 65C02: NOP (nonstd loc, implied), 65CE02: ROW $nnnn Rotate word LEFT?! [other documents says RIGHT!!! FIXME] */
			if (IS_CPU_NMOS) { NMOS_JAM_OPCODE(); } else {
#ifdef CPU_65CE02
			OPC_65CE02("ROW nnnn");
			int addr = _abs();
			int data = ((readByte(addr) | (readByte(addr + 1) << 8)) << 1) | (CPU65.pf_c ? 1 : 0);
			CPU65.pf_c = data & 0x10000;
			data &= 0xFFFF;
			SET_NZ16(data);
			writeByte(addr, data & 0xFF);
			writeByte(addr + 1, data >> 8);
#endif
			}
			break;
	case 0xEC:	/* CPX Absolute */
			_CMP(CPU65.x, readByte(_abs()));
			break;
	case 0xED:	/* SBC Absolute */
			_SBC(readByte(_abs()));
			break;
	case 0xEE:	/* INC Absolute */
			{
			int addr = _abs();
			Uint8 data = readByte(addr) + 1;
			SET_NZ(data);
			writeByte(addr, data);
			}
			break;
	case 0xEF:	/* BBS Relative */
			if (IS_CPU_NMOS) { NMOS_JAM_OPCODE(); } else {
			_BRA( readByte(_zp()) & 64 );
			}
			break;
	case 0xF0:	/* BEQ Relative */
#ifdef CPU65_DISCRETE_PF_NZ
			_BRA(CPU65.pf_z);
#else
			_BRA(CPU65.pf_nz & CPU65_PF_Z);
#endif
			break;
	case 0xF1:	/* SBC (Zero_Page),Y */
			_SBC(readByte(_zpiy()));
			break;
	case 0xF2:	/* SBC (Zero_Page) or (ZP),Z on 65CE02 */
			if (IS_CPU_NMOS) { NMOS_JAM_OPCODE(); } else {
#ifdef MEGA65
			if (IS_FLAT32_DATA_OP())
				_SBC(readFlatAddressedByte());
			else
#endif
				_SBC(readByte(_zpi()));
			}
			break;
	case 0xF3:	/* 65C02: NOP (nonstd loc, implied), 65CE02: BEQ16 */
			if (IS_CPU_NMOS) { NMOS_JAM_OPCODE(); } else {
#ifdef CPU_65CE02
			OPC_65CE02("BEQ16");
#ifdef CPU65_DISCRETE_PF_NZ
			_BRA16(CPU65.pf_z);
#else
			_BRA16(CPU65.pf_nz & CPU65_PF_Z);
#endif
#endif
			}
			break;
	case 0xF4:	/* 65C02: NOP zpx (non-std NOP with addr mode), 65CE02: PHW #$nnnn (push word) */
			if (IS_CPU_NMOS) { NMOS_JAM_OPCODE(); } else {
#ifdef CPU_65CE02
			OPC_65CE02("PHW #nnnn");
			PUSH_FOR_PHW(readWord(CPU65.pc));	// 65CE02 PHW #$nnnn
			CPU65.pc += 2;
#else
			CPU65.pc++; // 0xf4 NOP zpx (non-std NOP with addr mode)
#endif
			}
			break;
	case 0xF5:	/* SBC Zero_Page,X */
			_SBC(readByte(_zpx()));
			break;
	case 0xF6:	/* INC Zero_Page,X */
			{
			int addr = _zpx();
			Uint8 data = readByte(addr) + 1;
			SET_NZ(data);
			writeByte(addr, data);
			}
			break;
	case 0xF7:	/* SMB Zero_Page */
			if (IS_CPU_NMOS) { NMOS_JAM_OPCODE(); } else {
			int a = _zp();
			writeByte(a, readByte(a) | 128);
			}
			break;
	case 0xF8:	/* SED Implied */
			CPU65.pf_d = 1;
			break;
	case 0xF9:	/* SBC Absolute,Y */
			_SBC(readByte(_absy()));
			break;
	case 0xFA:	/* PLX Implied */
			if (IS_CPU_NMOS) { NMOS_JAM_OPCODE(); } else {
			SET_NZ(CPU65.x = pop());
			}
			break;
	case 0xFB:	/* 65C02: NOP (nonstd loc, implied), 65CE02: PLZ */
			if (IS_CPU_NMOS) { NMOS_JAM_OPCODE(); } else {
#ifdef CPU_65CE02
			OPC_65CE02("PLZ");
			SET_NZ(CPU65.z = pop());	// PLZ
#endif
			}
			break;
	case 0xFC:	/* 65C02: NOP (nonstd loc, implied) FIXME: bugfix NOP absolute?
			   65CE02: PHW $nnnn [? push word from an absolute address, maybe?] Note: C65 BASIC depends on this opcode to be correct! */
			if (IS_CPU_NMOS) { NMOS_JAM_OPCODE(); } else {
#ifdef CPU_65CE02
			OPC_65CE02("PHW nnnn");
			PUSH_FOR_PHW(readWord(readWord(CPU65.pc)));
			CPU65.pc += 2;
#else
			CPU65.pc += 2;
#endif
			}
			break;
	case 0xFD:	/* SBC Absolute,X */
			_SBC(readByte(_absx()));
			break;
	case 0xFE:	/* INC Absolute,X */
			{
			int addr = _absx();
			Uint8 data = readByte(addr) + 1;
			SET_NZ(data);
			writeByte(addr, data);
			}
			break;
	case 0xFF:	/* BBS Relative */
			if (IS_CPU_NMOS) { NMOS_JAM_OPCODE(); } else {
			_BRA( readByte(_zp()) & 128 );
			}
			break;
	default:
			XEMU_UNREACHABLE();
			break;
	}
#ifdef MEGA65
	CPU65.neg_neg_prefix = 0;
do_not_reset_neg_neg_prefix:
#endif
#ifdef CPU_STEP_MULTI_OPS
	all_cycles += CPU65.op_cycles;
	if (XEMU_UNLIKELY(CPU65.multi_step_stop_trigger)) {
		CPU65.multi_step_stop_trigger = 0;
		return all_cycles;
	}
	} while (all_cycles < run_for_cycles);
	return all_cycles;
#else
	return CPU65.op_cycles;
#endif
}


/* ---- SNAPSHOT RELATED ---- */

/* NOTE: cpu_linear_memory_addressing_is_enabled is not the CPU emulator handled data ...
 * FIXME: many things are missing for now from snapshot ... I should review all cpu65 struct things
 * if they're really stored, I can't remember, but at least things like neg_neg_prefix is not,
 * and maybe tons of others. Certainly it will cause problems on shapshot loading back, which is
 * not so much a frequent usage in Xemu now but there can be in the future!
*/


#ifdef XEMU_SNAPSHOT_SUPPORT

#include "xemu/emutools_snapshot.h"
#include <string.h>

#define SNAPSHOT_CPU65_BLOCK_VERSION	0
#define SNAPSHOT_CPU65_BLOCK_SIZE	256

#ifdef CPU_65CE02
#define SNAPSHOT_CPU65_ID		2
#else
#define SNAPSHOT_CPU65_ID		1
#endif

int cpu65_snapshot_load_state ( const struct xemu_snapshot_definition_st *def, struct xemu_snapshot_block_st *block )
{
	int ret;
	Uint8 buffer[SNAPSHOT_CPU65_BLOCK_SIZE];
	if (block->sub_counter || block->block_version != SNAPSHOT_CPU65_BLOCK_VERSION || block->sub_size != sizeof buffer)
		RETURN_XSNAPERR_USER("Bad CPU 65xx block syntax");
	ret = xemusnap_read_file(buffer, sizeof buffer);
	if (ret) return ret;
	if (buffer[0] != SNAPSHOT_CPU65_ID)
		RETURN_XSNAPERR_USER("CPU type mismatch");
	CPU65.pc = P_AS_BE16(buffer + 1);
	CPU65.a = buffer[3];
	CPU65.x = buffer[4];
	CPU65.y = buffer[5];
	CPU65.s = buffer[6];
	cpu65_set_pf(buffer[7]);
	CPU65.pf_e = buffer[7] & CPU65_PF_E;	// must be set manually ....
	CPU65.irqLevel = (int)P_AS_BE32(buffer + 32);
	CPU65.nmiEdge  = (int)P_AS_BE32(buffer + 36);
	CPU65.op_cycles = buffer[42];
	CPU65.op = buffer[43];
#ifdef CPU_65CE02
	CPU65.z = buffer[64];
	CPU65.bphi = (Uint16)buffer[65] << 8;
	CPU65.sphi = (Uint16)buffer[66] << 8;
	CPU65.cpu_inhibit_interrupts = (int)P_AS_BE32(buffer + 96);
#endif
	return 0;
}


int cpu65_snapshot_save_state ( const struct xemu_snapshot_definition_st *def )
{
	Uint8 buffer[SNAPSHOT_CPU65_BLOCK_SIZE];
	int ret = xemusnap_write_block_header(def->idstr, SNAPSHOT_CPU65_BLOCK_VERSION);
	if (ret) return ret;
	memset(buffer, 0xFF, sizeof buffer);
	buffer[0] = SNAPSHOT_CPU65_ID;
	U16_AS_BE(buffer + 1, CPU65.pc);
	buffer[3] = CPU65.a;
	buffer[4] = CPU65.x;
	buffer[5] = CPU65.y;
	buffer[6] = CPU65.s;
	buffer[7] = cpu65_get_pf();
	U32_AS_BE(buffer + 32, (Uint32)CPU65.irqLevel);
	U32_AS_BE(buffer + 36, (Uint32)CPU65.nmiEdge);
	buffer[42] = CPU65.op_cycles;
	buffer[43] = CPU65.op;
#ifdef CPU_65CE02
	buffer[64] = CPU65.z;
	buffer[65] = CPU65.bphi >> 8;
	buffer[66] = CPU65.sphi >> 8;
	U32_AS_BE(buffer + 96, (Uint32)CPU65.cpu_inhibit_interrupts);
#endif
	return xemusnap_write_sub_block(buffer, sizeof buffer);
}
#endif
