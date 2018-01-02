/* Xemu - Somewhat lame emulation (running on Linux/Unix/Windows/OSX, utilizing
   SDL2) of some 8 bit machines, including the Commodore LCD and Commodore 65
   and some Mega-65 features as well.
   Copyright (C)2016-2018 LGB (Gábor Lénárt) <lgblgblgb@gmail.com>

   THIS IS AN UGLY PIECE OF SOURCE REALLY.

   Quite confusing comment section even at the beginning, from this point ...

   | This file tries to implement a 65C02 CPU. Also, there is an on-going work to be able to
   | emulate a 65CE02 on request as well. These *may* be used in my experiments
   | and not too much related to the Commodore LCD at all!
   | Note: the original solution was *generated* source, that can explain the structure.
   | Note: it was written in JavaScript, but the conversion to C and custom modification
   | Note: does not use this generation scheme anymore.

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

/* Original information/copyright:
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
#define CPU_TYPE "65C02"
#define ZERO_REG	0
#endif

#define A_OP(op,dat) CPU65.a = CPU65.a op dat


#ifdef MEGA65
//#warning "Compiling for MEGA65, hacky stuff!"
#define IS_FLAT32_DATA_OP() XEMU_UNLIKELY(CPU65.previous_op == 0xEA && cpu_linear_memory_addressing_is_enabled)
#endif

#ifdef CPU_65CE02
#ifdef DEBUG_CPU
#define OPC_65CE02(w) DEBUG("CPU: 65CE02 opcode: %s" NL, w)
#else
#define OPC_65CE02(w)
#endif
static const Uint8 opcycles[] = {7,5,2,2,4,3,4,4,3,2,1,1,5,4,5,4,2,5,5,3,4,3,4,4,1,4,1,1,5,4,5,4,5,5,7,7,3,3,4,4,3,2,1,1,4,4,5,4,2,5,5,3,3,3,4,4,1,4,1,1,4,4,5,4,5,5,2,2,4,3,4,4,3,2,1,1,3,4,5,4,2,5,5,3,4,3,4,4,2,4,3,1,4,4,5,4,4,5,7,5,3,3,4,4,3,2,1,1,5,4,5,4,2,5,5,3,3,3,4,4,2,4,3,1,5,4,5,4,2,5,6,3,3,3,3,4,1,2,1,4,4,4,4,4,2,5,5,3,3,3,3,4,1,4,1,4,4,4,4,4,2,5,2,2,3,3,3,4,1,2,1,4,4,4,4,4,2,5,5,3,3,3,3,4,1,4,1,4,4,4,4,4,2,5,2,6,3,3,4,4,1,2,1,7,4,4,5,4,2,5,5,3,3,3,4,4,1,4,3,3,4,4,5,4,2,5,6,6,3,3,4,4,1,2,1,7,4,4,5,4,2,5,5,3,5,3,4,4,1,4,3,3,7,4,5,4};
#else
static const Uint8 opcycles[] = {7,6,2,2,5,3,5,5,3,2,2,2,6,4,6,2,2,5,5,2,5,4,6,5,2,4,2,2,6,4,7,2,6,6,2,2,3,3,5,5,4,2,2,2,4,4,6,2,2,5,5,2,4,4,6,5,2,4,2,2,4,4,7,2,6,6,2,2,3,3,5,5,3,2,2,2,3,4,6,2,2,5,5,2,4,4,6,5,2,4,3,2,2,4,7,2,6,6,2,2,3,3,5,5,4,2,2,2,5,4,6,2,2,5,5,2,4,4,6,5,2,4,4,2,6,4,7,2,3,6,2,2,3,3,3,5,2,2,2,2,4,4,4,2,2,6,5,2,4,4,4,5,2,5,2,2,4,5,5,2,2,6,2,2,3,3,3,5,2,2,2,2,4,4,4,2,2,5,5,2,4,4,4,5,2,4,2,2,4,4,4,2,2,6,2,2,3,3,5,5,2,2,2,2,4,4,6,2,2,5,5,2,4,4,6,5,2,4,3,2,2,4,7,2,2,6,2,2,3,3,5,5,2,2,2,2,4,4,6,2,2,5,5,2,4,4,6,5,2,4,4,2,2,4,7,2};
#endif


#define PF_N 0x80
#define PF_V 0x40
#define PF_E 0x20
#define PF_B 0x10
#define PF_D 0x08
#define PF_I 0x04
#define PF_Z 0x02
#define PF_C 0x01

#ifndef CPU65_DISCRETE_PF_NZ
#define VALUE_TO_PF_ZERO(a) ((a) ? 0 : PF_Z)
#endif

#define writeFlatAddressedByte(d)	cpu65_write_linear_opcode_callback(d)
#define writeByteTwice(a,od,nd)		cpu65_write_rmw_callback(a,od,nd)
#define readFlatAddressedByte()		cpu65_read_linear_opcode_callback()
#define writeByte(a,d)			cpu65_write_callback(a,d)
#define readByte(a)			cpu65_read_callback(a)


static XEMU_INLINE Uint16 readWord(Uint16 addr) {
	return readByte(addr) | (readByte(addr + 1) << 8);
}

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
	CPU65.pf_n = st & PF_N;
	CPU65.pf_z = st & PF_Z;
#else
	CPU65.pf_nz = st & (PF_N | PF_Z);
#endif
	CPU65.pf_v = st & PF_V;
#ifdef CPU_65CE02
	// Note: E bit cannot be changed by PLP/RTI, so it's commented out here ...
	// At least *I* think :) FIXME?
	// CPU65.pf_e = st & PF_E;
#endif
	CPU65.pf_d = st & PF_D;
	CPU65.pf_i = st & PF_I;
	CPU65.pf_c = st & PF_C;
}

Uint8 cpu65_get_pf() {
	return
#ifdef CPU65_DISCRETE_PF_NZ
	(CPU65.pf_n ? PF_N : 0) | (CPU65.pf_z ? PF_Z : 0)
#else
	CPU65.pf_nz
#endif
	|
	(CPU65.pf_v ?  PF_V : 0) |
#ifdef CPU_65CE02
	(CPU65.pf_e ?  PF_E : 0) |
#else
	PF_E |
#endif
	(CPU65.pf_d ? PF_D : 0) |
	(CPU65.pf_i ? PF_I : 0) |
	(CPU65.pf_c ? PF_C : 0);
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
	CPU65.pc = readWord(0xFFFC);
	DEBUG("CPU[" CPU_TYPE "]: RESET, PC=%04X" NL, CPU65.pc);
}


static XEMU_INLINE void setNZ(Uint8 st) {
#ifdef CPU65_DISCRETE_PF_NZ
	CPU65.pf_n = st & PF_N;
	CPU65.pf_z = !st;
#else
	CPU65.pf_nz = (st & PF_N) | VALUE_TO_PF_ZERO(st);
#endif
}
#ifdef CPU_65CE02
static XEMU_INLINE void setNZ16(Uint16 st) {
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
	setNZ(temp);
}
static XEMU_INLINE void _TSB(int addr) {
	Uint8 m = readByte(addr);
#ifdef CPU65_DISCRETE_PF_NZ
	CPU65.pf_z = (!(m & CPU65.a));
#else
	if (m & CPU65.a) CPU65.pf_nz &= (~PF_Z); else CPU65.pf_nz |= PF_Z;
#endif
	writeByte(addr, m | CPU65.a);
}
static XEMU_INLINE void _TRB(int addr) {
	Uint8 m = readByte(addr);
#ifdef CPU65_DISCRETE_PF_NZ
	CPU65.pf_z = (!(m & CPU65.a));
#else
	if (m & CPU65.a) CPU65.pf_nz &= (~PF_Z); else CPU65.pf_nz |= PF_Z;
#endif
	writeByte(addr, m & (255 - CPU65.a));
}
static XEMU_INLINE void _ASL(int addr) {
	Uint8 t = (addr == -1 ? CPU65.a : readByte(addr));
	Uint8 o = t;
	CPU65.pf_c = t & 128;
	//t = (t << 1) & 0xFF;
	t <<= 1;
	setNZ(t);
	if (addr == -1) CPU65.a = t; else writeByteTwice(addr, o, t);
}
static XEMU_INLINE void _LSR(int addr) {
	Uint8 t = (addr == -1 ? CPU65.a : readByte(addr));
	Uint8 o = t;
	CPU65.pf_c = t & 1;
	//t = (t >> 1) & 0xFF;
	t >>= 1;
	setNZ(t);
	if (addr == -1) CPU65.a = t; else writeByteTwice(addr, o, t);
}
#ifdef CPU_65CE02
static XEMU_INLINE void _ASR(int addr) {
	Uint8 t = (addr == -1 ? CPU65.a : readByte(addr));
	Uint8 o = t;
	CPU65.pf_c = t & 1;
	t = (t >> 1) | (t & 0x80);
	setNZ(t);
	if (addr == -1) CPU65.a = t; else writeByteTwice(addr, o, t);
}
#endif
static XEMU_INLINE void _BIT(Uint8 data) {
	CPU65.pf_v = data & 64;
#ifdef CPU65_DISCRETE_PF_NZ
	CPU65.pf_n = data & PF_N;
	CPU65.pf_z = (!(CPU65.a & data));
#else
	CPU65.pf_nz = (data & PF_N) | VALUE_TO_PF_ZERO(CPU65.a & data);
#endif
}
static XEMU_INLINE void _ADC(int data) {
	if (CPU65.pf_d) {
		Uint16 temp  = (CPU65.a & 0x0F) + (data & 0x0F) + (CPU65.pf_c ? 1 : 0);
		Uint16 temp2 = (CPU65.a & 0xF0) + (data & 0xF0);
		if (temp > 9) { temp2 += 0x10; temp += 6; }
		CPU65.pf_v = (~(CPU65.a ^ data) & (CPU65.a ^ temp) & 0x80);
		if (temp2 > 0x90) temp2 += 0x60;
		CPU65.pf_c = (temp2 & 0xFF00);
		CPU65.a = (temp & 0x0F) + (temp2 & 0xF0);
		setNZ(CPU65.a);
	} else {
		Uint16 temp = data + CPU65.a + (CPU65.pf_c ? 1 : 0);
		CPU65.pf_c = temp > 0xFF;
		CPU65.pf_v = (!((CPU65.a ^ data) & 0x80) && ((CPU65.a ^ temp) & 0x80));
		CPU65.a = temp & 0xFF;
		setNZ(CPU65.a);
	}
}
static XEMU_INLINE void _SBC(int data) {
	if (CPU65.pf_d) {
		Uint16 temp = CPU65.a - (data & 0x0F) - (CPU65.pf_c ? 0 : 1);
		if ((temp & 0x0F) > (CPU65.a & 0x0F)) temp -= 6;
		temp -= (data & 0xF0);
		if ((temp & 0xF0) > (CPU65.a & 0xF0)) temp -= 0x60;
		CPU65.pf_v = (!(temp > CPU65.a));
		CPU65.pf_c = (!(temp > CPU65.a));
		CPU65.a = temp & 0xFF;
		setNZ(CPU65.a);
	} else {
		Uint16 temp = CPU65.a - data - (CPU65.pf_c ? 0 : 1);
		CPU65.pf_c = temp < 0x100;
		CPU65.pf_v = ((CPU65.a ^ temp) & 0x80) && ((CPU65.a ^ data) & 0x80);
		CPU65.a = temp & 0xFF;
		setNZ(CPU65.a);
	}
}
static XEMU_INLINE void _ROR(int addr) {
	Uint16 t = ((addr == -1) ? CPU65.a : readByte(addr));
	Uint8  o = t;
	if (CPU65.pf_c) t |= 0x100;
	CPU65.pf_c = t & 1;
	t >>= 1;
	setNZ(t);
	if (addr == -1) CPU65.a = t; else writeByteTwice(addr, o, t);
}
static XEMU_INLINE void _ROL(int addr) {
	Uint16 t = ((addr == -1) ? CPU65.a : readByte(addr));
	Uint8  o = t;
	t = (t << 1) | (CPU65.pf_c ? 1 : 0);
	CPU65.pf_c = t & 0x100;
	t &= 0xFF;
	setNZ(t);
	if (addr == -1) CPU65.a = t; else writeByteTwice(addr, o, t);
}


//static Uint8 last_p;


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
	)) {
#ifdef DEBUG_CPU
		DEBUG("CPU: serving NMI on NMI edge at PC $%04X" NL, CPU65.pc);
#endif
		CPU65.nmiEdge = 0;
		pushWord(CPU65.pc);
		push(cpu65_get_pf());	// no PF_B is pushed!
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
	)) {
#ifdef DEBUG_CPU
		DEBUG("CPU: servint IRQ on IRQ level at PC $%04X" NL, CPU65.pc);
#endif
		//last_p = cpu65_get_pf();
		pushWord(CPU65.pc);
		push(cpu65_get_pf());	// no PF_B is pushed!
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
	case 0x00:
#ifdef DEBUG_CPU
			DEBUG("CPU: WARN: BRK is about executing at PC=$%04X" NL, (CPU65.pc - 1) & 0xFFFF);
#endif
			// FIXME: does BRK sets I and D flag? Hmm, I can't even remember now why I wrote these :-D
			// FIXME-2: does BRK sets B flag, or only in the saved copy on the stack??
			// NOTE: D flag clearing was not done on the original 6502 I guess, but indeed on the 65C02 already
			pushWord(CPU65.pc + 1); push(cpu65_get_pf() | PF_B); CPU65.pf_d = 0; CPU65.pf_i = 1; CPU65.pc = readWord(0xFFFE); /* 0x0 BRK Implied */
			break;
	case 0x01:	setNZ(A_OP(|,readByte(_zpxi()))); break; /* 0x1 ORA (Zero_Page,X) */
	case 0x02:
#ifdef CPU_65CE02
			OPC_65CE02("CLE");
			CPU65.pf_e = 0;	// 65CE02: CLE
#ifdef DEBUG_CPU
			DEBUG("CPU: WARN: E flag is cleared!" NL);
#endif
#else
			CPU65.pc++; /* 0x2 NOP imm (non-std NOP with addr mode) */
#endif
			break;
	case 0x03:
#ifdef CPU_65CE02
			OPC_65CE02("SEE");
			CPU65.pf_e = 1;	// 65CE02: SEE
#endif
			break; /* 0x3 NOP (nonstd loc, implied) */
	case 0x04:	_TSB(_zp()); break; /* 0x4 TSB Zero_Page */
	case 0x05:	setNZ(A_OP(|,readByte(_zp()))); break; /* 0x5 ORA Zero_Page */
	case 0x06:	_ASL(_zp()); break; /* 0x6 ASL Zero_Page */
	case 0x07:	{ int a = _zp(); writeByte(a, readByte(a) & 254);  } break; /* 0x7 RMB Zero_Page */
	case 0x08:	push(cpu65_get_pf() | PF_B); break; /* 0x8 PHP Implied */
	case 0x09:	setNZ(A_OP(|,readByte(_imm()))); break; /* 0x9 ORA Immediate */
	case 0x0a:	_ASL(-1); break; /* 0xa ASL Accumulator */
	case 0x0b:
#ifdef CPU_65CE02
			OPC_65CE02("TSY");
			setNZ(CPU65.y = (CPU65.sphi >> 8));   // TSY                  0B   65CE02
#endif
			break; /* 0xb NOP (nonstd loc, implied) */
	case 0x0c:	_TSB(_abs()); break; /* 0xc TSB Absolute */
	case 0x0d:	setNZ(A_OP(|,readByte(_abs()))); break; /* 0xd ORA Absolute */
	case 0x0e:	_ASL(_abs()); break; /* 0xe ASL Absolute */
	case 0x0f:	_BRA(!(readByte(_zp()) & 1)); break; /* 0xf BBR Relative */
	case 0x10:
#ifdef CPU65_DISCRETE_PF_NZ
			_BRA(! CPU65.pf_n);
#else
			_BRA(!(CPU65.pf_nz & PF_N));
#endif
			break;	/* 0x10 BPL Relative */
	case 0x11:	setNZ(A_OP(|,readByte(_zpiy()))); break; /* 0x11 ORA (Zero_Page),Y */
	case 0x12:
			/* 0x12 ORA (Zero_Page) or (ZP),Z on 65CE02 */
#ifdef MEGA65
			if (IS_FLAT32_DATA_OP())
				setNZ(A_OP(|,readFlatAddressedByte()));
			else
#endif
				setNZ(A_OP(|,readByte(_zpi())));
			break;
	case 0x13:
#ifdef CPU_65CE02
			OPC_65CE02("BPL16");
			// 65CE02: BPL 16 bit relative
#ifdef CPU65_DISCRETE_PF_NZ
			_BRA16(!CPU65.pf_n);
#else
			_BRA16(!(CPU65.pf_nz & PF_N));
#endif
#endif
			break; /* 0x13 NOP (nonstd loc, implied) */
	case 0x14:	_TRB(_zp()); break; /* 0x14 TRB Zero_Page */
	case 0x15:	setNZ(A_OP(|,readByte(_zpx()))); break; /* 0x15 ORA Zero_Page,X */
	case 0x16:	_ASL(_zpx()); break; /* 0x16 ASL Zero_Page,X */
	case 0x17:	{ int a = _zp(); writeByte(a, readByte(a) & 253); } break; /* 0x17 RMB Zero_Page */
	case 0x18:	CPU65.pf_c = 0; break; /* 0x18 CLC Implied */
	case 0x19:	setNZ(A_OP(|,readByte(_absy()))); break; /* 0x19 ORA Absolute,Y */
	case 0x1a:	setNZ(++CPU65.a); break; /* 0x1a INA Accumulator */
	case 0x1b:
#ifdef CPU_65CE02
			OPC_65CE02("INZ");
			setNZ(++CPU65.z);	// 65CE02: INZ
#endif
			break; /* 0x1b NOP (nonstd loc, implied) */
	case 0x1c:	_TRB(_abs()); break; /* 0x1c TRB Absolute */
	case 0x1d:	setNZ(A_OP(|,readByte(_absx()))); break; /* 0x1d ORA Absolute,X */
	case 0x1e:	_ASL(_absx()); break; /* 0x1e ASL Absolute,X */
	case 0x1f:	_BRA(!(readByte(_zp()) & 2)); break; /* 0x1f BBR Relative */
	case 0x20:	pushWord(CPU65.pc + 1); CPU65.pc = _abs(); break; /* 0x20 JSR Absolute */
	case 0x21:	setNZ(A_OP(&,readByte(_zpxi()))); break; /* 0x21 AND (Zero_Page,X) */
	case 0x22:
#ifdef CPU_65CE02
			OPC_65CE02("JSR (nnnn)");
			// 65CE02 JSR ($nnnn)
			pushWord(CPU65.pc + 1);
			CPU65.pc = _absi();
#else
			CPU65.pc++;	/* 0x22 NOP imm (non-std NOP with addr mode) */
#endif
			break;
	case 0x23:
#ifdef CPU_65CE02
			OPC_65CE02("JSR (nnnn,X)");
			// 65CE02 JSR ($nnnn,X)
			pushWord(CPU65.pc + 1);
			CPU65.pc = _absxi();
#endif
			break; /* 0x23 NOP (nonstd loc, implied) */
	case 0x24:	_BIT(readByte(_zp())); break; /* 0x24 BIT Zero_Page */
	case 0x25:	setNZ(A_OP(&,readByte(_zp()))); break; /* 0x25 AND Zero_Page */
	case 0x26:	_ROL(_zp()); break; /* 0x26 ROL Zero_Page */
	case 0x27:	{ int a = _zp(); writeByte(a, readByte(a) & 251); } break; /* 0x27 RMB Zero_Page */
	case 0x28:
			cpu65_set_pf(pop());
			break; /* 0x28 PLP Implied */
	case 0x29:	setNZ(A_OP(&,readByte(_imm()))); break; /* 0x29 AND Immediate */
	case 0x2a:	_ROL(-1); break; /* 0x2a ROL Accumulator */
	case 0x2b:
#ifdef CPU_65CE02
			OPC_65CE02("TYS");
			CPU65.sphi = CPU65.y << 8;	// 65CE02	TYS
#ifdef DEBUG_CPU
			if (CPU65.sphi != 0x100)
				DEBUG("CPU: WARN: stack page is set non-0x100: $%04X" NL, CPU65.sphi);
#endif
#endif
			break; /* 0x2b NOP (nonstd loc, implied) */
	case 0x2c:	_BIT(readByte(_abs())); break; /* 0x2c BIT Absolute */
	case 0x2d:	setNZ(A_OP(&,readByte(_abs()))); break; /* 0x2d AND Absolute */
	case 0x2e:	_ROL(_abs()); break; /* 0x2e ROL Absolute */
	case 0x2f:	_BRA(!(readByte(_zp()) & 4)); break; /* 0x2f BBR Relative */
	case 0x30:
#ifdef CPU65_DISCRETE_PF_NZ
			_BRA(CPU65.pf_n);
#else
			_BRA(CPU65.pf_nz & PF_N);
#endif
			break; /* 0x30 BMI Relative */
	case 0x31:	setNZ(A_OP(&,readByte(_zpiy()))); break; /* 0x31 AND (Zero_Page),Y */
	case 0x32:
			/* 0x32 AND (Zero_Page) or (ZP),Z on 65CE02*/
#ifdef MEGA65
			if (IS_FLAT32_DATA_OP())
				setNZ(A_OP(&,readFlatAddressedByte()));
			else
#endif
				setNZ(A_OP(&,readByte(_zpi())));
			break;
	case 0x33:
#ifdef CPU_65CE02
			OPC_65CE02("BMI16");
#ifdef CPU65_DISCRETE_PF_NZ
			_BRA16(CPU65.pf_n);
#else
			_BRA16(CPU65.pf_nz & PF_N);
#endif
			// 65CE02 BMI 16 bit relative
#endif
			break; /* 0x33 NOP (nonstd loc, implied) */
	case 0x34:	_BIT(readByte(_zpx())); break; /* 0x34 BIT Zero_Page,X */
	case 0x35:	setNZ(A_OP(&,readByte(_zpx()))); break; /* 0x35 AND Zero_Page,X */
	case 0x36:	_ROL(_zpx()); break; /* 0x36 ROL Zero_Page,X */
	case 0x37:	{ int a = _zp(); writeByte(a, readByte(a) & 247); } break; /* 0x37 RMB Zero_Page */
	case 0x38:	CPU65.pf_c = 1; break; /* 0x38 SEC Implied */
	case 0x39:	setNZ(A_OP(&,readByte(_absy()))); break; /* 0x39 AND Absolute,Y */
	case 0x3a:	setNZ(--CPU65.a); break; /* 0x3a DEA Accumulator */
	case 0x3b:
#ifdef CPU_65CE02
			OPC_65CE02("DEZ");
			setNZ(--CPU65.z);		// 65CE02	DEZ
#endif
			break; /* 0x3b NOP (nonstd loc, implied) */
	case 0x3c:	_BIT(readByte(_absx())); break; /* 0x3c BIT Absolute,X */
	case 0x3d:	setNZ(A_OP(&,readByte(_absx()))); break; /* 0x3d AND Absolute,X */
	case 0x3e:	_ROL(_absx()); break; /* 0x3e ROL Absolute,X */
	case 0x3f:	_BRA(!(readByte(_zp()) & 8)); break; /* 0x3f BBR Relative */
	case 0x40:	cpu65_set_pf(pop()); CPU65.pc = popWord(); break; /* 0x40 RTI Implied */
	case 0x41:	setNZ(A_OP(^,readByte(_zpxi()))); break; /* 0x41 EOR (Zero_Page,X) */
	case 0x42:
#ifdef CPU_65CE02
			OPC_65CE02("NEG");
			setNZ(CPU65.a = -CPU65.a);	// 65CE02: NEG	FIXME: flags etc are correct?
#else
			CPU65.pc++;	/* 0x42 NOP imm (non-std NOP with addr mode) */
#endif
			break;
	case 0x43:
#ifdef CPU_65CE02
			// 65CE02: ASR A
			OPC_65CE02("ASR A");
			_ASR(-1);
			//CPU65.pf_c = CPU65.a & 1;
			//CPU65.a = (CPU65.a >> 1) | (CPU65.a & 0x80);
			//setNZ(CPU65.a);
#endif
			break; /* 0x43 NOP (nonstd loc, implied) */
	case 0x44:
#ifdef CPU_65CE02
			OPC_65CE02("ASR nn");
			_ASR(_zp());				// 65CE02: ASR $nn
#else
			CPU65.pc++;	// 0x44 NOP zp (non-std NOP with addr mode)
#endif
			break;
	case 0x45:	setNZ(A_OP(^,readByte(_zp()))); break; /* 0x45 EOR Zero_Page */
	case 0x46:	_LSR(_zp()); break; /* 0x46 LSR Zero_Page */
	case 0x47:	{ int a = _zp(); writeByte(a, readByte(a) & 239); } break; /* 0x47 RMB Zero_Page */
	case 0x48:	push(CPU65.a); break; /* 0x48 PHA Implied */
	case 0x49:	setNZ(A_OP(^,readByte(_imm()))); break; /* 0x49 EOR Immediate */
	case 0x4a:	_LSR(-1); break; /* 0x4a LSR Accumulator */
	case 0x4b:
#ifdef CPU_65CE02
			OPC_65CE02("TAZ");
			setNZ(CPU65.z = CPU65.a);	// 65CE02: TAZ
#endif
			break; /* 0x4b NOP (nonstd loc, implied) */
	case 0x4c:	CPU65.pc = _abs(); break; /* 0x4c JMP Absolute */
	case 0x4d:	setNZ(A_OP(^,readByte(_abs()))); break; /* 0x4d EOR Absolute */
	case 0x4e:	_LSR(_abs()); break; /* 0x4e LSR Absolute */
	case 0x4f:	_BRA(!(readByte(_zp()) & 16)); break; /* 0x4f BBR Relative */
	case 0x50:	_BRA(!CPU65.pf_v); break; /* 0x50 BVC Relative */
	case 0x51:	setNZ(A_OP(^,readByte(_zpiy()))); break; /* 0x51 EOR (Zero_Page),Y */
	case 0x52:	/* 0x52 EOR (Zero_Page) or (ZP),Z on 65CE02 */
#ifdef MEGA65
			if (IS_FLAT32_DATA_OP())
				setNZ(A_OP(^,readFlatAddressedByte()));
			else
#endif
				setNZ(A_OP(^,readByte(_zpi())));
			break;
	case 0x53:
#ifdef CPU_65CE02
			OPC_65CE02("BVC16");
			_BRA16(!CPU65.pf_v); // 65CE02: BVC 16-bit-relative
#endif
			break; /* 0x53 NOP (nonstd loc, implied) */
	case 0x54:
#ifdef CPU_65CE02
			OPC_65CE02("ASR nn,X");
			_ASR(_zpx());				// ASR $nn,X
#else
			CPU65.pc++;	// NOP zpx (non-std NOP with addr mode)
#endif
			break;
	case 0x55:	setNZ(A_OP(^,readByte(_zpx()))); break; /* 0x55 EOR Zero_Page,X */
	case 0x56:	_LSR(_zpx()); break; /* 0x56 LSR Zero_Page,X */
	case 0x57:	{ int a = _zp(); writeByte(a, readByte(a) & 223); } break; /* 0x57 RMB Zero_Page */
	case 0x58:	CPU65.pf_i = 0; break; /* 0x58 CLI Implied */
	case 0x59:	setNZ(A_OP(^,readByte(_absy()))); break; /* 0x59 EOR Absolute,Y */
	case 0x5a:	push(CPU65.y); break; /* 0x5a PHY Implied */
	case 0x5b:
#ifdef CPU_65CE02
			OPC_65CE02("TAB");
			CPU65.bphi = CPU65.a << 8; // 65CE02: TAB
#ifdef DEBUG_CPU
			if (CPU65.bphi)
				DEBUG("CPU: WARN base page is non-zero now with value of $%04X" NL, CPU65.bphi);
#endif
#endif
			break; /* 0x5b NOP (nonstd loc, implied) */
	case 0x5c:
#ifdef CPU_65CE02
			OPC_65CE02("MAP");
			cpu65_do_aug_callback();	/* 0x5c on 65CE02: this is the "AUG" opcode. It must be handled by the emulator, on 4510 (C65) it's redefined as MAP for MMU functionality */
#else
			CPU65.pc += 2;
#endif
			break; /* 0x5c NOP (nonstd loc, implied) */ // FIXME: NOP absolute!
	case 0x5d:	setNZ(A_OP(^,readByte(_absx()))); break; /* 0x5d EOR Absolute,X */
	case 0x5e:	_LSR(_absx()); break; /* 0x5e LSR Absolute,X */
	case 0x5f:	_BRA(!(readByte(_zp()) & 32)); break; /* 0x5f BBR Relative */
	case 0x60:	CPU65.pc = popWord() + 1; break; /* 0x60 RTS Implied */
	case 0x61:	_ADC(readByte(_zpxi())); break; /* 0x61 ADC (Zero_Page,X) */
	case 0x62:
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
			break;
	case 0x63:
#ifdef CPU_65CE02
			OPC_65CE02("BSR16");
			// 65C02 ?! BSR $nnnn Interesting 65C02-only? FIXME TODO: does this opcode exist before 65CE02 as well?!
			pushWord(CPU65.pc + 1);
			_BRA16(1);
#endif
			break; /* 0x63 NOP (nonstd loc, implied) */
	case 0x64:	writeByte(_zp(), ZERO_REG); break; /* 0x64 STZ Zero_Page */
	case 0x65:	_ADC(readByte(_zp())); break; /* 0x65 ADC Zero_Page */
	case 0x66:	_ROR(_zp()); break; /* 0x66 ROR Zero_Page */
	case 0x67:	{ int a = _zp(); writeByte(a, readByte(a) & 191); } break; /* 0x67 RMB Zero_Page */
	case 0x68:	setNZ(CPU65.a = pop()); break; /* 0x68 PLA Implied */
	case 0x69:	_ADC(readByte(_imm())); break; /* 0x69 ADC Immediate */
	case 0x6a:	_ROR(-1); break; /* 0x6a ROR Accumulator */
	case 0x6b:
#ifdef CPU_65CE02
			OPC_65CE02("TZA");
			setNZ(CPU65.a = CPU65.z);	// 65CE02 TZA
#endif
			break; /* 0x6b NOP (nonstd loc, implied) */
	case 0x6c:	CPU65.pc = _absi(); break; /* 0x6c JMP (Absolute) */
	case 0x6d:	_ADC(readByte(_abs())); break; /* 0x6d ADC Absolute */
	case 0x6e:	_ROR(_abs()); break; /* 0x6e ROR Absolute */
	case 0x6f:	_BRA(!(readByte(_zp()) & 64)); break; /* 0x6f BBR Relative */
	case 0x70:	_BRA(CPU65.pf_v); break; /* 0x70 BVS Relative */
	case 0x71:	_ADC(readByte(_zpiy())); break; /* 0x71 ADC (Zero_Page),Y */
	case 0x72:	/* 0x72 ADC (Zero_Page) or (ZP),Z on 65CE02 */
#ifdef MEGA65
			if (IS_FLAT32_DATA_OP())
				_ADC(readFlatAddressedByte());
			else
#endif
				_ADC(readByte(_zpi()));
			break;
	case 0x73:
#ifdef CPU_65CE02
			OPC_65CE02("BVS16");
			_BRA16(CPU65.pf_v);	// 65CE02 BVS 16 bit relative
#endif
			break; /* 0x73 NOP (nonstd loc, implied) */
	case 0x74:	writeByte(_zpx(), ZERO_REG); break; /* 0x74 STZ Zero_Page,X */
	case 0x75:	_ADC(readByte(_zpx())); break; /* 0x75 ADC Zero_Page,X */
	case 0x76:	_ROR(_zpx()); break; /* 0x76 ROR Zero_Page,X */
	case 0x77:	{ int a = _zp(); writeByte(a, readByte(a) & 127); } break; /* 0x77 RMB Zero_Page */
	case 0x78:	CPU65.pf_i = 1; break; /* 0x78 SEI Implied */
	case 0x79:	_ADC(readByte(_absy())); break; /* 0x79 ADC Absolute,Y */
	case 0x7a:	setNZ(CPU65.y = pop()); break; /* 0x7a PLY Implied */
	case 0x7b:
#ifdef CPU_65CE02
			OPC_65CE02("TBA");
			setNZ(CPU65.a = (CPU65.bphi >> 8));	// 65C02 TBA
#endif
			break; /* 0x7b NOP (nonstd loc, implied) */
	case 0x7c:	CPU65.pc = _absxi(); break; /* 0x7c JMP (Absolute,X) */
	case 0x7d:	_ADC(readByte(_absx())); break; /* 0x7d ADC Absolute,X */
	case 0x7e:	_ROR(_absx()); break; /* 0x7e ROR Absolute,X */
	case 0x7f:	_BRA(!(readByte(_zp()) & 128)); break; /* 0x7f BBR Relative */
	case 0x80:	_BRA(1); break; /* 0x80 BRA Relative */
	case 0x81:	writeByte(_zpxi(), CPU65.a); break; /* 0x81 STA (Zero_Page,X) */
	case 0x82:
#ifdef CPU_65CE02
			OPC_65CE02("STA (nn,S),Y");
			writeByte(_GET_SP_INDIRECT_ADDR(), CPU65.a);	// 65CE02 STA ($nn,SP),Y
#else
			CPU65.pc++;	// NOP imm (non-std NOP with addr mode)
#endif
			break;
	case 0x83:
#ifdef CPU_65CE02
			OPC_65CE02("BRA16");
			_BRA16(1);	// 65CE02 BRA $nnnn 16-bit-pc-rel?
#endif
			break; /* 0x83 NOP (nonstd loc, implied) */
	case 0x84:	writeByte(_zp(), CPU65.y); break; /* 0x84 STY Zero_Page */
	case 0x85:	writeByte(_zp(), CPU65.a); break; /* 0x85 STA Zero_Page */
	case 0x86:	writeByte(_zp(), CPU65.x); break; /* 0x86 STX Zero_Page */
	case 0x87:	{ int a = _zp(); writeByte(a, readByte(a) | 1); } break; /* 0x87 SMB Zero_Page */
	case 0x88:	setNZ(--CPU65.y); break; /* 0x88 DEY Implied */
	case 0x89:
#ifdef CPU65_DISCRETE_PF_NZ
			CPU65.pf_z = (!(CPU65.a & readByte(_imm())));
#else
			if (CPU65.a & readByte(_imm())) CPU65.pf_nz &= (~PF_Z); else CPU65.pf_nz |= PF_Z;
#endif
			break;	/* 0x89 BIT+ Immediate */
	case 0x8a:	setNZ(CPU65.a = CPU65.x); break; /* 0x8a TXA Implied */
	case 0x8b:
#ifdef CPU_65CE02
			OPC_65CE02("STY nnnn,X");
			writeByte(_absx(), CPU65.y); // 65CE02 STY $nnnn,X
#endif
			break; /* 0x8b NOP (nonstd loc, implied) */
	case 0x8c:	writeByte(_abs(), CPU65.y); break; /* 0x8c STY Absolute */
	case 0x8d:	writeByte(_abs(), CPU65.a); break; /* 0x8d STA Absolute */
	case 0x8e:	writeByte(_abs(), CPU65.x); break; /* 0x8e STX Absolute */
	case 0x8f:	_BRA( readByte(_zp()) & 1 ); break; /* 0x8f BBS Relative */
	case 0x90:	_BRA(!CPU65.pf_c); break; /* 0x90 BCC Relative */
	case 0x91:	writeByte(_zpiy(), CPU65.a); break; /* 0x91 STA (Zero_Page),Y */
	case 0x92:	// /* 0x92 STA (Zero_Page) or (ZP),Z on 65CE02 */
#ifdef MEGA65
			if (IS_FLAT32_DATA_OP())
				writeFlatAddressedByte(CPU65.a);
			else
#endif
				writeByte(_zpi(), CPU65.a);
			break;
	case 0x93:
#ifdef CPU_65CE02
			OPC_65CE02("BCC16");
			_BRA16(!CPU65.pf_c);	// 65CE02  BCC $nnnn
#endif
			break; /* 0x93 NOP (nonstd loc, implied) */
	case 0x94:	writeByte(_zpx(), CPU65.y); break; /* 0x94 STY Zero_Page,X */
	case 0x95:	writeByte(_zpx(), CPU65.a); break; /* 0x95 STA Zero_Page,X */
	case 0x96:	writeByte(_zpy(), CPU65.x); break; /* 0x96 STX Zero_Page,Y */
	case 0x97:	{ int a = _zp(); writeByte(a, readByte(a) | 2); } break; /* 0x97 SMB Zero_Page */
	case 0x98:	setNZ(CPU65.a = CPU65.y); break; /* 0x98 TYA Implied */
	case 0x99:	writeByte(_absy(), CPU65.a); break; /* 0x99 STA Absolute,Y */
	case 0x9a:	CPU65.s = CPU65.x; break; /* 0x9a TXS Implied */
	case 0x9b:
#ifdef CPU_65CE02
			OPC_65CE02("STX nnnn,Y");
			writeByte(_absy(), CPU65.x);	// 65CE02 STX $nnnn,Y
#endif
			break; /* 0x9b NOP (nonstd loc, implied) */
	case 0x9c:	writeByte(_abs(), ZERO_REG); break; /* 0x9c STZ Absolute */
	case 0x9d:	writeByte(_absx(), CPU65.a); break; /* 0x9d STA Absolute,X */
	case 0x9e:	writeByte(_absx(), ZERO_REG); break; /* 0x9e STZ Absolute,X */
	case 0x9f:	_BRA( readByte(_zp()) & 2 ); break; /* 0x9f BBS Relative */
	case 0xa0:	setNZ(CPU65.y = readByte(_imm())); break; /* 0xa0 LDY Immediate */
	case 0xa1:	setNZ(CPU65.a = readByte(_zpxi())); break; /* 0xa1 LDA (Zero_Page,X) */
	case 0xa2:	setNZ(CPU65.x = readByte(_imm())); break; /* 0xa2 LDX Immediate */
	case 0xa3:
#ifdef CPU_65CE02
			OPC_65CE02("LDZ #nn");
			setNZ(CPU65.z = readByte(_imm())); // LDZ #$nn             A3   65CE02
#endif
			break; /* 0xa3 NOP (nonstd loc, implied) */
	case 0xa4:	setNZ(CPU65.y = readByte(_zp())); break; /* 0xa4 LDY Zero_Page */
	case 0xa5:	setNZ(CPU65.a = readByte(_zp())); break; /* 0xa5 LDA Zero_Page */
	case 0xa6:	setNZ(CPU65.x = readByte(_zp())); break; /* 0xa6 LDX Zero_Page */
	case 0xa7:	{ int a = _zp(); writeByte(a, readByte(a) | 4); } break; /* 0xa7 SMB Zero_Page */
	case 0xa8:	setNZ(CPU65.y = CPU65.a); break; /* 0xa8 TAY Implied */
	case 0xa9:	setNZ(CPU65.a = readByte(_imm())); break; /* 0xa9 LDA Immediate */
	case 0xaa:	setNZ(CPU65.x = CPU65.a); break; /* 0xaa TAX Implied */
	case 0xab:
#ifdef CPU_65CE02
			OPC_65CE02("LDZ nnnn");
			setNZ(CPU65.z = readByte(_abs()));	// 65CE02 LDZ $nnnn
#endif
			break; /* 0xab NOP (nonstd loc, implied) */
	case 0xac:	setNZ(CPU65.y = readByte(_abs())); break; /* 0xac LDY Absolute */
	case 0xad:	setNZ(CPU65.a = readByte(_abs())); break; /* 0xad LDA Absolute */
	case 0xae:	setNZ(CPU65.x = readByte(_abs())); break; /* 0xae LDX Absolute */
	case 0xaf:	_BRA( readByte(_zp()) & 4 ); break; /* 0xaf BBS Relative */
	case 0xb0:	_BRA(CPU65.pf_c); break; /* 0xb0 BCS Relative */
	case 0xb1:	setNZ(CPU65.a = readByte(_zpiy())); break; /* 0xb1 LDA (Zero_Page),Y */
	case 0xb2:	/* 0xb2 LDA (Zero_Page) or (ZP),Z on 65CE02 */
#ifdef MEGA65
			if (IS_FLAT32_DATA_OP())
				setNZ(CPU65.a = readFlatAddressedByte());
			else
#endif
				setNZ(CPU65.a = readByte(_zpi()));
			break;
	case 0xb3:
#ifdef CPU_65CE02
			OPC_65CE02("BCS16");
			_BRA16(CPU65.pf_c);	// 65CE02 BCS $nnnn
#endif
			break; /* 0xb3 NOP (nonstd loc, implied) */
	case 0xb4:	setNZ(CPU65.y = readByte(_zpx())); break; /* 0xb4 LDY Zero_Page,X */
	case 0xb5:	setNZ(CPU65.a = readByte(_zpx())); break; /* 0xb5 LDA Zero_Page,X */
	case 0xb6:	setNZ(CPU65.x = readByte(_zpy())); break; /* 0xb6 LDX Zero_Page,Y */
	case 0xb7:	{ int a = _zp(); writeByte(a, readByte(a) | 8); } break; /* 0xb7 SMB Zero_Page */
	case 0xb8:	CPU65.pf_v = 0; break; /* 0xb8 CLV Implied */
	case 0xb9:	setNZ(CPU65.a = readByte(_absy())); break; /* 0xb9 LDA Absolute,Y */
	case 0xba:	setNZ(CPU65.x = CPU65.s); break; /* 0xba TSX Implied */
	case 0xbb:
#ifdef CPU_65CE02
			OPC_65CE02("LDZ nnnn,X");
			setNZ(CPU65.z = readByte(_absx()));	// 65CE02 LDZ $nnnn,X
#endif
			break; /* 0xbb NOP (nonstd loc, implied) */
	case 0xbc:	setNZ(CPU65.y = readByte(_absx())); break; /* 0xbc LDY Absolute,X */
	case 0xbd:	setNZ(CPU65.a = readByte(_absx())); break; /* 0xbd LDA Absolute,X */
	case 0xbe:	setNZ(CPU65.x = readByte(_absy())); break; /* 0xbe LDX Absolute,Y */
	case 0xbf:	_BRA( readByte(_zp()) & 8 ); break; /* 0xbf BBS Relative */
	case 0xc0:	_CMP(CPU65.y, readByte(_imm())); break; /* 0xc0 CPY Immediate */
	case 0xc1:	_CMP(CPU65.a, readByte(_zpxi())); break; /* 0xc1 CMP (Zero_Page,X) */
	case 0xc2:
#ifdef CPU_65CE02
			OPC_65CE02("CPZ #nn");
			_CMP(CPU65.z, readByte(_imm()));	// 65CE02 CPZ #$nn
#else
			CPU65.pc++; // imm (non-std NOP with addr mode)
#endif
			break;
	case 0xc3:
#ifdef CPU_65CE02
			OPC_65CE02("DEW nn");
			{       //  DEW $nn 65CE02  C3  Decrement Word (maybe an error in 64NET.OPC ...) ANOTHER FIX: this is zero (errr, base ...) page!!!
                        int alo = _zp();
                        int ahi = (alo & 0xFF00) | ((alo + 1) & 0xFF);
                        Uint16 data = (readByte(alo) | (readByte(ahi) << 8)) - 1;
                        setNZ16(data);
                        writeByte(alo, data & 0xFF);
                        writeByte(ahi, data >> 8);
                        }
#endif
			break; /* 0xc3 NOP (nonstd loc, implied) */
	case 0xc4:	_CMP(CPU65.y, readByte(_zp())); break; /* 0xc4 CPY Zero_Page */
	case 0xc5:	_CMP(CPU65.a, readByte(_zp())); break; /* 0xc5 CMP Zero_Page */
	case 0xc6:	{ int addr = _zp(); Uint8 data = readByte(addr) - 1; setNZ(data); writeByte(addr, data); } break; /* 0xc6 DEC Zero_Page */
	case 0xc7:	{ int a = _zp(); writeByte(a, readByte(a) | 16); } break; /* 0xc7 SMB Zero_Page */
	case 0xc8:	setNZ(++CPU65.y); break; /* 0xc8 INY Implied */
	case 0xc9:	_CMP(CPU65.a, readByte(_imm())); break; /* 0xc9 CMP Immediate */
	case 0xca:	setNZ(--CPU65.x); break; /* 0xca DEX Implied */
	case 0xcb:
#ifdef CPU_65CE02
			OPC_65CE02("ASW nnnn");
			{					// 65CE02 ASW $nnnn	(CB  Arithmetic Shift Left Word)
			int addr = _abs();
			Uint16 data = readByte(addr) | (readByte(addr + 1) << 8);
			CPU65.pf_c = data & 0x8000;
			data <<= 1;
			setNZ16(data);
			writeByte(addr, data & 0xFF);
			writeByte(addr + 1, data >> 8);
			}
#endif
			break; /* 0xcb NOP (nonstd loc, implied) */
	case 0xcc:	_CMP(CPU65.y, readByte(_abs())); break; /* 0xcc CPY Absolute */
	case 0xcd:	_CMP(CPU65.a, readByte(_abs())); break; /* 0xcd CMP Absolute */
	case 0xce:	{ int addr = _abs(); Uint8 data = readByte(addr) - 1; setNZ(data); writeByte(addr, data); } break; /* 0xce DEC Absolute */
	case 0xcf:	_BRA( readByte(_zp()) & 16 ); break; /* 0xcf BBS Relative */
	case 0xd0:
#ifdef CPU65_DISCRETE_PF_NZ
			_BRA( !CPU65.pf_z);
#else
			_BRA(!(CPU65.pf_nz & PF_Z));
#endif
			break; /* 0xd0 BNE Relative */
	case 0xd1:	_CMP(CPU65.a, readByte(_zpiy())); break; /* 0xd1 CMP (Zero_Page),Y */
	case 0xd2:	/* 0xd2 CMP (Zero_Page) or (ZP),Z on 65CE02 */
#ifdef MEGA65
			if (IS_FLAT32_DATA_OP())	// NOTE: this was not mentioned in Paul's blog-post, but this op should have this property as well, IMHO!
				_CMP(CPU65.a, readFlatAddressedByte());
			else
#endif
				_CMP(CPU65.a, readByte(_zpi()));
			break;
	case 0xd3:
#ifdef CPU_65CE02
			OPC_65CE02("BNE16");
#ifdef CPU65_DISCRETE_PF_NZ
			_BRA16( !CPU65.pf_z);
#else
			_BRA16(!(CPU65.pf_nz & PF_Z));
#endif
			// 65CE02 BNE $nnnn
#endif
			break; /* 0xd3 NOP (nonstd loc, implied) */
	case 0xd4:
#ifdef CPU_65CE02
			OPC_65CE02("CPZ nn");
			_CMP(CPU65.z, readByte(_zp()));	// 65CE02 CPZ $nn
#else
			CPU65.pc++;	// NOP zpx (non-std NOP with addr mode)
#endif
			break;
	case 0xd5:	_CMP(CPU65.a, readByte(_zpx())); break; /* 0xd5 CMP Zero_Page,X */
	case 0xd6:	{ int addr = _zpx(); Uint8 data = readByte(addr) - 1; setNZ(data); writeByte(addr, data); } break; /* 0xd6 DEC Zero_Page,X */
	case 0xd7:	{ int a = _zp(); writeByte(a, readByte(a) | 32); } break; /* 0xd7 SMB Zero_Page */
	case 0xd8:	CPU65.pf_d = 0; break; /* 0xd8 CLD Implied */
	case 0xd9:	_CMP(CPU65.a, readByte(_absy())); break; /* 0xd9 CMP Absolute,Y */
	case 0xda:	push(CPU65.x); break; /* 0xda PHX Implied */
	case 0xdb:
#ifdef CPU_65CE02
			OPC_65CE02("PHZ");
			push(CPU65.z);		// 65CE02: PHZ
#endif
			break; /* 0xdb NOP (nonstd loc, implied) */
	case 0xdc:
#ifdef CPU_65CE02
			OPC_65CE02("CPZ nnnn");
			_CMP(CPU65.z, readByte(_abs())); // 65CE02 CPZ $nnnn
#else
			CPU65.pc += 2;
#endif
			break; /* 0xdc NOP (nonstd loc, implied) */ // FIXME: bugfix NOP absolute!
	case 0xdd:	_CMP(CPU65.a, readByte(_absx())); break; /* 0xdd CMP Absolute,X */
	case 0xde:	{ int addr = _absx(); Uint8 data = readByte(addr) - 1; setNZ(data); writeByte(addr, data); } break; /* 0xde DEC Absolute,X */
	case 0xdf:	_BRA( readByte(_zp()) & 32 ); break; /* 0xdf BBS Relative */
	case 0xe0:	_CMP(CPU65.x, readByte(_imm())); break; /* 0xe0 CPX Immediate */
	case 0xe1:	_SBC(readByte(_zpxi())); break; /* 0xe1 SBC (Zero_Page,X) */
	case 0xe2:
#ifdef CPU_65CE02
			OPC_65CE02("LDA (nn,S),Y");
			// 65CE02 LDA ($nn,SP),Y
			// REALLY IMPORTANT: please read the comment at _GET_SP_INDIRECT_ADDR()!
			setNZ(CPU65.a = readByte(_GET_SP_INDIRECT_ADDR()));
			//DEBUG("CPU: LDA (nn,S),Y returned: A = $%02X, P before last IRQ was: $%02X" NL, CPU65.a, last_p);
#else
			CPU65.pc++; // 0xe2 NOP imm (non-std NOP with addr mode)
#endif
			break;
	case 0xe3:
#ifdef CPU_65CE02
			OPC_65CE02("INW nn");
			{	//  INW $nn            E3  Increment Word (maybe an error in 64NET.OPC ...) ANOTHER FIX: this is zero (errr, base ...) page!!!
			int alo = _zp();
			int ahi = (alo & 0xFF00) | ((alo + 1) & 0xFF);
			Uint16 data = (readByte(alo) | (readByte(ahi) << 8)) + 1;
			setNZ16(data);
			//cpu_pfz = (data == 0);
			writeByte(alo, data & 0xFF);
			writeByte(ahi, data >> 8);
			}
#endif
			break; /* 0xe3 NOP (nonstd loc, implied) */
	case 0xe4:	_CMP(CPU65.x, readByte(_zp())); break; /* 0xe4 CPX Zero_Page */
	case 0xe5:	_SBC(readByte(_zp())); break; /* 0xe5 SBC Zero_Page */
	case 0xe6:	{ int addr = _zp(); Uint8 data = readByte(addr) + 1; setNZ(data); writeByte(addr, data); } break; /* 0xe6 INC Zero_Page */
	case 0xe7:	{ int a = _zp(); writeByte(a, readByte(a) | 64); } break; /* 0xe7 SMB Zero_Page */
	case 0xe8:	setNZ(++CPU65.x); break; /* 0xe8 INX Implied */
	case 0xe9:	_SBC(readByte(_imm())); break; /* 0xe9 SBC Immediate */
	case 0xea:
#ifdef CPU_65CE02
			// on 65CE02 it's not special, but in C65 (4510) it is (EOM). It's up the emulator though (in the the second case) ...
			OPC_65CE02("EOM");
			cpu65_do_nop_callback();
#endif
			break;	// 0xea NOP Implied - the "standard" NOP of original 6502 core
	case 0xeb:
#ifdef CPU_65CE02
			OPC_65CE02("ROW nnnn");			// ROW $nnnn		EB  Rotate word LEFT?! [other documents says RIGHT!!!]
			{
			int addr = _abs();
			int data = ((readByte(addr) | (readByte(addr + 1) << 8)) << 1) | (CPU65.pf_c ? 1 : 0);
			CPU65.pf_c = data & 0x10000;
			data &= 0xFFFF;
			setNZ16(data);
			writeByte(addr, data & 0xFF);
			writeByte(addr + 1, data >> 8);
			}
#endif
			break; /* 0xeb NOP (nonstd loc, implied) */
	case 0xec:	_CMP(CPU65.x, readByte(_abs())); break; /* 0xec CPX Absolute */
	case 0xed:	_SBC(readByte(_abs())); break; /* 0xed SBC Absolute */
	case 0xee:	{ int addr = _abs(); Uint8 data = readByte(addr) + 1; setNZ(data); writeByte(addr, data); } break; /* 0xee INC Absolute */
	case 0xef:	_BRA( readByte(_zp()) & 64 ); break; /* 0xef BBS Relative */
	case 0xf0:
#ifdef CPU65_DISCRETE_PF_NZ
			_BRA(CPU65.pf_z);
#else
			_BRA(CPU65.pf_nz & PF_Z);
#endif
			break; /* 0xf0 BEQ Relative */
	case 0xf1:	_SBC(readByte(_zpiy())); break; /* 0xf1 SBC (Zero_Page),Y */
	case 0xf2:	/* 0xf2 SBC (Zero_Page) or (ZP),Z on 65CE02 */
#ifdef MEGA65
			if (IS_FLAT32_DATA_OP())
				_SBC(readFlatAddressedByte());
			else
#endif
				_SBC(readByte(_zpi()));
			break;
	case 0xf3:
#ifdef CPU_65CE02
			OPC_65CE02("BEQ16");
#ifdef CPU65_DISCRETE_PF_NZ
			_BRA16(CPU65.pf_z);
#else
			_BRA16(CPU65.pf_nz & PF_Z);
#endif
			// 65CE02 BEQ $nnnn
#endif
			break; /* 0xf3 NOP (nonstd loc, implied) */
	case 0xf4:
#ifdef CPU_65CE02
			OPC_65CE02("PHW #nnnn");
			PUSH_FOR_PHW(readWord(CPU65.pc));		// 65CE02 PHW #$nnnn (push word)
			CPU65.pc += 2;
#else
			CPU65.pc++; // 0xf4 NOP zpx (non-std NOP with addr mode)
#endif
			break;
	case 0xf5:	_SBC(readByte(_zpx())); break; /* 0xf5 SBC Zero_Page,X */
	case 0xf6:	{ int addr = _zpx(); Uint8 data = readByte(addr) + 1; setNZ(data); writeByte(addr, data); } break; /* 0xf6 INC Zero_Page,X */
	case 0xf7:	{ int a = _zp(); writeByte(a, readByte(a) | 128); } break; /* 0xf7 SMB Zero_Page */
	case 0xf8:	CPU65.pf_d = 1; break; /* 0xf8 SED Implied */
	case 0xf9:	_SBC(readByte(_absy())); break; /* 0xf9 SBC Absolute,Y */
	case 0xfa:	setNZ(CPU65.x = pop()); break; /* 0xfa PLX Implied */
	case 0xfb:
#ifdef CPU_65CE02
			OPC_65CE02("PLZ");
			setNZ(CPU65.z = pop());	// 65CE02 PLZ
#endif
			break; /* 0xfb NOP (nonstd loc, implied) */
	case 0xfc:
#ifdef CPU_65CE02
			OPC_65CE02("PHW nnnn");
			PUSH_FOR_PHW(readWord(readWord(CPU65.pc)));	// PHW $nnnn [? push word from an absolute address, maybe?] Note: C65 BASIC depends on this opcode to be correct!
			CPU65.pc += 2;
#else
			CPU65.pc += 2;
#endif
			break; /* 0xfc NOP (nonstd loc, implied) */ // FIXME: bugfix NOP absolute?
	case 0xfd:	_SBC(readByte(_absx())); break; /* 0xfd SBC Absolute,X */
	case 0xfe:	{ int addr = _absx(); Uint8 data = readByte(addr) + 1; setNZ(data); writeByte(addr, data); } break; /* 0xfe INC Absolute,X */
	case 0xff:	_BRA( readByte(_zp()) & 128 ); break; /* 0xff BBS Relative */
#ifdef DEBUG_CPU
	default:
			FATAL("FATAL: not handled CPU opcode: $%02X", CPU65.op);
			break;
#endif
	}
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
*/


#ifdef XEMU_SNAPSHOT_SUPPORT

#include "xemu/emutools_snapshot.h"
#include <string.h>

#define SNAPSHOT_CPU_BLOCK_VERSION	0
#define SNAPSHOT_CPU_BLOCK_SIZE		256

#ifdef CPU_65CE02
#define SNAPSHOT_CPU_ID			2
#else
#define SNAPSHOT_CPU_ID			1
#endif

int cpu65_snapshot_load_state ( const struct xemu_snapshot_definition_st *def, struct xemu_snapshot_block_st *block )
{
	int ret;
	Uint8 buffer[SNAPSHOT_CPU_BLOCK_SIZE];
	if (block->sub_counter || block->block_version != SNAPSHOT_CPU_BLOCK_VERSION || block->sub_size != sizeof buffer)
		RETURN_XSNAPERR_USER("Bad CPU 65xx block syntax");
	ret = xemusnap_read_file(buffer, sizeof buffer);
	if (ret) return ret;
	if (buffer[0] != SNAPSHOT_CPU_ID)
		RETURN_XSNAPERR_USER("CPU type mismatch");
	CPU65.pc = P_AS_BE16(buffer + 1);
	CPU65.a = buffer[3];
	CPU65.x = buffer[4];
	CPU65.y = buffer[5];
	CPU65.s = buffer[6];
	cpu65_set_pf(buffer[7]);
	CPU65.pf_e = buffer[7] & 32;	// must be set manually ....
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
	Uint8 buffer[SNAPSHOT_CPU_BLOCK_SIZE];
	int ret = xemusnap_write_block_header(def->idstr, SNAPSHOT_CPU_BLOCK_VERSION);
	if (ret) return ret;
	memset(buffer, 0xFF, sizeof buffer);
	buffer[0] = SNAPSHOT_CPU_ID;
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
