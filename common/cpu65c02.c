/* Xemu - Somewhat lame emulation (running on Linux/Unix/Windows/OSX, utilizing
   SDL2) of some 8 bit machines, including the Commodore LCD and Commodore 65
   and some Mega-65 features as well.
   Copyright (C)2016 LGB (Gábor Lénárt) <lgblgblgb@gmail.com>

   THIS IS AN UGLY PIECE OF SOURCE REALLY.

   Quite confusing comment section even at the beginning, from this point ...

   | This file tries to implement a 65C02 CPU, also with the ability (unused here) for
   | *some* kind of DTV CPU hacks (though incorrect as other opcodes would be emulated
   | illegal 6502 opcodes, not 65C02). Also, there is an on-going work to be able to
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

#include "emutools_basicdefs.h"
#ifndef CPU_CUSTOM_INCLUDED
#include "cpu65c02.h"
#endif

#ifdef DEBUG_CPU
#include "cpu65ce02_disasm_tables.c"
//#include "cpu65ce02_disasm.c
#endif

#ifdef DTV_CPU_HACK
#ifdef CPU_65CE02
#	error "DTV_CPU_HACK and CPU_65CE02 are both defined. This is illegal currently."
#endif
#	error "DTV_CPU_HACK is not ready/usable yet!"
	Uint8 cpu_regs[16];
	int cpu_a_sind, cpu_a_tind, cpu_x_ind, cpu_y_ind;
#	define SP_HI (cpu_regs[11]<<8)
#	define ZP_HI (cpu_regs[10]<<8)
#	define CPU_A_INC(n) cpu_regs[cpu_a_tind] = ((Uint8)cpu_regs[cpu_a_sind] + n)
#	define CPU_A_GET() cpu_regs[cpu_a_sind]
#	define CPU_A_SET(d) cpu_regs[cpu_a_tind]=d
#	define cpu_x cpu_regs[cpu_x_ind]
#	define cpu_y cpu_regs[cpu_y_ind]
#	define CPU_TYPE "65C02+DTV"
#	warning "Incomplete, inaccurate partial emulation of C64-DTV CPU, just the three extra opcodes, but 65C02 otherwise (with the original timings, no burst/skip cycle modes/etc)!"
#	define A_OP(op,dat) cpu_regs[cpu_a_tind] = cpu_regs[cpu_a_sind] op dat
#else
	Uint8 cpu_a, cpu_x, cpu_y;
#ifdef CPU_65CE02
	//int cpu_last_opcode_cycles;
	Uint8 cpu_z;
	Uint16 cpu_bphi;	// NOTE: it must store the value shifted to the high byte!
	Uint16 cpu_sphi;	// NOTE: it must store the value shifted to the high byte!
	int cpu_inhibit_interrupts;
#define	SP_HI cpu_sphi
#define	ZP_HI cpu_bphi
#define	ZERO_REG	cpu_z
	int cpu_pfe;
#	define CPU_TYPE "65CE02"
#else
#	define SP_HI	0x100
#	define ZP_HI	0
#	define CPU_TYPE "65C02"
#	define ZERO_REG	0
#endif

#	define CPU_A_INC(n) cpu_a = ((Uint8)cpu_a + n)
#	define CPU_A_GET()  cpu_a
#	define CPU_A_SET(d) cpu_a=d
#	define A_OP(op,dat) cpu_a = cpu_a op dat
#endif


Uint8 cpu_sp, cpu_op;
#ifdef MEGA65
#warning "Compiling for MEGA65, hacky stuff!"
#define IS_FLAT32_DATA_OP() unlikely(cpu_previous_op == 0xEA && cpu_linear_memory_addressing_is_enabled)
Uint8 cpu_previous_op;
#endif
Uint16 cpu_pc, cpu_old_pc;
int cpu_pfn,cpu_pfv,cpu_pfb,cpu_pfd,cpu_pfi,cpu_pfz,cpu_pfc;
int cpu_irqLevel = 0, cpu_nmiEdge = 0;
int cpu_cycles;

#ifdef CPU_65CE02
#ifdef DEBUG_CPU
#define OPC_65CE02(w) DEBUG("CPU: 65CE02 opcode: %s" NL, w)
#else
#define OPC_65CE02(w)
#endif
#if 0
static inline void UNIMPLEMENTED_65CE02 ( const char *msg )
{
	fprintf(stderr, "UNIMPLEMENTED 65CE02 opcode $%02X [$%02X $%02X] at $%04X: \"%s\"" NL,
		cpu_op, cpu_read(cpu_pc), cpu_read(cpu_pc + 1), (cpu_pc - 1) & 0xFFFF, msg
	);
	exit(1);
}
#endif
static const Uint8 opcycles[] = {7,5,2,2,4,3,4,4,3,2,1,1,5,4,5,4,2,5,5,3,4,3,4,4,1,4,1,1,5,4,5,4,5,5,7,7,3,3,4,4,3,2,1,1,4,4,5,4,2,5,5,3,3,3,4,4,1,4,1,1,4,4,5,4,5,5,2,2,4,3,4,4,3,2,1,1,3,4,5,4,2,5,5,3,4,3,4,4,2,4,3,1,4,4,5,4,4,5,7,5,3,3,4,4,3,2,1,1,5,4,5,4,2,5,5,3,3,3,4,4,2,4,3,1,5,4,5,4,2,5,6,3,3,3,3,4,1,2,1,4,4,4,4,4,2,5,5,3,3,3,3,4,1,4,1,4,4,4,4,4,2,5,2,2,3,3,3,4,1,2,1,4,4,4,4,4,2,5,5,3,3,3,3,4,1,4,1,4,4,4,4,4,2,5,2,6,3,3,4,4,1,2,1,7,4,4,5,4,2,5,5,3,3,3,4,4,1,4,3,3,4,4,5,4,2,5,6,6,3,3,4,4,1,2,1,7,4,4,5,4,2,5,5,3,5,3,4,4,1,4,3,3,7,4,5,4};
#else
static const Uint8 opcycles[] = {7,6,2,2,5,3,5,5,3,2,2,2,6,4,6,2,2,5,5,2,5,4,6,5,2,4,2,2,6,4,7,2,6,6,2,2,3,3,5,5,4,2,2,2,4,4,6,2,2,5,5,2,4,4,6,5,2,4,2,2,4,4,7,2,6,6,2,2,3,3,5,5,3,2,2,2,3,4,6,2,2,5,5,2,4,4,6,5,2,4,3,2,2,4,7,2,6,6,2,2,3,3,5,5,4,2,2,2,5,4,6,2,2,5,5,2,4,4,6,5,2,4,4,2,6,4,7,2,3,6,2,2,3,3,3,5,2,2,2,2,4,4,4,2,2,6,5,2,4,4,4,5,2,5,2,2,4,5,5,2,2,6,2,2,3,3,3,5,2,2,2,2,4,4,4,2,2,5,5,2,4,4,4,5,2,4,2,2,4,4,4,2,2,6,2,2,3,3,5,5,2,2,2,2,4,4,6,2,2,5,5,2,4,4,6,5,2,4,3,2,2,4,7,2,2,6,2,2,3,3,5,5,2,2,2,2,4,4,6,2,2,5,5,2,4,4,6,5,2,4,4,2,2,4,7,2};
#endif

static inline Uint16 readWord(Uint16 addr) {
	return cpu_read(addr) | (cpu_read(addr + 1) << 8);
}

#ifdef CPU_65CE02
/* The stack pointer is a 16 bit register that has two modes. It can be programmed to be either an 8-bit page Programmable pointer, or a full 16-bit pointer.
   The processor status E bit selects which mode will be used. When set, the E bit selects the 8-bit mode. When reset, the E bit selects the 16-bit mode. */

static inline void push ( Uint8 data )
{
	cpu_write(cpu_sp | cpu_sphi, data);
	cpu_sp--;
	if (cpu_sp == 0xFF && (!cpu_pfe)) {
		cpu_sphi -= 0x100;
#ifdef DEBUG_CPU
		DEBUG("CPU: 65CE02: SPHI changed to $%04X" NL, cpu_sphi);
#endif
	}
}
static inline Uint8 pop ( void )
{
	cpu_sp++;
	if (cpu_sp == 0 && (!cpu_pfe)) {
		cpu_sphi += 0x100;
#ifdef DEBUG_CPU
		DEBUG("CPU: 65CE02: SPHI changed to $%04X" NL, cpu_sphi);
#endif
	}
	return cpu_read(cpu_sp | cpu_sphi);
}
#else
#define push(data) cpu_write(((Uint8)(cpu_sp--)) | SP_HI, data)
#define pop() cpu_read(((Uint8)(++cpu_sp)) | SP_HI)
#endif

static inline void  pushWord(Uint16 data) { push(data >> 8); push(data & 0xFF); }
static inline Uint16 popWord() { Uint16 temp = pop(); return temp | (pop() << 8); }


#ifdef CPU_65CE02
// FIXME: remove this, if we don't need!
// NOTE!! Interesting, it seems PHW opcodes pushes the word the OPPOSITE direction as eg JSR would push the PC ...
#define PUSH_FOR_PHW pushWord_rev
static inline void  pushWord_rev(Uint16 data) { push(data & 0xFF); push(data >> 8); }
#endif


void cpu_set_p(Uint8 st) {
	cpu_pfn = st & 128;
	cpu_pfv = st &  64;
#ifdef CPU_65CE02
	// Note: E bit cannot be changed by PLP/RTI, so it's commented out here ...
	// At least *I* think :) FIXME?
	// cpu_pfe = st &  32;
#endif
	cpu_pfb = st &  16;
	cpu_pfd = st &   8;
	cpu_pfi = st &   4;
	cpu_pfz = st &   2;
	cpu_pfc = st &   1;
}

Uint8 cpu_get_p() {
	return  (cpu_pfn ? 128 : 0) |
	(cpu_pfv ?  64 : 0) |
#ifdef CPU_65CE02
	(cpu_pfe ?  32 : 0) |
#else
	32 |
#endif
	(cpu_pfb ?  16 : 0) |
	(cpu_pfd ?   8 : 0) |
	(cpu_pfi ?   4 : 0) |
	(cpu_pfz ?   2 : 0) |
	(cpu_pfc ?   1 : 0);
}

void cpu_reset() {
	cpu_set_p(0x34);
	cpu_sp = 0xFF;
	cpu_irqLevel = cpu_nmiEdge = 0;
	cpu_cycles = 0;
#ifdef DTV_CPU_HACK
	cpu_a_sind = 0;
	cpu_a_tind = 0;
	cpu_y_ind = 1;
	cpu_x_ind = 2;
	memset(cpu_regs, 0, 16);
	cpu_regs[11] = 1; // set page#1 for stack by default
#else
	CPU_A_SET(0);
	cpu_x = 0;
	cpu_y = 0;
#ifdef CPU_65CE02
	cpu_pfe = 1;
	cpu_z = 0;
	cpu_bphi = 0x0000;
	cpu_sphi = 0x0100;
	cpu_inhibit_interrupts = 0;
#endif
#endif
	cpu_pc = readWord(0xFFFC);
	DEBUG("CPU[" CPU_TYPE "]: RESET, PC=%04X" NL, cpu_pc);
}


static inline void setNZ(Uint8 st) {
	cpu_pfn = st & 128;
	cpu_pfz = !st;
}
#ifdef CPU_65CE02
static inline void setNZ16(Uint16 st) {
	cpu_pfn = st & 0x8000;
	cpu_pfz = !st;
}
#endif

#define _imm() (cpu_pc++)
static inline Uint16 _abs() {
	Uint16 o = cpu_read(cpu_pc++);
	return o | (cpu_read(cpu_pc++) << 8);
}
#define _absx() ((Uint16)(_abs() + cpu_x))
#define _absy() ((Uint16)(_abs() + cpu_y))
#define _absi() readWord(_abs())
#define _absxi() readWord(_absx())
#define _zp() (cpu_read(cpu_pc++) | ZP_HI)

static inline Uint16 _zpi() {
	Uint8 a = cpu_read(cpu_pc++);
#ifdef CPU_65CE02
	return (cpu_read(a | ZP_HI) | (cpu_read(((a + 1) & 0xFF) | ZP_HI) << 8)) + cpu_z;
#else
	return  cpu_read(a | ZP_HI) | (cpu_read(((a + 1) & 0xFF) | ZP_HI) << 8);
#endif
}

static inline Uint16 _zpiy() {
	Uint8 a = cpu_read(cpu_pc++);
	return (cpu_read(a | ZP_HI) | (cpu_read(((a + 1) & 0xFF) | ZP_HI) << 8)) + cpu_y;
}


#define _zpx() (((cpu_read(cpu_pc++) + cpu_x) & 0xFF) | ZP_HI)
#define _zpy() (((cpu_read(cpu_pc++) + cpu_y) & 0xFF) | ZP_HI)

static inline Uint16 _zpxi() {
	Uint8 a = cpu_read(cpu_pc++) + cpu_x;
	return cpu_read(a | ZP_HI) | (cpu_read(((a + 1) & 0xFF) | ZP_HI) << 8);
}

static inline void _BRA(int cond) {
	 if (cond) {
		int temp = cpu_read(cpu_pc);
		if (temp & 128) temp = cpu_pc - (temp ^ 0xFF);
		else temp = cpu_pc + temp + 1;
		if ((temp & 0xFF00) != (cpu_pc & 0xFF00)) cpu_cycles++;
		cpu_pc = temp;
		cpu_cycles++;
	} else
		cpu_pc++;
}
#ifdef CPU_65CE02
static inline void _BRA16(int cond) {
	if (cond) {
		// Note: 16 bit PC relative stuffs works a bit differently as 8 bit ones, not the same base of the offsets!
#if 0
		int temp = cpu_read(cpu_pc) | (cpu_read(cpu_pc + 1) << 8);
		//if (temp & 0x8000) temp = 1 + cpu_pc - (temp ^ 0xFFFF);
		//else temp = cpu_pc + temp + 2;
		if (temp & 0x8000) temp = 1 + cpu_pc - (temp ^ 0xFFFF);
		else temp = cpu_pc + temp + 2;
#endif
		cpu_pc += 1 + (Sint16)(cpu_read(cpu_pc) | (cpu_read(cpu_pc + 1) << 8));

		//if ((temp & 0xFF00) != (cpu_pc & 0xFF00)) cpu_cycles++; // FIXME: sill applies in 16 bit relative mode as well?!
		//cpu_pc = temp;
		cpu_cycles++;
	} else
		cpu_pc += 2;
}
// Used by LDA/STA (nn,SP), Y opcodes
/* Big fat NOTE/FIXME/TODO:
   See the question #1/2, even two places where stack 'warping around' effect can
   be an interesting question in 8 bit stack mode.
*/
static inline Uint16 _GET_SP_INDIRECT_ADDR ( void )
{
	int tmp2;
	int tmp = cpu_sp + cpu_read(cpu_pc++);
	if (cpu_pfe)		// FIXME: question #1: is E flag affects this addressing mode this way
		tmp &= 0xFF;
	tmp2 = cpu_read((cpu_sphi + tmp) & 0xFFFF);
	tmp++;
	if (cpu_pfe)		// FIXME: question #2: what happens if lo/hi bytes would be used at exactly at 'wrapping the stack' around case, with 8 bit stack mode?
		tmp &= 0xFF;
	tmp2 |= cpu_read((cpu_sphi + tmp) & 0xFFFF) << 8;
	return (Uint16)(tmp2 + cpu_y);
#if 0
	// Older, bad implementation, by misunderstanding the stuff badly:
	Uint16 res = cpu_read(cpu_pc++) + cpu_y + cpu_sp + 1;
	// Guessing: if stack is in 8 bit mode, it warps around inside the given stack page
	if (cpu_pfe)
		res &= 0xFF; 
	return (Uint16)(res + cpu_sphi);
#endif
}
#endif
static inline void _CMP(Uint8 reg, Uint8 data) {
	Uint16 temp = reg - data;
	cpu_pfc = temp < 0x100;
	setNZ(temp);
}
static inline void _TSB(int addr) {
	Uint8 m = cpu_read(addr);
	cpu_pfz = (!(m & CPU_A_GET()));
	cpu_write(addr, m | CPU_A_GET());
}
static inline void _TRB(int addr) {
	Uint8 m = cpu_read(addr);
	cpu_pfz = (!(m & CPU_A_GET()));
	cpu_write(addr, m & (255 - CPU_A_GET()));
}
static inline void _ASL(int addr) {
	Uint8 t = (addr == -1 ? CPU_A_GET() : cpu_read(addr));
	Uint8 o = t;
	cpu_pfc = t & 128;
	//t = (t << 1) & 0xFF;
	t <<= 1;
	setNZ(t);
	if (addr == -1) CPU_A_SET(t); else cpu_write_rmw(addr, o, t);
}
static inline void _LSR(int addr) {
	Uint8 t = (addr == -1 ? CPU_A_GET() : cpu_read(addr));
	Uint8 o = t;
	cpu_pfc = t & 1;
	//t = (t >> 1) & 0xFF;
	t >>= 1;
	setNZ(t);
	if (addr == -1) CPU_A_SET(t); else cpu_write_rmw(addr, o, t);
}
#ifdef CPU_65CE02
static inline void _ASR(int addr) {
	Uint8 t = (addr == -1 ? CPU_A_GET() : cpu_read(addr));
	Uint8 o = t;
	cpu_pfc = t & 1;
	t = (t >> 1) | (t & 0x80);
	setNZ(t);
	if (addr == -1) CPU_A_SET(t); else cpu_write_rmw(addr, o, t);
}
#endif
static inline void _BIT(Uint8 data) {
	cpu_pfn = data & 128;
	cpu_pfv = data & 64;
	cpu_pfz = (!(CPU_A_GET() & data));
}
static inline void _ADC(int data) {
	if (cpu_pfd) {
		Uint16 temp  = (CPU_A_GET() & 0x0F) + (data & 0x0F) + (cpu_pfc ? 1 : 0);
		Uint16 temp2 = (CPU_A_GET() & 0xF0) + (data & 0xF0);
		if (temp > 9) { temp2 += 0x10; temp += 6; }
		cpu_pfv = (~(CPU_A_GET() ^ data) & (CPU_A_GET() ^ temp) & 0x80);
		if (temp2 > 0x90) temp2 += 0x60;
		cpu_pfc = (temp2 & 0xFF00);
		CPU_A_SET((temp & 0x0F) + (temp2 & 0xF0));
		setNZ(CPU_A_GET());
	} else {
		Uint16 temp = data + CPU_A_GET() + (cpu_pfc ? 1 : 0);
		cpu_pfc = temp > 0xFF;
		cpu_pfv = (!((CPU_A_GET() ^ data) & 0x80) && ((CPU_A_GET() ^ temp) & 0x80));
		CPU_A_SET(temp & 0xFF);
		setNZ(CPU_A_GET());
	}
}
static inline void _SBC(int data) {
	if (cpu_pfd) {
		Uint16 temp = CPU_A_GET() - (data & 0x0F) - (cpu_pfc ? 0 : 1);
		if ((temp & 0x0F) > (CPU_A_GET() & 0x0F)) temp -= 6;
		temp -= (data & 0xF0);
		if ((temp & 0xF0) > (CPU_A_GET() & 0xF0)) temp -= 0x60;
		cpu_pfv = (!(temp > CPU_A_GET()));
		cpu_pfc = (!(temp > CPU_A_GET()));
		CPU_A_SET(temp & 0xFF);
		setNZ(CPU_A_GET());
	} else {
		Uint16 temp = CPU_A_GET() - data - (cpu_pfc ? 0 : 1);
		cpu_pfc = temp < 0x100;
		cpu_pfv = ((CPU_A_GET() ^ temp) & 0x80) && ((CPU_A_GET() ^ data) & 0x80);
		CPU_A_SET(temp & 0xFF);
		setNZ(CPU_A_GET());
	}
}
static inline void _ROR(int addr) {
	Uint16 t = ((addr == -1) ? CPU_A_GET() : cpu_read(addr));
	Uint8  o = t;
	if (cpu_pfc) t |= 0x100;
	cpu_pfc = t & 1;
	t >>= 1;
	setNZ(t);
	if (addr == -1) CPU_A_SET(t); else cpu_write_rmw(addr, o, t);
}
static inline void _ROL(int addr) {
	Uint16 t = ((addr == -1) ? CPU_A_GET() : cpu_read(addr));
	Uint8  o = t;
	t = (t << 1) | (cpu_pfc ? 1 : 0);
	cpu_pfc = t & 0x100;
	t &= 0xFF;
	setNZ(t);
	if (addr == -1) CPU_A_SET(t); else cpu_write_rmw(addr, o, t);
}


static Uint8 last_p;


int cpu_step () {
	if (cpu_nmiEdge
#ifdef CPU_65CE02
		&& cpu_cycles != 1 && !cpu_inhibit_interrupts
#endif
	) {
#ifdef DEBUG_CPU
		DEBUG("CPU: serving NMI on NMI edge at PC $%04X" NL, cpu_pc);
#endif
		cpu_nmiEdge = 0;
		pushWord(cpu_pc);
		push(cpu_get_p() & (255 - 0x10));
		cpu_pfi = 1;
		cpu_pfd = 0;			// NOTE: D flag clearing was not done on the original 6502 I guess, but indeed on the 65C02 already
		cpu_pc = readWord(0xFFFA);
		return 7;
	}
	if (cpu_irqLevel && (!cpu_pfi)
#ifdef CPU_65CE02
		&& cpu_cycles != 1 && !cpu_inhibit_interrupts
#endif
	) {
#ifdef DEBUG_CPU
		DEBUG("CPU: servint IRQ on IRQ level at PC $%04X" NL, cpu_pc);
#endif
		last_p = cpu_get_p();
		pushWord(cpu_pc);
		cpu_pfb = 0;
		push(cpu_get_p() & (255 - 0x10));
		cpu_pfi = 1;
		cpu_pfd = 0;
		cpu_pc = readWord(0xFFFE);
		return 7;
	}
	cpu_old_pc = cpu_pc;
#ifdef DEBUG_CPU
	if (cpu_pc == 0)
		DEBUG("CPU: WARN: PC at zero!" NL);
#endif
#ifdef MEGA65
	cpu_previous_op = cpu_op;
#endif
	cpu_op = cpu_read(cpu_pc++);
#ifdef DEBUG_CPU
	DEBUG("CPU: at $%04X opcode = $%02X %s %s A=%02X X=%02X Y=%02X Z=%02X SP=%02X" NL, (cpu_pc - 1) & 0xFFFF, cpu_op, opcode_names[cpu_op], opcode_adm_names[opcode_adms[cpu_op]],
		cpu_a, cpu_x, cpu_y, cpu_z, cpu_sp
	);
	if (cpu_op == 0x60)
		DEBUG("CPU: SP before RTS is (SPHI=$%04X) SP=$%02X" NL, cpu_sphi, cpu_sp);
#endif
#ifdef CPU_TRAP
	if (cpu_op == CPU_TRAP) {
		int ret = cpu_trap(CPU_TRAP);
		if (ret > 0)
			return ret;
	}
#endif
	cpu_cycles = opcycles[cpu_op];
	switch (cpu_op) {
	case 0x00:
#ifdef DEBUG_CPU
			DEBUG("CPU: WARN: BRK is about executing at PC=$%04X" NL, (cpu_pc - 1) & 0xFFFF);
#endif
			// FIXME: does BRK sets I and D flag? Hmm, I can't even remember now why I wrote these :-D
			// FIXME-2: does BRK sets B flag, or only in the saved copy on the stack??
			// NOTE: D flag clearing was not done on the original 6502 I guess, but indeed on the 65C02 already
			pushWord(cpu_pc + 1); push(cpu_get_p() | 0x10); cpu_pfd = 0; cpu_pfi = 1; cpu_pc = readWord(0xFFFE); /* 0x0 BRK Implied */
			break;
	case 0x01:	setNZ(A_OP(|,cpu_read(_zpxi()))); break; /* 0x1 ORA (Zero_Page,X) */
	case 0x02:
#ifdef CPU_65CE02
			OPC_65CE02("CLE");
			cpu_pfe = 0;	// 65CE02: CLE
#ifdef DEBUG_CPU
			DEBUG("CPU: WARN: E flag is cleared!" NL);
#endif
#else
			cpu_pc++; /* 0x2 NOP imm (non-std NOP with addr mode) */
#endif
			break;
	case 0x03:
#ifdef CPU_65CE02
			OPC_65CE02("SEE");
			cpu_pfe = 1;	// 65CE02: SEE
#endif
			break; /* 0x3 NOP (nonstd loc, implied) */
	case 0x04:	_TSB(_zp()); break; /* 0x4 TSB Zero_Page */
	case 0x05:	setNZ(A_OP(|,cpu_read(_zp()))); break; /* 0x5 ORA Zero_Page */
	case 0x06:	_ASL(_zp()); break; /* 0x6 ASL Zero_Page */
	case 0x07:	{ int a = _zp(); cpu_write(a, cpu_read(a) & 254);  } break; /* 0x7 RMB Zero_Page */
	case 0x08:	push(cpu_get_p() | 0x10); break; /* 0x8 PHP Implied */
	case 0x09:	setNZ(A_OP(|,cpu_read(_imm()))); break; /* 0x9 ORA Immediate */
	case 0x0a:	_ASL(-1); break; /* 0xa ASL Accumulator */
	case 0x0b:
#ifdef CPU_65CE02
			OPC_65CE02("TSY");
			setNZ(cpu_y = (cpu_sphi >> 8));   // TSY                  0B   65CE02
#endif
			break; /* 0xb NOP (nonstd loc, implied) */
	case 0x0c:	_TSB(_abs()); break; /* 0xc TSB Absolute */
	case 0x0d:	setNZ(A_OP(|,cpu_read(_abs()))); break; /* 0xd ORA Absolute */
	case 0x0e:	_ASL(_abs()); break; /* 0xe ASL Absolute */
	case 0x0f:	_BRA(!(cpu_read(_zp()) & 1)); break; /* 0xf BBR Relative */
	case 0x10:	_BRA(!cpu_pfn); break; /* 0x10 BPL Relative */
	case 0x11:	setNZ(A_OP(|,cpu_read(_zpiy()))); break; /* 0x11 ORA (Zero_Page),Y */
	case 0x12:
#ifdef DTV_CPU_HACK
			_BRA(1);		/* 0x12: DTV specific BRA */
#else
			/* 0x12 ORA (Zero_Page) or (ZP),Z on 65CE02 */
#ifdef MEGA65
			if (IS_FLAT32_DATA_OP())
				setNZ(A_OP(|,cpu_read_linear_opcode()));
			else
#endif
				setNZ(A_OP(|,cpu_read(_zpi())));
#endif
			break;
	case 0x13:
#ifdef CPU_65CE02
			OPC_65CE02("BPL16");
			_BRA16(!cpu_pfn);		// 65CE02: BPL 16 bit relative
#endif
			break; /* 0x13 NOP (nonstd loc, implied) */
	case 0x14:	_TRB(_zp()); break; /* 0x14 TRB Zero_Page */
	case 0x15:	setNZ(A_OP(|,cpu_read(_zpx()))); break; /* 0x15 ORA Zero_Page,X */
	case 0x16:	_ASL(_zpx()); break; /* 0x16 ASL Zero_Page,X */
	case 0x17:	{ int a = _zp(); cpu_write(a, cpu_read(a) & 253); } break; /* 0x17 RMB Zero_Page */
	case 0x18:	cpu_pfc = 0; break; /* 0x18 CLC Implied */
	case 0x19:	setNZ(A_OP(|,cpu_read(_absy()))); break; /* 0x19 ORA Absolute,Y */
	case 0x1a:	setNZ(CPU_A_INC(1)); break; /* 0x1a INA Accumulator */
	case 0x1b:
#ifdef CPU_65CE02
			OPC_65CE02("INZ");
			setNZ(++cpu_z);	// 65CE02: INZ
#endif
			break; /* 0x1b NOP (nonstd loc, implied) */
	case 0x1c:	_TRB(_abs()); break; /* 0x1c TRB Absolute */
	case 0x1d:	setNZ(A_OP(|,cpu_read(_absx()))); break; /* 0x1d ORA Absolute,X */
	case 0x1e:	_ASL(_absx()); break; /* 0x1e ASL Absolute,X */
	case 0x1f:	_BRA(!(cpu_read(_zp()) & 2)); break; /* 0x1f BBR Relative */
	case 0x20:	pushWord(cpu_pc + 1); cpu_pc = _abs(); break; /* 0x20 JSR Absolute */
	case 0x21:	setNZ(A_OP(&,cpu_read(_zpxi()))); break; /* 0x21 AND (Zero_Page,X) */
	case 0x22:
#ifdef CPU_65CE02
			OPC_65CE02("JSR (nnnn)");
			// 65CE02 JSR ($nnnn)
			pushWord(cpu_pc + 1);
			cpu_pc = _absi();
#else
			cpu_pc++;	/* 0x22 NOP imm (non-std NOP with addr mode) */
#endif
			break;
	case 0x23:
#ifdef CPU_65CE02
			OPC_65CE02("JSR (nnnn,X)");
			// 65CE02 JSR ($nnnn,X)
			pushWord(cpu_pc + 1);
			cpu_pc = _absxi();
#endif
			break; /* 0x23 NOP (nonstd loc, implied) */
	case 0x24:	_BIT(cpu_read(_zp())); break; /* 0x24 BIT Zero_Page */
	case 0x25:	setNZ(A_OP(&,cpu_read(_zp()))); break; /* 0x25 AND Zero_Page */
	case 0x26:	_ROL(_zp()); break; /* 0x26 ROL Zero_Page */
	case 0x27:	{ int a = _zp(); cpu_write(a, cpu_read(a) & 251); } break; /* 0x27 RMB Zero_Page */
	case 0x28:
			cpu_set_p(pop() | 0x10);
			break; /* 0x28 PLP Implied */
	case 0x29:	setNZ(A_OP(&,cpu_read(_imm()))); break; /* 0x29 AND Immediate */
	case 0x2a:	_ROL(-1); break; /* 0x2a ROL Accumulator */
	case 0x2b:
#ifdef CPU_65CE02
			OPC_65CE02("TYS");
			cpu_sphi = cpu_y << 8;	// 65CE02	TYS
#ifdef DEBUG_CPU
			if (cpu_sphi != 0x100)
				DEBUG("CPU: WARN: stack page is set non-0x100: $%04X" NL, cpu_sphi);
#endif
#endif
			break; /* 0x2b NOP (nonstd loc, implied) */
	case 0x2c:	_BIT(cpu_read(_abs())); break; /* 0x2c BIT Absolute */
	case 0x2d:	setNZ(A_OP(&,cpu_read(_abs()))); break; /* 0x2d AND Absolute */
	case 0x2e:	_ROL(_abs()); break; /* 0x2e ROL Absolute */
	case 0x2f:	_BRA(!(cpu_read(_zp()) & 4)); break; /* 0x2f BBR Relative */
	case 0x30:	_BRA(cpu_pfn); break; /* 0x30 BMI Relative */
	case 0x31:	setNZ(A_OP(&,cpu_read(_zpiy()))); break; /* 0x31 AND (Zero_Page),Y */
	case 0x32:
#ifdef DTV_CPU_HACK
			cpu_a_sind = cpu_a_tind = cpu_read(cpu_pc++); cpu_a_sind &= 15; cpu_a_tind >>= 4; /* 0x32: DTV specific: SAC */
#else
			/* 0x32 AND (Zero_Page) or (ZP),Z on 65CE02*/
#ifdef MEGA65
			if (IS_FLAT32_DATA_OP())
				setNZ(A_OP(&,cpu_read_linear_opcode()));
			else
#endif
				setNZ(A_OP(&,cpu_read(_zpi())));
#endif
			break;
	case 0x33:
#ifdef CPU_65CE02
			OPC_65CE02("BMI16");
			_BRA16(cpu_pfn); // 65CE02 BMI 16 bit relative
#endif
			break; /* 0x33 NOP (nonstd loc, implied) */
	case 0x34:	_BIT(cpu_read(_zpx())); break; /* 0x34 BIT Zero_Page,X */
	case 0x35:	setNZ(A_OP(&,cpu_read(_zpx()))); break; /* 0x35 AND Zero_Page,X */
	case 0x36:	_ROL(_zpx()); break; /* 0x36 ROL Zero_Page,X */
	case 0x37:	{ int a = _zp(); cpu_write(a, cpu_read(a) & 247); } break; /* 0x37 RMB Zero_Page */
	case 0x38:	cpu_pfc = 1; break; /* 0x38 SEC Implied */
	case 0x39:	setNZ(A_OP(&,cpu_read(_absy()))); break; /* 0x39 AND Absolute,Y */
	case 0x3a:	setNZ(CPU_A_INC(-1)); break; /* 0x3a DEA Accumulator */
	case 0x3b:
#ifdef CPU_65CE02
			OPC_65CE02("DEZ");
			setNZ(--cpu_z);		// 65CE02	DEZ
#endif
			break; /* 0x3b NOP (nonstd loc, implied) */
	case 0x3c:	_BIT(cpu_read(_absx())); break; /* 0x3c BIT Absolute,X */
	case 0x3d:	setNZ(A_OP(&,cpu_read(_absx()))); break; /* 0x3d AND Absolute,X */
	case 0x3e:	_ROL(_absx()); break; /* 0x3e ROL Absolute,X */
	case 0x3f:	_BRA(!(cpu_read(_zp()) & 8)); break; /* 0x3f BBR Relative */
	case 0x40:	cpu_set_p(pop() | 0x10); cpu_pc = popWord(); break; /* 0x40 RTI Implied */
	case 0x41:	setNZ(A_OP(^,cpu_read(_zpxi()))); break; /* 0x41 EOR (Zero_Page,X) */
	case 0x42:
#ifdef CPU_65CE02
			OPC_65CE02("NEG");
			setNZ(cpu_a = -cpu_a);	// 65CE02: NEG	FIXME: flags etc are correct?
#else
#ifdef DTV_CPU_HACK
			cpu_x_ind = cpu_y_ind = cpu_read(cpu_pc++);	// DTV specific: SIR
			cpu_x_ind &= 15;
			cpu_y_ind >>= 4;
#else
			cpu_pc++;	/* 0x42 NOP imm (non-std NOP with addr mode) */
#endif
#endif
			break;
	case 0x43:
#ifdef CPU_65CE02
			// 65CE02: ASR A
			OPC_65CE02("ASR A");
			_ASR(-1);
			//cpu_pfc = cpu_a & 1;
			//cpu_a = (cpu_a >> 1) | (cpu_a & 0x80);
			//setNZ(cpu_a);
#endif
			break; /* 0x43 NOP (nonstd loc, implied) */
	case 0x44:
#ifdef CPU_65CE02
			OPC_65CE02("ASR nn");
			_ASR(_zp());				// 65CE02: ASR $nn
#else
			cpu_pc++;	// 0x44 NOP zp (non-std NOP with addr mode)
#endif
			break;
	case 0x45:	setNZ(A_OP(^,cpu_read(_zp()))); break; /* 0x45 EOR Zero_Page */
	case 0x46:	_LSR(_zp()); break; /* 0x46 LSR Zero_Page */
	case 0x47:	{ int a = _zp(); cpu_write(a, cpu_read(a) & 239); } break; /* 0x47 RMB Zero_Page */
	case 0x48:	push(CPU_A_GET()); break; /* 0x48 PHA Implied */
	case 0x49:	setNZ(A_OP(^,cpu_read(_imm()))); break; /* 0x49 EOR Immediate */
	case 0x4a:	_LSR(-1); break; /* 0x4a LSR Accumulator */
	case 0x4b:
#ifdef CPU_65CE02
			OPC_65CE02("TAZ");
			setNZ(cpu_z = cpu_a);	// 65CE02: TAZ
#endif
			break; /* 0x4b NOP (nonstd loc, implied) */
	case 0x4c:	cpu_pc = _abs(); break; /* 0x4c JMP Absolute */
	case 0x4d:	setNZ(A_OP(^,cpu_read(_abs()))); break; /* 0x4d EOR Absolute */
	case 0x4e:	_LSR(_abs()); break; /* 0x4e LSR Absolute */
	case 0x4f:	_BRA(!(cpu_read(_zp()) & 16)); break; /* 0x4f BBR Relative */
	case 0x50:	_BRA(!cpu_pfv); break; /* 0x50 BVC Relative */
	case 0x51:	setNZ(A_OP(^,cpu_read(_zpiy()))); break; /* 0x51 EOR (Zero_Page),Y */
	case 0x52:	/* 0x52 EOR (Zero_Page) or (ZP),Z on 65CE02 */
#ifdef MEGA65
			if (IS_FLAT32_DATA_OP())
				setNZ(A_OP(^,cpu_read_linear_opcode()));
			else
#endif
				setNZ(A_OP(^,cpu_read(_zpi())));
			break;
	case 0x53:
#ifdef CPU_65CE02
			OPC_65CE02("BVC16");
			_BRA16(!cpu_pfv); // 65CE02: BVC 16-bit-relative
#endif
			break; /* 0x53 NOP (nonstd loc, implied) */
	case 0x54:
#ifdef CPU_65CE02
			OPC_65CE02("ASR nn,X");
			_ASR(_zpx());				// ASR $nn,X
#else
			cpu_pc++;	// NOP zpx (non-std NOP with addr mode)
#endif
			break;
	case 0x55:	setNZ(A_OP(^,cpu_read(_zpx()))); break; /* 0x55 EOR Zero_Page,X */
	case 0x56:	_LSR(_zpx()); break; /* 0x56 LSR Zero_Page,X */
	case 0x57:	{ int a = _zp(); cpu_write(a, cpu_read(a) & 223); } break; /* 0x57 RMB Zero_Page */
	case 0x58:	cpu_pfi = 0; break; /* 0x58 CLI Implied */
	case 0x59:	setNZ(A_OP(^,cpu_read(_absy()))); break; /* 0x59 EOR Absolute,Y */
	case 0x5a:	push(cpu_y); break; /* 0x5a PHY Implied */
	case 0x5b:
#ifdef CPU_65CE02
			OPC_65CE02("TAB");
			cpu_bphi = cpu_a << 8; // 65CE02: TAB
#ifdef DEBUG_CPU
			if (cpu_bphi)
				DEBUG("CPU: WARN base page is non-zero now with value of $%04X" NL, cpu_bphi);
#endif
#endif
			break; /* 0x5b NOP (nonstd loc, implied) */
	case 0x5c:
#ifdef CPU_65CE02
			OPC_65CE02("MAP");
			cpu_do_aug();	/* 0x5c on 65CE02: this is the "AUG" opcode. It must be handled by the emulator, on 4510 (C65) it's redefined as MAP for MMU functionality */
#else
			cpu_pc += 2;
#endif
			break; /* 0x5c NOP (nonstd loc, implied) */ // FIXME: NOP absolute!
	case 0x5d:	setNZ(A_OP(^,cpu_read(_absx()))); break; /* 0x5d EOR Absolute,X */
	case 0x5e:	_LSR(_absx()); break; /* 0x5e LSR Absolute,X */
	case 0x5f:	_BRA(!(cpu_read(_zp()) & 32)); break; /* 0x5f BBR Relative */
	case 0x60:	cpu_pc = popWord() + 1; break; /* 0x60 RTS Implied */
	case 0x61:	_ADC(cpu_read(_zpxi())); break; /* 0x61 ADC (Zero_Page,X) */
	case 0x62:
#ifdef CPU_65CE02
			OPC_65CE02("RTS #nn");
			{	// 65CE02 RTS #$nn TODO: what this opcode does _exactly_? Guess: correcting stack pointer with a given value? Also some docs says it's RTN ...
			int temp = cpu_read(cpu_pc);
			cpu_pc = popWord() + 1;
			if (cpu_sp + temp > 0xFF && (!cpu_pfe))
				cpu_sphi += 0x100;
			cpu_sp += temp; // SP was already incremented by two by popWord, we need only the extra stuff here
			}
#else
			cpu_pc++;	// NOP imm (non-std NOP with addr mode)
#endif
			break;
	case 0x63:
#ifdef CPU_65CE02
			OPC_65CE02("BSR16");
			// 65C02 ?! BSR $nnnn Interesting 65C02-only? FIXME TODO: does this opcode exist before 65CE02 as well?!
			pushWord(cpu_pc + 1);
			_BRA16(1);
#endif
			break; /* 0x63 NOP (nonstd loc, implied) */
	case 0x64:	cpu_write(_zp(), ZERO_REG); break; /* 0x64 STZ Zero_Page */
	case 0x65:	_ADC(cpu_read(_zp())); break; /* 0x65 ADC Zero_Page */
	case 0x66:	_ROR(_zp()); break; /* 0x66 ROR Zero_Page */
	case 0x67:	{ int a = _zp(); cpu_write(a, cpu_read(a) & 191); } break; /* 0x67 RMB Zero_Page */
	case 0x68:	setNZ(CPU_A_SET(pop())); break; /* 0x68 PLA Implied */
	case 0x69:	_ADC(cpu_read(_imm())); break; /* 0x69 ADC Immediate */
	case 0x6a:	_ROR(-1); break; /* 0x6a ROR Accumulator */
	case 0x6b:
#ifdef CPU_65CE02
			OPC_65CE02("TZA");
			setNZ(cpu_a = cpu_z);	// 65CE02 TZA
#endif
			break; /* 0x6b NOP (nonstd loc, implied) */
	case 0x6c:	cpu_pc = _absi(); break; /* 0x6c JMP (Absolute) */
	case 0x6d:	_ADC(cpu_read(_abs())); break; /* 0x6d ADC Absolute */
	case 0x6e:	_ROR(_abs()); break; /* 0x6e ROR Absolute */
	case 0x6f:	_BRA(!(cpu_read(_zp()) & 64)); break; /* 0x6f BBR Relative */
	case 0x70:	_BRA(cpu_pfv); break; /* 0x70 BVS Relative */
	case 0x71:	_ADC(cpu_read(_zpiy())); break; /* 0x71 ADC (Zero_Page),Y */
	case 0x72:	/* 0x72 ADC (Zero_Page) or (ZP),Z on 65CE02 */
#ifdef MEGA65
			if (IS_FLAT32_DATA_OP())
				_ADC(cpu_read(cpu_read_linear_opcode()));
			else
#endif
				_ADC(cpu_read(_zpi()));
			break;
	case 0x73:
#ifdef CPU_65CE02
			OPC_65CE02("BVS16");
			_BRA16(cpu_pfv);	// 65CE02 BVS 16 bit relative
#endif
			break; /* 0x73 NOP (nonstd loc, implied) */
	case 0x74:	cpu_write(_zpx(), ZERO_REG); break; /* 0x74 STZ Zero_Page,X */
	case 0x75:	_ADC(cpu_read(_zpx())); break; /* 0x75 ADC Zero_Page,X */
	case 0x76:	_ROR(_zpx()); break; /* 0x76 ROR Zero_Page,X */
	case 0x77:	{ int a = _zp(); cpu_write(a, cpu_read(a) & 127); } break; /* 0x77 RMB Zero_Page */
	case 0x78:	cpu_pfi = 1; break; /* 0x78 SEI Implied */
	case 0x79:	_ADC(cpu_read(_absy())); break; /* 0x79 ADC Absolute,Y */
	case 0x7a:	setNZ(cpu_y = pop()); break; /* 0x7a PLY Implied */
	case 0x7b:
#ifdef CPU_65CE02
			OPC_65CE02("TBA");
			setNZ(cpu_a = (cpu_bphi >> 8));	// 65C02 TBA
#endif
			break; /* 0x7b NOP (nonstd loc, implied) */
	case 0x7c:	cpu_pc = _absxi(); break; /* 0x7c JMP (Absolute,X) */
	case 0x7d:	_ADC(cpu_read(_absx())); break; /* 0x7d ADC Absolute,X */
	case 0x7e:	_ROR(_absx()); break; /* 0x7e ROR Absolute,X */
	case 0x7f:	_BRA(!(cpu_read(_zp()) & 128)); break; /* 0x7f BBR Relative */
	case 0x80:	_BRA(1); break; /* 0x80 BRA Relative */
	case 0x81:	cpu_write(_zpxi(), CPU_A_GET()); break; /* 0x81 STA (Zero_Page,X) */
	case 0x82:
#ifdef CPU_65CE02
			OPC_65CE02("STA (nn,S),Y");
			cpu_write(_GET_SP_INDIRECT_ADDR(), cpu_a);	// 65CE02 STA ($nn,SP),Y
#else
			cpu_pc++;	// NOP imm (non-std NOP with addr mode)
#endif
			break;
	case 0x83:
#ifdef CPU_65CE02
			OPC_65CE02("BRA16");
			_BRA16(1);	// 65CE02 BRA $nnnn 16-bit-pc-rel?
#endif
			break; /* 0x83 NOP (nonstd loc, implied) */
	case 0x84:	cpu_write(_zp(), cpu_y); break; /* 0x84 STY Zero_Page */
	case 0x85:	cpu_write(_zp(), CPU_A_GET()); break; /* 0x85 STA Zero_Page */
	case 0x86:	cpu_write(_zp(), cpu_x); break; /* 0x86 STX Zero_Page */
	case 0x87:	{ int a = _zp(); cpu_write(a, cpu_read(a) | 1); } break; /* 0x87 SMB Zero_Page */
	case 0x88:	setNZ(--cpu_y); break; /* 0x88 DEY Implied */
	case 0x89:	cpu_pfz = (!(CPU_A_GET() & cpu_read(_imm()))); break; /* 0x89 BIT+ Immediate */
	case 0x8a:	setNZ(CPU_A_SET(cpu_x)); break; /* 0x8a TXA Implied */
	case 0x8b:
#ifdef CPU_65CE02
			OPC_65CE02("STY nnnn,X");
			cpu_write(_absx(), cpu_y); // 65CE02 STY $nnnn,X
#endif
			break; /* 0x8b NOP (nonstd loc, implied) */
	case 0x8c:	cpu_write(_abs(), cpu_y); break; /* 0x8c STY Absolute */
	case 0x8d:	cpu_write(_abs(), CPU_A_GET()); break; /* 0x8d STA Absolute */
	case 0x8e:	cpu_write(_abs(), cpu_x); break; /* 0x8e STX Absolute */
	case 0x8f:	_BRA( cpu_read(_zp()) & 1 ); break; /* 0x8f BBS Relative */
	case 0x90:	_BRA(!cpu_pfc); break; /* 0x90 BCC Relative */
	case 0x91:	cpu_write(_zpiy(), CPU_A_GET()); break; /* 0x91 STA (Zero_Page),Y */
	case 0x92:	// /* 0x92 STA (Zero_Page) or (ZP),Z on 65CE02 */
#ifdef MEGA65
			if (IS_FLAT32_DATA_OP())
				cpu_write_linear_opcode(CPU_A_GET());
			else
#endif
				cpu_write(_zpi(), CPU_A_GET());
			break;
	case 0x93:
#ifdef CPU_65CE02
			OPC_65CE02("BCC16");
			_BRA16(!cpu_pfc);	// 65CE02  BCC $nnnn
#endif
			break; /* 0x93 NOP (nonstd loc, implied) */
	case 0x94:	cpu_write(_zpx(), cpu_y); break; /* 0x94 STY Zero_Page,X */
	case 0x95:	cpu_write(_zpx(), CPU_A_GET()); break; /* 0x95 STA Zero_Page,X */
	case 0x96:	cpu_write(_zpy(), cpu_x); break; /* 0x96 STX Zero_Page,Y */
	case 0x97:	{ int a = _zp(); cpu_write(a, cpu_read(a) | 2); } break; /* 0x97 SMB Zero_Page */
	case 0x98:	setNZ(CPU_A_SET(cpu_y)); break; /* 0x98 TYA Implied */
	case 0x99:	cpu_write(_absy(), CPU_A_GET()); break; /* 0x99 STA Absolute,Y */
	case 0x9a:	cpu_sp = cpu_x; break; /* 0x9a TXS Implied */
	case 0x9b:
#ifdef CPU_65CE02
			OPC_65CE02("STX nnnn,Y");
			cpu_write(_absy(), cpu_x);	// 65CE02 STX $nnnn,Y
#endif
			break; /* 0x9b NOP (nonstd loc, implied) */
	case 0x9c:	cpu_write(_abs(), ZERO_REG); break; /* 0x9c STZ Absolute */
	case 0x9d:	cpu_write(_absx(), CPU_A_GET()); break; /* 0x9d STA Absolute,X */
	case 0x9e:	cpu_write(_absx(), ZERO_REG); break; /* 0x9e STZ Absolute,X */
	case 0x9f:	_BRA( cpu_read(_zp()) & 2 ); break; /* 0x9f BBS Relative */
	case 0xa0:	setNZ(cpu_y = cpu_read(_imm())); break; /* 0xa0 LDY Immediate */
	case 0xa1:	setNZ(CPU_A_SET(cpu_read(_zpxi()))); break; /* 0xa1 LDA (Zero_Page,X) */
	case 0xa2:	setNZ(cpu_x = cpu_read(_imm())); break; /* 0xa2 LDX Immediate */
	case 0xa3:
#ifdef CPU_65CE02
			OPC_65CE02("LDZ #nn");
			setNZ(cpu_z = cpu_read(_imm())); // LDZ #$nn             A3   65CE02
#endif
			break; /* 0xa3 NOP (nonstd loc, implied) */
	case 0xa4:	setNZ(cpu_y = cpu_read(_zp())); break; /* 0xa4 LDY Zero_Page */
	case 0xa5:	setNZ(CPU_A_SET(cpu_read(_zp()))); break; /* 0xa5 LDA Zero_Page */
	case 0xa6:	setNZ(cpu_x = cpu_read(_zp())); break; /* 0xa6 LDX Zero_Page */
	case 0xa7:	{ int a = _zp(); cpu_write(a, cpu_read(a) | 4); } break; /* 0xa7 SMB Zero_Page */
	case 0xa8:	setNZ(cpu_y = CPU_A_GET()); break; /* 0xa8 TAY Implied */
	case 0xa9:	setNZ(CPU_A_SET(cpu_read(_imm()))); break; /* 0xa9 LDA Immediate */
	case 0xaa:	setNZ(cpu_x = CPU_A_GET()); break; /* 0xaa TAX Implied */
	case 0xab:
#ifdef CPU_65CE02
			OPC_65CE02("LDZ nnnn");
			setNZ(cpu_z = cpu_read(_abs()));	// 65CE02 LDZ $nnnn
#endif
			break; /* 0xab NOP (nonstd loc, implied) */
	case 0xac:	setNZ(cpu_y = cpu_read(_abs())); break; /* 0xac LDY Absolute */
	case 0xad:	setNZ(CPU_A_SET(cpu_read(_abs()))); break; /* 0xad LDA Absolute */
	case 0xae:	setNZ(cpu_x = cpu_read(_abs())); break; /* 0xae LDX Absolute */
	case 0xaf:	_BRA( cpu_read(_zp()) & 4 ); break; /* 0xaf BBS Relative */
	case 0xb0:	_BRA(cpu_pfc); break; /* 0xb0 BCS Relative */
	case 0xb1:	setNZ(CPU_A_SET(cpu_read(_zpiy()))); break; /* 0xb1 LDA (Zero_Page),Y */
	case 0xb2:	/* 0xb2 LDA (Zero_Page) or (ZP),Z on 65CE02 */
#ifdef MEGA65
			if (IS_FLAT32_DATA_OP())
				setNZ(CPU_A_SET(cpu_read_linear_opcode()));
			else
#endif
				setNZ(CPU_A_SET(cpu_read(_zpi())));
			break;
	case 0xb3:
#ifdef CPU_65CE02
			OPC_65CE02("BCS16");
			_BRA16(cpu_pfc);	// 65CE02 BCS $nnnn
#endif
			break; /* 0xb3 NOP (nonstd loc, implied) */
	case 0xb4:	setNZ(cpu_y = cpu_read(_zpx())); break; /* 0xb4 LDY Zero_Page,X */
	case 0xb5:	setNZ(CPU_A_SET(cpu_read(_zpx()))); break; /* 0xb5 LDA Zero_Page,X */
	case 0xb6:	setNZ(cpu_x = cpu_read(_zpy())); break; /* 0xb6 LDX Zero_Page,Y */
	case 0xb7:	{ int a = _zp(); cpu_write(a, cpu_read(a) | 8); } break; /* 0xb7 SMB Zero_Page */
	case 0xb8:	cpu_pfv = 0; break; /* 0xb8 CLV Implied */
	case 0xb9:	setNZ(CPU_A_SET(cpu_read(_absy()))); break; /* 0xb9 LDA Absolute,Y */
	case 0xba:	setNZ(cpu_x = cpu_sp); break; /* 0xba TSX Implied */
	case 0xbb:
#ifdef CPU_65CE02
			OPC_65CE02("LDZ nnnn,X");
			setNZ(cpu_z = cpu_read(_absx()));	// 65CE02 LDZ $nnnn,X
#endif
			break; /* 0xbb NOP (nonstd loc, implied) */
	case 0xbc:	setNZ(cpu_y = cpu_read(_absx())); break; /* 0xbc LDY Absolute,X */
	case 0xbd:	setNZ(CPU_A_SET(cpu_read(_absx()))); break; /* 0xbd LDA Absolute,X */
	case 0xbe:	setNZ(cpu_x = cpu_read(_absy())); break; /* 0xbe LDX Absolute,Y */
	case 0xbf:	_BRA( cpu_read(_zp()) & 8 ); break; /* 0xbf BBS Relative */
	case 0xc0:	_CMP(cpu_y, cpu_read(_imm())); break; /* 0xc0 CPY Immediate */
	case 0xc1:	_CMP(CPU_A_GET(), cpu_read(_zpxi())); break; /* 0xc1 CMP (Zero_Page,X) */
	case 0xc2:
#ifdef CPU_65CE02
			OPC_65CE02("CPZ #nn");
			_CMP(cpu_z, cpu_read(_imm()));	// 65CE02 CPZ #$nn
#else
			cpu_pc++; // imm (non-std NOP with addr mode)
#endif
			break;
	case 0xc3:
#ifdef CPU_65CE02
			OPC_65CE02("DEW nn");
			{       //  DEW $nn 65CE02  C3  Decrement Word (maybe an error in 64NET.OPC ...) ANOTHER FIX: this is zero (errr, base ...) page!!!
                        int alo = _zp();
                        int ahi = (alo & 0xFF00) | ((alo + 1) & 0xFF);
                        Uint16 data = (cpu_read(alo) | (cpu_read(ahi) << 8)) - 1;
                        setNZ16(data);
                        cpu_write(alo, data & 0xFF);
                        cpu_write(ahi, data >> 8);
                        }
#endif
			break; /* 0xc3 NOP (nonstd loc, implied) */
	case 0xc4:	_CMP(cpu_y, cpu_read(_zp())); break; /* 0xc4 CPY Zero_Page */
	case 0xc5:	_CMP(CPU_A_GET(), cpu_read(_zp())); break; /* 0xc5 CMP Zero_Page */
	case 0xc6:	{ int addr = _zp(); Uint8 data = cpu_read(addr) - 1; setNZ(data); cpu_write(addr, data); } break; /* 0xc6 DEC Zero_Page */
	case 0xc7:	{ int a = _zp(); cpu_write(a, cpu_read(a) | 16); } break; /* 0xc7 SMB Zero_Page */
	case 0xc8:	setNZ(++cpu_y); break; /* 0xc8 INY Implied */
	case 0xc9:	_CMP(CPU_A_GET(), cpu_read(_imm())); break; /* 0xc9 CMP Immediate */
	case 0xca:	setNZ(--cpu_x); break; /* 0xca DEX Implied */
	case 0xcb:
#ifdef CPU_65CE02
			OPC_65CE02("ASW nnnn");
			{					// 65CE02 ASW $nnnn	(CB  Arithmetic Shift Left Word)
			int addr = _abs();
			Uint16 data = cpu_read(addr) | (cpu_read(addr + 1) << 8);
			cpu_pfc = data & 0x8000;
			data <<= 1;
			setNZ16(data);
			cpu_write(addr, data & 0xFF);
			cpu_write(addr + 1, data >> 8);
			}
#endif
			break; /* 0xcb NOP (nonstd loc, implied) */
	case 0xcc:	_CMP(cpu_y, cpu_read(_abs())); break; /* 0xcc CPY Absolute */
	case 0xcd:	_CMP(CPU_A_GET(), cpu_read(_abs())); break; /* 0xcd CMP Absolute */
	case 0xce:	{ int addr = _abs(); Uint8 data = cpu_read(addr) - 1; setNZ(data); cpu_write(addr, data); } break; /* 0xce DEC Absolute */
	case 0xcf:	_BRA( cpu_read(_zp()) & 16 ); break; /* 0xcf BBS Relative */
	case 0xd0:	_BRA(!cpu_pfz); break; /* 0xd0 BNE Relative */
	case 0xd1:	_CMP(CPU_A_GET(), cpu_read(_zpiy())); break; /* 0xd1 CMP (Zero_Page),Y */
	case 0xd2:	/* 0xd2 CMP (Zero_Page) or (ZP),Z on 65CE02 */
#ifdef MEGA65
			if (IS_FLAT32_DATA_OP())	// NOTE: this was not mentioned in Paul's blog-post, but this op should have this property as well, IMHO!
				_CMP(CPU_A_GET(), cpu_read_linear_opcode());
			else
#endif
				_CMP(CPU_A_GET(), cpu_read(_zpi()));
			break;
	case 0xd3:
#ifdef CPU_65CE02
			OPC_65CE02("BNE16");
			_BRA16(!cpu_pfz);	// 65CE02 BNE $nnnn
#endif
			break; /* 0xd3 NOP (nonstd loc, implied) */
	case 0xd4:
#ifdef CPU_65CE02
			OPC_65CE02("CPZ nn");
			_CMP(cpu_z, cpu_read(_zp()));	// 65CE02 CPZ $nn
#else
			cpu_pc++;	// NOP zpx (non-std NOP with addr mode)
#endif
			break;
	case 0xd5:	_CMP(CPU_A_GET(), cpu_read(_zpx())); break; /* 0xd5 CMP Zero_Page,X */
	case 0xd6:	{ int addr = _zpx(); Uint8 data = cpu_read(addr) - 1; setNZ(data); cpu_write(addr, data); } break; /* 0xd6 DEC Zero_Page,X */
	case 0xd7:	{ int a = _zp(); cpu_write(a, cpu_read(a) | 32); } break; /* 0xd7 SMB Zero_Page */
	case 0xd8:	cpu_pfd = 0; break; /* 0xd8 CLD Implied */
	case 0xd9:	_CMP(CPU_A_GET(), cpu_read(_absy())); break; /* 0xd9 CMP Absolute,Y */
	case 0xda:	push(cpu_x); break; /* 0xda PHX Implied */
	case 0xdb:
#ifdef CPU_65CE02
			OPC_65CE02("PHZ");
			push(cpu_z);		// 65CE02: PHZ
#endif
			break; /* 0xdb NOP (nonstd loc, implied) */
	case 0xdc:
#ifdef CPU_65CE02
			OPC_65CE02("CPZ nnnn");
			_CMP(cpu_z, cpu_read(_abs())); // 65CE02 CPZ $nnnn
#else
			cpu_pc += 2;
#endif
			break; /* 0xdc NOP (nonstd loc, implied) */ // FIXME: bugfix NOP absolute!
	case 0xdd:	_CMP(CPU_A_GET(), cpu_read(_absx())); break; /* 0xdd CMP Absolute,X */
	case 0xde:	{ int addr = _absx(); Uint8 data = cpu_read(addr) - 1; setNZ(data); cpu_write(addr, data); } break; /* 0xde DEC Absolute,X */
	case 0xdf:	_BRA( cpu_read(_zp()) & 32 ); break; /* 0xdf BBS Relative */
	case 0xe0:	_CMP(cpu_x, cpu_read(_imm())); break; /* 0xe0 CPX Immediate */
	case 0xe1:	_SBC(cpu_read(_zpxi())); break; /* 0xe1 SBC (Zero_Page,X) */
	case 0xe2:
#ifdef CPU_65CE02
			OPC_65CE02("LDA (nn,S),Y");
			// 65CE02 LDA ($nn,SP),Y
			// REALLY IMPORTANT: please read the comment at _GET_SP_INDIRECT_ADDR()!
			setNZ(cpu_a = cpu_read(_GET_SP_INDIRECT_ADDR()));
			//DEBUG("CPU: LDA (nn,S),Y returned: A = $%02X, P before last IRQ was: $%02X" NL, cpu_a, last_p);
#else
			cpu_pc++; // 0xe2 NOP imm (non-std NOP with addr mode)
#endif
			break;
	case 0xe3:
#ifdef CPU_65CE02
			OPC_65CE02("INW nn");
			{	//  INW $nn            E3  Increment Word (maybe an error in 64NET.OPC ...) ANOTHER FIX: this is zero (errr, base ...) page!!!
			int alo = _zp();
			int ahi = (alo & 0xFF00) | ((alo + 1) & 0xFF);
			Uint16 data = (cpu_read(alo) | (cpu_read(ahi) << 8)) + 1;
			setNZ16(data);
			//cpu_pfz = (data == 0);
			cpu_write(alo, data & 0xFF);
			cpu_write(ahi, data >> 8);
			}
#endif
			break; /* 0xe3 NOP (nonstd loc, implied) */
	case 0xe4:	_CMP(cpu_x, cpu_read(_zp())); break; /* 0xe4 CPX Zero_Page */
	case 0xe5:	_SBC(cpu_read(_zp())); break; /* 0xe5 SBC Zero_Page */
	case 0xe6:	{ int addr = _zp(); Uint8 data = cpu_read(addr) + 1; setNZ(data); cpu_write(addr, data); } break; /* 0xe6 INC Zero_Page */
	case 0xe7:	{ int a = _zp(); cpu_write(a, cpu_read(a) | 64); } break; /* 0xe7 SMB Zero_Page */
	case 0xe8:	setNZ(++cpu_x); break; /* 0xe8 INX Implied */
	case 0xe9:	_SBC(cpu_read(_imm())); break; /* 0xe9 SBC Immediate */
	case 0xea:
#ifdef CPU_65CE02
			// on 65CE02 it's not special, but in C65 (4510) it is (EOM). It's up the emulator though ...
			OPC_65CE02("EOM");
			cpu_do_nop();
#endif
			break;	// 0xea NOP Implied - the "standard" NOP of original 6502 core
	case 0xeb:
#ifdef CPU_65CE02
			OPC_65CE02("ROW nnnn");			// ROW $nnnn		EB  Rotate word LEFT?! [other documents says RIGHT!!!]
			{
			int addr = _abs();
			int data = ((cpu_read(addr) | (cpu_read(addr + 1) << 8)) << 1) | (cpu_pfc ? 1 : 0);
			cpu_pfc = data & 0x10000;
			data &= 0xFFFF;
			setNZ16(data);
			cpu_write(addr, data & 0xFF);
			cpu_write(addr + 1, data >> 8);
			}
#endif
			break; /* 0xeb NOP (nonstd loc, implied) */
	case 0xec:	_CMP(cpu_x, cpu_read(_abs())); break; /* 0xec CPX Absolute */
	case 0xed:	_SBC(cpu_read(_abs())); break; /* 0xed SBC Absolute */
	case 0xee:	{ int addr = _abs(); Uint8 data = cpu_read(addr) + 1; setNZ(data); cpu_write(addr, data); } break; /* 0xee INC Absolute */
	case 0xef:	_BRA( cpu_read(_zp()) & 64 ); break; /* 0xef BBS Relative */
	case 0xf0:	_BRA(cpu_pfz); break; /* 0xf0 BEQ Relative */
	case 0xf1:	_SBC(cpu_read(_zpiy())); break; /* 0xf1 SBC (Zero_Page),Y */
	case 0xf2:	/* 0xf2 SBC (Zero_Page) or (ZP),Z on 65CE02 */
#ifdef MEGA65
			if (IS_FLAT32_DATA_OP())
				_SBC(cpu_read_linear_opcode());
			else
#endif
				_SBC(cpu_read(_zpi()));
			break;
	case 0xf3:
#ifdef CPU_65CE02
			OPC_65CE02("BEQ16");
			_BRA16(cpu_pfz);	// 65CE02 BEQ $nnnn
#endif
			break; /* 0xf3 NOP (nonstd loc, implied) */
	case 0xf4:
#ifdef CPU_65CE02
			OPC_65CE02("PHW #nnnn");
			PUSH_FOR_PHW(readWord(cpu_pc));		// 65CE02 PHW #$nnnn (push word)
			cpu_pc += 2;
#else
			cpu_pc++; // 0xf4 NOP zpx (non-std NOP with addr mode)
#endif
			break;
	case 0xf5:	_SBC(cpu_read(_zpx())); break; /* 0xf5 SBC Zero_Page,X */
	case 0xf6:	{ int addr = _zpx(); Uint8 data = cpu_read(addr) + 1; setNZ(data); cpu_write(addr, data); } break; /* 0xf6 INC Zero_Page,X */
	case 0xf7:	{ int a = _zp(); cpu_write(a, cpu_read(a) | 128); } break; /* 0xf7 SMB Zero_Page */
	case 0xf8:	cpu_pfd = 1; break; /* 0xf8 SED Implied */
	case 0xf9:	_SBC(cpu_read(_absy())); break; /* 0xf9 SBC Absolute,Y */
	case 0xfa:	setNZ(cpu_x = pop()); break; /* 0xfa PLX Implied */
	case 0xfb:
#ifdef CPU_65CE02
			OPC_65CE02("PLZ");
			setNZ(cpu_z = pop());	// 65CE02 PLZ
#endif
			break; /* 0xfb NOP (nonstd loc, implied) */
	case 0xfc:
#ifdef CPU_65CE02
			OPC_65CE02("PHW nnnn");
			PUSH_FOR_PHW(readWord(readWord(cpu_pc)));	// PHW $nnnn [? push word from an absolute address, maybe?] Note: C65 BASIC depends on this opcode to be correct!
			cpu_pc += 2;
#if 0
			{					// PHW $nnnn [? push word from an absolute address, maybe?]
			Uint16 temp = cpu_read(cpu_pc++);
			temp |= cpu_read(cpu_pc++) << 8;
			pushWord(readWord(temp));
			}
#endif
#else
			cpu_pc += 2;
#endif
			break; /* 0xfc NOP (nonstd loc, implied) */ // FIXME: bugfix NOP absolute?
	case 0xfd:	_SBC(cpu_read(_absx())); break; /* 0xfd SBC Absolute,X */
	case 0xfe:	{ int addr = _absx(); Uint8 data = cpu_read(addr) + 1; setNZ(data); cpu_write(addr, data); } break; /* 0xfe INC Absolute,X */
	case 0xff:	_BRA( cpu_read(_zp()) & 128 ); break; /* 0xff BBS Relative */
#ifdef DEBUG_CPU
	default:
			FATAL("FATAL: not handled CPU opcode: $%02X", cpu_op);
			break;
#endif
	}
	return cpu_cycles;
}


/* ---- SNAPSHOT RELATED ---- */

/* NOTE: cpu_linear_memory_addressing_is_enabled is not the CPU emulator handled data ...
*/


#ifdef XEMU_SNAPSHOT_SUPPORT

#include "emutools_snapshot.h"
#include <string.h>

#define SNAPSHOT_CPU_BLOCK_VERSION	0
#define SNAPSHOT_CPU_BLOCK_SIZE		256

#ifdef CPU_65CE02
#define SNAPSHOT_CPU_ID			2
#else
#define SNAPSHOT_CPU_ID			1
#endif

int cpu_snapshot_load_state ( const struct xemu_snapshot_definition_st *def, struct xemu_snapshot_block_st *block )
{
	int ret;
	Uint8 buffer[SNAPSHOT_CPU_BLOCK_SIZE];
	if (block->sub_counter || block->block_version != SNAPSHOT_CPU_BLOCK_VERSION || block->sub_size != sizeof buffer)
		RETURN_XSNAPERR_USER("Bad CPU 65xx block syntax");
	ret = xemusnap_read_file(buffer, sizeof buffer);
	if (ret) return ret;
	if (buffer[0] != SNAPSHOT_CPU_ID)
		RETURN_XSNAPERR_USER("CPU type mismatch");
	cpu_pc = P_AS_BE16(buffer + 1);
	cpu_a = buffer[3];
	cpu_x = buffer[4];
	cpu_y = buffer[5];
	cpu_sp = buffer[6];
	cpu_set_p(buffer[7]);
	cpu_pfe = buffer[7] & 32;	// must be set manually ....
	cpu_irqLevel = (int)P_AS_BE32(buffer + 32);
	cpu_nmiEdge  = (int)P_AS_BE32(buffer + 36);
	cpu_cycles = buffer[42];
	cpu_op = buffer[43];
#ifdef CPU_65CE02
	cpu_z = buffer[64];
	cpu_bphi = (Uint16)buffer[65] << 8;
	cpu_sphi = (Uint16)buffer[66] << 8;
	cpu_inhibit_interrupts = (int)P_AS_BE32(buffer + 96);
#endif
	return 0;
}


int cpu_snapshot_save_state ( const struct xemu_snapshot_definition_st *def )
{
	Uint8 buffer[SNAPSHOT_CPU_BLOCK_SIZE];
	int ret = xemusnap_write_block_header(def->idstr, SNAPSHOT_CPU_BLOCK_VERSION);
	if (ret) return ret;
	memset(buffer, 0xFF, sizeof buffer);
	buffer[0] = SNAPSHOT_CPU_ID;
	U16_AS_BE(buffer + 1, cpu_pc);
	buffer[3] = cpu_a;
	buffer[4] = cpu_x;
	buffer[5] = cpu_y;
	buffer[6] = cpu_sp;
	buffer[7] = cpu_get_p();
	U32_AS_BE(buffer + 32, (Uint32)cpu_irqLevel);
	U32_AS_BE(buffer + 36, (Uint32)cpu_nmiEdge);
	buffer[42] = cpu_cycles;
	buffer[43] = cpu_op;
#ifdef CPU_65CE02
	buffer[64] = cpu_z;
	buffer[65] = cpu_bphi >> 8;
	buffer[66] = cpu_sphi >> 8;
	U32_AS_BE(buffer + 96, (Uint32)cpu_inhibit_interrupts);
#endif
	return xemusnap_write_sub_block(buffer, sizeof buffer);
}
#endif
