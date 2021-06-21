/* Z8001 CPU emulator
 * Part of the Xemu project, please visit: https://github.com/lgblgblgb/xemu
 * Copyright (C)2018 LGB (Gábor Lénárt) <lgblgblgb@gmail.com>
 * --------------------------------------------------------------------------------------
 * Written from the technical manual of the Z8000 released by Zilog by me,
 * so it can be very ugly, strange and incorrect code, especially, because
 * Z8000 is a new thing me, first try to even get to know it ...
 * In many cases, the source is about the most crude solution, far from being
 * optimized. Also, Z8K though being 16 bit CPU, the internal emu struct is 8 bit
 * based mainly to simplify things and not to be confused with different byte order
 * between Z8K and emu host or similar issues.
 * --------------------------------------------------------------------------------------
 * My main problem was: I couldn't find any existing open source Z8001 emulation.
 * MAME has "something" but MAME's license is "strange" and also it markes the emulation
 * as "non-working". Also it's C++ what I couldn't even understand. So I haven't even
 * tried to use that resource of course, other than checking out the binary itself if
 * it can emulate a Commodore 900. Some resources mention that Commodore 900 is a "rather
 * simple typical Z8001 based machine". OK, I've tried to find other Z8001 based
 * machines. I could find P8000 system which uses Z8001, and it has an emulator.
 * But well, no source at all. Ok then, so I give up, I have to write my own from
 * zero ...
 * --------------------------------------------------------------------------------------
 * This file was extremely useful for me:
 * https://raw.githubusercontent.com/bsdphk/PyRevEng/master/cpus/z8000_instructions.txt
 * since I don't have the Z8000 manual only a poor quality scanned PDF, and it was
 * a fantasic way to write a python program myself (not the one in PyRevEng!) to generate
 * the skeleton of the big switch/case statement and comments on the opcode composition.
 * Surely, maybe even more can be generated right from the listing file, but most of the
 * code is hand-written here anyway after this step.

 ****************************************************************************************

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
#include "z8k1.h"

// Undefine this, to see compiler errors we need to fix cycles at
//#define TODO_CYCLES 1


#define BEGIN do{
#define END   }while(0);break;
#define OPCODE(n) END case n: BEGIN


#define DO_DISASM
#define DO_EXEC		1

#define FLAG_CARRY	0x80
#define FLAG_ZERO	0x40
#define FLAG_SIGN	0x20
#define FLAG_PV		0x10
#define FLAG_DA		0x08
#define FLAG_HC		0x04
#define F_CARRY_BOOL	(!!(z8k1.flags & FLAG_CARRY))
#define	F_ZERO_BOOL	(!!(z8k1.flags & FLAG_ZERO))
#define F_SIGN_BOOL	(!!(z8k1.flags & FLAG_SIGN))
#define F_PV_BOOL	(!!(z8k1.flags & FLAG_PV))
#define F_DA_BOOL	(!!(z8k1.flags & FLAG_DA))
#define F_HC_BOOL	(!!(z8k1.flags & FLAG_HC))


// Lower 3 bits are not used.
// SEG: 1 = CPU in segmented mode (not for Z8002)
// SYS: 1 = CPU is in system mode (privileged opcodes can be used, stack pointer changes etc - priv ops in user mode causes traps)
// EPA: 1 = extended processing architecture (when 0, EPA ops cause traps)
// VIE: 1 = vectored interrupts enabled
// NVIE: 1 = non-vectored interrupts enabled
#define FCW_SEG		0x80
#define FCW_SYS		0x40
#define FCW_EPA		0x20
#define FCW_VIE		0x10
#define FCW_NVIE	0x08
#define FCW_ALL_MASK	(FCW_SEG|FCW_SYS|FCW_EPA|FCW_VIE|FCW_NVIE)



#define IS_SEGMENTED_MODE	(z8k1.fcw & FCW_SEG)
#define IS_SYSTEM_MODE		(z8k1.fcw & FCW_SYS)
#define IS_USER_MODE		(!(IS_SYSTEM_MODE))

static int do_disasm = 1;


#ifdef DO_DISASM
#define DISASM(fmt,...) do { if (XEMU_UNLIKELY(do_disasm)) DEBUGPRINT("%02X:%04X %04X " fmt NL, z8k1.codeseg, pc_orig, opc, __VA_ARGS__); } while(0)
#else
#define DISASM(fmt,...)
#endif



#define NOT_EMULATED_OPCODE()		FATAL("ERROR: Opcode not emulated: $%04X at $%02X:%04X", opc, z8k1.codeseg, pc_orig)
#define NOT_EMULATED_OPCODE_VARIANT()	FATAL("ERROR: Opcode VARIANT not emulated: $%04X at $%02X:%04X", opc, z8k1.codeseg, pc_orig)
#define RESERVED_OPCODE()		FATAL("ERROR: Reserved opcode: $%04X at $%02X:%04X\n", opc, z8k1.codeseg, pc_orig);


// The *internal* (to this emulator, not for Z8000!) representation
// of the general purpose registers. Internally, we use 8 bit scheme,
// that's why we have 32 of them, and not 16. Note, that Z8000 register
// reference encoding is a bit tricky, ie it can only do byte based
// reference for the first 8 16-bit register, but can access all of
// them as 16 bit versions, and also 32/64 bit access can be done.
// Logically we use the register file in this order:
// HIGH byte of REG0, LOW byte of REG0, HIGH byte of REG1, LOW byte of REG1, etc ..
// for 16 bits registers. See the macro REG8INDEX().
// Also, this is the *actual* and *used* register set. Things like user/system
// mode stack etc is not handled, and must be COPIED the appropriate one here
// on CPU mode change, etc!
static struct {
	Uint16 regs[16];
	Uint8 fcw, flags;	// for real, these are one word in most cases
	Uint16 pc;
	Uint8 codeseg;
	Uint16 stackptrusr, stackptrsys;
	Uint8 stacksegusr, stacksegsys;
	Uint16 refresh;
	Uint8 psaseg;
	Uint16 psaofs;
	//int m1;		// FIXME: no sane "bus mode" (encoded with 4 bits) are used in emulation. this wanted to mean the opcode fetch though if set in read mem callback
	Uint16 use_ofs;
	Uint8  use_seg;
	int get_address_mode;
} z8k1;


#ifdef DO_DISASM
static const char *__reg8names__[16] = {
	"RH0", "RH1", "RH2", "RH3", "RH4", "RH5", "RH6", "RH7",
	"RL0", "RL1", "RL2", "RL3", "RL4", "RL5", "RL6", "RL7"
};
#define reg8names(n) __reg8names__[(n) & 0xF]
static const char *__reg16names__[16] = {
	"R0", "R1", "R2",  "R3",  "R4",  "R5",  "R6",  "R7",
	"R8", "R9", "R10", "R11", "R12", "R13", "R14", "R15"
};
#define reg16names(n) __reg16names__[(n) & 0xF]
static const char *__reg32names__[16] = {
	"RR0", "RR0?", "RR2",  "RR2?",  "RR4",  "RR4?",  "RR6",  "RR6?",
	"RR8", "RR8?", "RR10", "RR10?", "RR12", "RR12?", "RR14", "RR14?"
};
#define reg32names(n) __reg32names__[(n) & 0xF]
static const char *__reg64names__[16] = {
	"RQ0", "RQ0?", "RQ0??", "RQ0???", "RQ4",  "RQ4?",  "RQ4??",  "RQ4???",
	"RQ8", "RQ8?", "RQ8??", "RQ8???", "RQ12", "RQ12?", "RQ12??", "RQ12???"
};
#define reg64names(n) __reg64names__[(n) & 0xF]
static const char *__ccnames__[16] = {
	"F", "LT", "LE", "ULE", "PE", "MI", "Z",  "C",
	"T", "GE", "GT", "UGT", "PO", "PL", "NZ", "NC"
};
#define ccnames(n) __ccnames__[(n) & 0xF]
#endif



static Uint16 READCODE ( void )
{
	Uint16 ret;
	if (XEMU_UNLIKELY(z8k1.pc & 1))
		FATAL("READCODE() at odd address $%04X", z8k1.pc);
	ret = z8k1_read_code_cb(z8k1.codeseg, z8k1.pc);
	z8k1.pc += 2;
	//z8k1.opfetch_cycle++;
	return ret;
}

// Check conditions based on flags, with the "cc" field of the opcode
// Note: Z8K has "T" and "F" codes as well, being always True and always False (though "F" sounds a bit useless, T can be useful!)
// Note: some of the coniditions have more names, ie "S" is also "MI" minus, etc etc ...
static int check_cc ( int cc )
{
	switch (cc & 0xF) {
		case 0x0:	// F: always false!
			return 0;
		case 0x1:	// LT: less-than: S XOR V = 1, that is: S != V
			return F_SIGN_BOOL ^ F_PV_BOOL;
		case 0x2:	// LE: less than or equal: Z OR (S XOR V) = 1
			return F_ZERO_BOOL | (F_SIGN_BOOL ^ F_PV_BOOL);
		case 0x3:	// ULE: unsigned less than or equal: (C OR Z) = 1
			return F_CARRY_BOOL | F_ZERO_BOOL;
		case 0x4:	// OV/PE = overflow or parity even
			return F_PV_BOOL;
		case 0x5:	// MI = minus, test for S=1
			return F_SIGN_BOOL;
		case 0x6:	// EQ/Z = equal or zero, test for Z=1
			return F_ZERO_BOOL;
		case 0x7:	// ULT/carry
			return F_CARRY_BOOL;
		case 0x8:	// T: always true!
			return 1;
		case 0x9:	// GE: greater than or equal: S XOR V = 0, that is: S == V
			return !(F_SIGN_BOOL ^ F_PV_BOOL);
		case 0xA:	// GT: greater than: Z OR (S XOR V) = 0
			return !(F_ZERO_BOOL | (F_SIGN_BOOL ^ F_PV_BOOL));
		case 0xB:	// UGT: unsigned greater than: ((C = 0) AND (Z = 0)) = 1
			return (!F_CARRY_BOOL) & (!F_ZERO_BOOL);
		case 0xC:	//
			return !(F_PV_BOOL);
		case 0xD:	//
			return !(F_SIGN_BOOL);
		case 0xE:	//
			return !(F_ZERO_BOOL);
		case 0xF:	//
			return !(F_CARRY_BOOL);
	}
	FATAL("Cannot happen");	// clang is stupid, cannot figure out that all cases are handled ...
}




#define F_ZERO_BY8(v)			(((v) & 0xFF) ? 0 : FLAG_ZERO)
#define F_SIGN_BY8(v)			(((v) & 0x80) ? FLAG_SIGN : 0)
#define F_CARRY_BY8(v)			(((v) & 0x100) ? FLAG_CARRY : 0)
#define F_OVERFLOW_BY8(ad1,ad2,res)	(((((ad1) & 0x80) == ((ad2) & 0x80)) && (((ad1) & 0x80) != ((res) & 0x80))) ? FLAG_PV : 0)

#define F_ZERO_BY16(v)			(((v) & 0xFFFF) ? 0 : FLAG_ZERO)
#define F_SIGN_BY16(v)			(((v) & 0x8000) ? FLAG_SIGN : 0)
#define F_CARRY_BY16(v)			(((v) & 0x10000) ? FLAG_CARRY : 0)
#define F_OVERFLOW_BY16(ad1,ad2,res)	(((((ad1) & 0x8000) == ((ad2) & 0x8000)) && (((ad1) & 0x8000) != ((res) & 0x8000))) ? FLAG_PV : 0)

#define F_PARITY_BY8(v)			yay
#define F_PARITY_BY16(v)		yay

#define F_HALFCARRY_BY8(ad1,ad2,res)	((((ad1) ^ (ad2) ^ (res)) & 16) ? FLAG_HC : 0)







// WARNING! Register number encoding used by Z8K is "logical" EXCEPT OF
// byte registers! This REG8INDEX can/must be used when/only 8 bit registers
// are accessed. In that case, this macro even handled an out-of range index.
#define REG8INDEX(index)	((((index)&7)<<1)|(((index)&8)>>3))


// Nibbles of the word from 3 (MS-nib) to 0
// I am still not sure this word "nibble" should be "nible" "nibble" "nyble" or "nybble" ...
#define NIB3(n)		(((n) >> 12) & 0xF)
#define NIB2(n)		(((n) >> 8) & 0xF)
#define NIB1(n)		(((n) >> 4) & 0xF)
#define NIB0(n)		((n) & 0xF)

#define OPCNIB3		NIB3(opc)
#define OPCNIB2		NIB2(opc)
#define OPCNIB1		NIB1(opc)
#define OPCNIB0		NIB0(opc)


static inline Uint8 GetReg8 ( int index ) // Get register value according to a 8 bit Z8K encoded register
{
	return (index & 8) ? (z8k1.regs[index & 7] & 0xFF) : (z8k1.regs[index & 7] >> 8);
}

static inline void SetReg8 ( int index, Uint8 data )
{
	if (index & 8)
		z8k1.regs[index & 7] = (z8k1.regs[index & 7] & 0xFF00) | data;
	else
		z8k1.regs[index & 7] = (z8k1.regs[index & 7] & 0x00FF) | (data << 8);
}

static inline Uint16 GetReg16 ( int index )
{
	return z8k1.regs[index & 0xF];
}

static inline void SetReg16 ( int index, Uint8 data )
{
	z8k1.regs[index & 0xF] = data;
}

static inline Uint32 GetReg32 ( int index )
{
	index &= 0xE;
	return ((Uint32)z8k1.regs[index] << 16) | (Uint32)z8k1.regs[index + 1];
}

static inline void SetReg32 ( int index, Uint32 data )
{
	index &= 0xE;
	z8k1.regs[index] = data >> 16;
	z8k1.regs[index + 1] = data & 0xFFFF;
}


#if 0
// that is tricky ... it seems, Z8K wastes a byte to have word aligned
// immediate byte value in the code,
// but it's unclear that the low or high byte is used actually to fill
// a register in case of a 8 bit immediate data "normally encoded".
// (Z8000 manual seems to indicate that both of low/high bytes should
// have the same value, so it gives the hint, that it's based on the
// actual 8 bit register used lo or hi 8 bit part of a register)
// I am just guessing that the low/hi usage is connected to the lo/hi
// 8 bit register selected ...
// Index is the encoded register number
static inline Uint8 GetVal8FromImmediateWord ( int index, Uint16 data )
{
	return (index & 8) ? (data & 0xFF) : (data >> 8);
}

#define IMMEDBYTEFROMWORD(index,n)	(((n)>>(((index)&8)?0:8))&0xFF)


static inline void SetReg8FromWord ( int index, Uint16 data )
{
	if (index & 8)
		z8k1.regs[index & 7] = (z8k1.regs[index & 7] & 0xFF00) | (data & 0x00FF);
	else
		z8k1.regs[index & 7] = (z8k1.regs[index & 7] & 0x00FF) | (data & 0xFF00);
}

static inline void SetReg16 ( int index, Uint16 data )
{
	index &= 0xE;
	z8k1.RegisterFile[index] = data >> 8;
	z8k1.RegisterFile[index + 1] = data & 0xFF;
}
#endif

#if 0
// that is tricky ... it seems, Z8K wastes a byte to have word aligned
// but it's unclear that the low or high byte is used actually to fill
// a register in case of an immediate data "normally encoded".
// I am just guessing that the low/hi usage is connected to the lo/hi
// 8 bit register selected ...
static inline void SetReg8FromCodeRead16 ( int index )
{
	Uint16 data = READCODE();
	index = REG8INDEX(index);
	RegisterFile[index] = (index & 1) ? (data & 0xFF) : (data >> 8);
}

static inline void SetReg16FromCodeRead ( int index )
{
	Uint16 data = READCODE();
	index &= 0xE;
	RegisterFile[index    ] = data >> 8;
	RegisterFile[index + 1] = data & 0xFF;
}

static inline void SetReg32FromCodeRead ( int index )
{
	Uint16 data = READCODE();
	index &= 0xC;
	RegisterFile[index    ] = data >> 8;
	RegisterFile[index + 1] = data & 0xFF;
	data = READCODE();
	RegisterFile[index + 2] = data >> 8;
	RegisterFile[index + 3] = data & 0xFF;
}
#endif

static inline Uint8 IncReg8 ( int index, int incval )
{
	Uint8 temp = incval + GetReg8(index);
	SetReg8(index, temp);
	return temp;
}


static inline Uint16 IncReg16 ( int index, int incval )
{
	z8k1.regs[index & 0xF] += incval;
	return z8k1.regs[index & 0xF];
}

#ifdef DO_DISASM
static char disasm_get_address_code[5+5+1];
static char disasm_get_address[5+3+1+1];
#endif


static void get_address ( void )
{
	if (IS_SEGMENTED_MODE) {
		// segmented mode
		int seg = READCODE();
		z8k1.use_seg = (seg >> 8) & 0x7F;
		if (seg & 0x8000) {
			z8k1.use_ofs = READCODE();
#ifdef DO_DISASM
			sprintf(disasm_get_address_code, "%04X %04X", seg, z8k1.use_ofs);
			sprintf(disasm_get_address, "$%02X:$%04X", z8k1.use_seg, z8k1.use_ofs);
#endif
			z8k1.get_address_mode = 1;
		} else {
			z8k1.use_ofs = seg & 0xFF;
#ifdef DO_DISASM
			sprintf(disasm_get_address_code, "%04X", seg);
			sprintf(disasm_get_address, "$%02X:$%02X", z8k1.use_seg, z8k1.use_ofs);
#endif
			z8k1.get_address_mode = 2;
		}
	} else {
		// nonsegmented mode, simple reads a word as the address
		z8k1.use_seg = 0;
		z8k1.use_ofs = READCODE();
#ifdef DO_DISASM
		sprintf(disasm_get_address_code, "%04X", z8k1.use_ofs);
		sprintf(disasm_get_address, "$%04X", z8k1.use_ofs);
#endif
		z8k1.get_address_mode = 0;
	}
}



#if 0
static Uint16 fetch_address ()
{
	if (IS_SEGMENTED_MODE) {
		Uint8 seg = READCODE_HI();
		if (seg & 0x80) {
			cycles_extra_fetch = 3;
			use_seg = seg & 0x7F;
			pc++;	// FIXME: ignore one byte with segmented-long offset
			Uint16 ret = READCODE_HI() << 8;
			return ret | READCODE_LO();
		} else {
			use_seg = seg;
			return READCODE_LO();
		}
	} else {
		use_seg = 0;	// FIXME: when non-segmented mode, it's the right way?
		Uint16 ret = READCODE_HI() << 8;
		return ret | READCODE_LO();
	}
}
#endif



static void set_fcw_byte ( Uint8 newfcw )
{
	newfcw &= FCW_ALL_MASK;
	// Check mode (system/normal) and "clone" stack pointer (+seg) to the normal register file
	// with backing up for the old mode. That is, RegisterFile always reflects the current CPU
	// mode, but separate stack pointer/seg is stored for both modes in my emulation.
	if ((newfcw & FCW_SYS) != (z8k1.fcw & FCW_SYS)) {
		if (newfcw & FCW_SYS) {	// CPU mode transition: normal -> system
			DEBUGPRINT("Z8000: FCW: CPU mode change: normal -> system" NL);
			// backup normal stack pointer + seg
			z8k1.stackptrusr = GetReg16(15);
			z8k1.stacksegusr = (GetReg16(14) >> 8) & 0x7F;
			// copy-in system stack pointer + seg
			SetReg16(15, z8k1.stackptrsys);
			SetReg16(14, z8k1.stacksegsys << 8);
		} else {			// CPU mode transition: system -> normal
			DEBUGPRINT("Z8000: FCW: CPU mode change: system -> normal" NL);
			// backip system stack pointer + seg
			z8k1.stackptrsys = GetReg16(15);
			z8k1.stacksegsys = (GetReg16(14) >> 8) & 0x7F;
			// copy-in normal stack pointer + seg
			SetReg16(15, z8k1.stackptrusr);
			SetReg16(14, z8k1.stacksegusr << 8);
		}
	}
	if ((newfcw & FCW_SEG) != (z8k1.fcw & FCW_SEG)) {
		DEBUGPRINT("Z8000: FCW: CPU segmented change: %s" NL, (newfcw & FCW_SEG) ? "ON" : "OFF");
	}
	if (newfcw != z8k1.fcw)
		DEBUGPRINT("Z8000: FCW: %02X -> %02X" NL, z8k1.fcw, newfcw);
	z8k1.fcw = newfcw;
}



void z8k1_reset ( void )
{
	memset(z8k1.regs, 0, sizeof z8k1.regs);
	z8k1.refresh = 0;	// refresh register
	z8k1.psaseg = 0;
	z8k1.psaofs = 0;
	// You should NEVER directly access these! Let these set_fcw_byte() to handle.
	// Teset is the sole exception of this rule, since set_fcw_byte() will clone from these,
	// and can be unititalized otherwise.
	// The other exception will be the privileges instruction involves normal (user mode)
	// stack pointer set/queried from system mode, see the LDCTL opcode emulations.
	z8k1.stacksegusr = 0;
	z8k1.stackptrusr = 0;
	z8k1.stacksegsys = 0;
	z8k1.stackptrsys = 0;
	// Z8K1 initializes some registers near the beginning of the system memory
	Uint16 data = z8k1_read_code_cb(0, 2);
	set_fcw_byte(data >> 8);	// FCW byte (hi byte of the word)
	// the CPU flags (actually FCW + flags form a word but anyway, I handle as two separated entities, easier and quickier to manipulate in opcode emulation,
	// since flags are often modified but the "real' FCW part is kinda limited what can modify it, only a few privileged opcodes, which must call
	// set_fcw_byte anyway to take care about normal/system mode transition and so on)
	// Flags has unused bits at pos 0,1. Be careful, documents often call FCW for the whole word, ie the lower byte being the "traditional" CPU flags!
	z8k1.flags = data & 0xFC;
	data = z8k1_read_code_cb(0, 4);
	z8k1.codeseg = (data >> 8) & 0x7F;	// code segment, Z8K1 uses 7 bits segment numbers! [low byte of this word is unused - AFAIK ...]
	z8k1.pc = z8k1_read_code_cb(0, 6);	// also initialize the PC itself
	printf("Z8000: reset -> FCW=$%02X%02X SEG=$%02X PC=$%04X\n" NL, z8k1.fcw, z8k1.flags, z8k1.codeseg, z8k1.pc);
}

void z8k1_init ( void )
{
}

int z8k1_step ( int cycles_limit )
{
	int cycles = 0;
	do {
	int pc_orig = z8k1.pc;
	//z8k1.opfetchcycle = 0;
	Uint16 opc = READCODE();
	switch (opc >> 8) {
	// the seems to be crazy order of opcodes has a reason: the opcode
	// construction of Z8K, to keep opcodes closer with different addr.modes ...
	// this is not always makes things that way, but in mode cases it does

	/*************************** OPC-HI = $00 ***************************/
	case 0x00:
	// ADDB	Rbd,#data		|0 0|0 0 0 0 0 0|0 0 0 0| Rbd
	// ADDB Rd,@Rs {BYTE}		|0 0|0 0 0 0 0 0| Rs!=0 | Rd
	NOT_EMULATED_OPCODE();
	break;

	/*************************** OPC-HI = $01 ***************************/
	case 0x01:
	// ADD	Rd,#data		|0 0|0 0 0 0 0 1|0 0 0 0| Rd
	// ADD	Rd,@Rs {WORD}		|0 0|0 0 0 0 0 1| Rs!=0 | Rd
	NOT_EMULATED_OPCODE();
	break;

	/*************************** OPC-HI = $40 ***************************/
	case 0x40:
	// ADDB Rd,address {BYTE}	|0 1|0 0 0 0 0 0|0 0 0 0| Rd
	// ADDB Rd,addr(Rs) {BYTE}	|0 1|0 0 0 0 0 0| Rs!=0	| Rd
	NOT_EMULATED_OPCODE();
	break;

	/*************************** OPC-HI = $41 ***************************/
	case 0x41:
	// ADD	Rd,address {WORD}	|0 1|0 0 0 0 0 1|0 0 0 0| Rd
	// ADD	Rd,addr(Rs) {WORD}	|0 1|0 0 0 0 0 1| Rs!=0	| Rd
	NOT_EMULATED_OPCODE();
	break;

	/*************************** OPC-HI = $80 ***************************/
	case 0x80:
	// ADDB Rd,Rs {BYTE}		|1 0|0 0 0 0 0 0| Rs    | Rd
	NOT_EMULATED_OPCODE();
	break;

	/*************************** OPC-HI = $81 ***************************/
	case 0x81:
	// ADD	Rd,Rs {WORD}		|1 0|0 0 0 0 0 1| Rs    | Rd
	NOT_EMULATED_OPCODE();
	break;

	/*************************** OPC-HI = $02 ***************************/
	case 0x02:
	// SUBB	Rbd,#data		|0 0|0 0 0 0 1 0|0 0 0 0| Rbd
	// SUB	Rd,@Rs {BYTE}		|0 0|0 0 0 0 1 0| Rs!=0 | Rd
	NOT_EMULATED_OPCODE();
	break;

	/*************************** OPC-HI = $03 ***************************/
	case 0x03:
	// SUB	Rd,#data		|0 0|0 0 0 0 1 1|0 0 0 0| Rd
	// SUB	Rd,@Rs {WORD}		|0 0|0 0 0 0 1 1| Rs!=0 | Rd
	NOT_EMULATED_OPCODE();
	break;

	/*************************** OPC-HI = $42 ***************************/
	case 0x42:
	// SUBB	Rd,address {BYTE}	|0 1|0 0 0 0 1 0|0 0 0 0| Rd
	// SUBB	Rd,addr(Rs) {BYTE}	|0 1|0 0 0 0 1 0| Rs!=0 | Rd
	NOT_EMULATED_OPCODE();
	break;

	/*************************** OPC-HI = $43 ***************************/
	case 0x43:
	// SUB	Rd,address {WORD}	|0 1|0 0 0 0 1 1|0 0 0 0| Rd
	// SUB	Rd,addr(Rs) {WORD}	|0 1|0 0 0 0 1 1| Rs!=0 | Rd
	NOT_EMULATED_OPCODE();
	break;

	/*************************** OPC-HI = $82 ***************************/
	case 0x82:
	// SUBB	Rd,Rs {BYTE}		|1 0|0 0 0 0 1 0| Rs    | Rd
	NOT_EMULATED_OPCODE();
	break;

	/*************************** OPC-HI = $83 ***************************/
	case 0x83:
	// SUB	Rd,Rs {WORD}		|1 0|0 0 0 0 1 1| Rs    | Rd
	DISASM("|%s\t%s,%s", "SUB", reg16names(OPCNIB0), reg16names(OPCNIB1));
	if (DO_EXEC) {
		int s = GetReg16(OPCNIB1), d = GetReg16(OPCNIB0), r = d - s;
		z8k1.flags = (z8k1.flags & (FLAG_DA | FLAG_HC)) | F_CARRY_BY16(r) | F_ZERO_BY16(r) | F_SIGN_BY16(r) | F_OVERFLOWSUB_BY16(s,d,r); // FIXME: overflow on SUB is different!!!!!!!
		SetReg16(OPCNIB0, r);
		cycles += 4;
	}
	break;

	/*************************** OPC-HI = $04 ***************************/
	case 0x04:
	// ORB	Rbd,#data		|0 0|0 0 0 1 0 0|0 0 0 0| Rbd
	// ORB Rd,@Rs {BYTE}		|0 0|0 0 0 1 0 0| Rs!=0 | Rd
	NOT_EMULATED_OPCODE();
	break;

	/*************************** OPC-HI = $05 ***************************/
	case 0x05:
	// OR	Rd,#data		|0 0|0 0 0 1 0 1|0 0 0 0| Rd
	// OR	Rd,@Rs {WORD}		|0 0|0 0 0 1 0 1| Rs!=0 | Rd
	NOT_EMULATED_OPCODE();
	break;

	/*************************** OPC-HI = $44 ***************************/
	case 0x44:
	// ORB Rd,address {BYTE}	|0 1|0 0 0 1 0 0|0 0 0 0| Rd
	// ORB Rd,addr(Rs) {BYTE}	|0 1|0 0 0 1 0 0| Rs!=0 | Rd
	NOT_EMULATED_OPCODE();
	break;

	/*************************** OPC-HI = $45 ***************************/
	case 0x45:
	// OR	Rd,address {WORD}	|0 1|0 0 0 1 0 1|0 0 0 0| Rd
	// OR	Rd,addr(Rs) {WORD}	|0 1|0 0 0 1 0 1| Rs!=0 | Rd
	NOT_EMULATED_OPCODE();
	break;

	/*************************** OPC-HI = $84 ***************************/
	case 0x84:
	// ORB Rd,Rs {BYTE}		|1 0|0 0 0 1 0 0| Rs    | Rd
	NOT_EMULATED_OPCODE();
	break;

	/*************************** OPC-HI = $85 ***************************/
	case 0x85:
	// OR	Rd,Rs {WORD}		|1 0|0 0 0 1 0 1| Rs    | Rd
	NOT_EMULATED_OPCODE();
	break;

	/*************************** OPC-HI = $06 ***************************/
	case 0x06:
	// ANDB	Rbd,#data		|0 0|0 0 0 1 1 0|0 0 0 0| Rbd
	// ANDB Rd,@Rs {BYTE}		|0 0|0 0 0 1 1 0| Rs!=0 | Rd
	NOT_EMULATED_OPCODE();
	break;

	/*************************** OPC-HI = $07 ***************************/
	case 0x07:
	// AND	Rd,#data		|0 0|0 0 0 1 1 1|0 0 0 0| Rd
	// AND	Rd,@Rs {WORD}		|0 0|0 0 0 1 1 1| Rs!=0 | Rd
	NOT_EMULATED_OPCODE();
	break;

	/*************************** OPC-HI = $46 ***************************/
	case 0x46:
	// ANDB Rd,address {BYTE}	|0 1|0 0 0 1 1 0|0 0 0 0| Rd
	// ANDB Rd,addr(Rs) {BYTE}	|0 1|0 0 0 1 1 0| Rs!=0	| Rd
	NOT_EMULATED_OPCODE();
	break;

	/*************************** OPC-HI = $47 ***************************/
	case 0x47:
	// AND	Rd,address {WORD}	|0 1|0 0 0 1 1 1|0 0 0 0| Rd
	// AND	Rd,addr(Rs) {WORD}	|0 1|0 0 0 1 1 1| Rs!=0	| Rd
	NOT_EMULATED_OPCODE();
	break;

	/*************************** OPC-HI = $86 ***************************/
	case 0x86:
	// ANDB Rd,Rs {BYTE}		|1 0|0 0 0 1 1 0| Rs    | Rd
	NOT_EMULATED_OPCODE();
	break;

	/*************************** OPC-HI = $87 ***************************/
	case 0x87:
	// AND	Rd,Rs {WORD}		|1 0|0 0 0 1 1 1| Rs    | Rd
	NOT_EMULATED_OPCODE();
	break;

	/*************************** OPC-HI = $08 ***************************/
	case 0x08:
	// XORB	Rbd,#data		|0 0|0 0 1 0 0 0|0 0 0 0| Rbd
	// XORB Rd,@Rs {BYTE}		|0 0|0 0 1 0 0 0| Rs!=0 | Rd
	NOT_EMULATED_OPCODE();
	break;

	/*************************** OPC-HI = $09 ***************************/
	case 0x09:
	// XOR	Rd,#data		|0 0|0 0 1 0 0 1|0 0 0 0| Rd
	// XOR	Rd,@Rs {WORD}		|0 0|0 0 1 0 0 1| Rs!=0 | Rd
	NOT_EMULATED_OPCODE();
	break;

	/*************************** OPC-HI = $48 ***************************/
	case 0x48:
	// XORB Rd,address {BYTE}	|0 1|0 0 1 0 0 0|0 0 0 0| Rd
	// XORB Rd,addr(Rs) {BYTE}	|0 1|0 0 1 0 0 0| Rs!=0 | Rd
	NOT_EMULATED_OPCODE();
	break;

	/*************************** OPC-HI = $49 ***************************/
	case 0x49:
	// XOR	Rd,address {WORD}	|0 1|0 0 1 0 0 1|0 0 0 0| Rd
	// XOR	Rd,addr(Rs) {WORD}	|0 1|0 0 1 0 0 1| Rs!=0 | Rd
	NOT_EMULATED_OPCODE();
	break;

	/*************************** OPC-HI = $88 ***************************/
	case 0x88:
	// XORB Rd,Rs {BYTE}		|1 0|0 0 1 0 0 0| Rs    | Rd
	NOT_EMULATED_OPCODE();
	break;

	/*************************** OPC-HI = $89 ***************************/
	case 0x89:
	// XOR	Rd,Rs {WORD}		|1 0|0 0 1 0 0 1| Rs    | Rd
	NOT_EMULATED_OPCODE();
	break;

	/*************************** OPC-HI = $0A ***************************/
	case 0x0A:
	// CPB	Rbd,#data		|0 0|0 0 1 0 1 0|0 0 0 0| Rbd
	// CPB Rd,@Rs {BYTE}		|0 0|0 0 1 0 1 0| Rs!=0 | Rd
	NOT_EMULATED_OPCODE();
	break;

	/*************************** OPC-HI = $0B ***************************/
	case 0x0B:
	// CP	Rd,#data		|0 0|0 0 1 0 1 1|0 0 0 0| Rd
	// CP	Rd,@Rs {WORD}		|0 0|0 0 1 0 1 1| Rs!=0 | Rd
	NOT_EMULATED_OPCODE();
	break;

	/*************************** OPC-HI = $4A ***************************/
	case 0x4A:
	// CPB Rd,address {BYTE}	|0 1|0 0 1 0 1 0|0 0 0 0| Rd
	// CPB Rd,addr(Rs) {BYTE}	|0 1|0 0 1 0 1 0| Rs!=0	| Rd
	NOT_EMULATED_OPCODE();
	break;

	/*************************** OPC-HI = $4B ***************************/
	case 0x4B:
	// CP	Rd,address {WORD}	|0 1|0 0 1 0 1 1|0 0 0 0| Rd
	// CP	Rd,addr(Rs) {WORD}	|0 1|0 0 1 0 1 1| Rs!=0	| Rd
	NOT_EMULATED_OPCODE();
	break;

	/*************************** OPC-HI = $8A ***************************/
	case 0x8A:
	// CPB Rd,Rs {BYTE}		|1 0|0 0 1 0 1 0| Rs    | Rd
	NOT_EMULATED_OPCODE();
	break;

	/*************************** OPC-HI = $8B ***************************/
	case 0x8B:
	// CP	Rd,Rs {WORD}		|1 0|0 0 1 0 1 1| Rs    | Rd
	NOT_EMULATED_OPCODE();
	break;

	/*************************** OPC-HI = $0C ***************************/
	case 0x0C:
	// CLRB @Rd {BYTE}		|0 0|0 0 1 1 0 0| Rd!=0 |1 0 0 0
	// COMB @Rd {BYTE}		|0 0|0 0 1 1 0 0| Rd!=0 |0 0 0 0
	// CPB	@Rd,#data		|0 0|0 0 1 1 0|0| Rd!=0 |0 0 0 1
	// LDB	@Rd,#data		|0 0|0 0 1 1 0 0| Rd!=0 |0 1 0 1
	// NEGB @Rd {BYTE}		|0 0|0 0 1 1 0 0| Rd!=0 |0 0 1 0
	// TESTB @Rd {BYTE}		|0 0|0 0 1 1 0 0| Rd!=0 |0 1 0 0
	// TSETB @Rd {BYTE}		|0 0|0 0 1 1 0 0| Rd!=0 |0 1 1 0
	NOT_EMULATED_OPCODE();
	break;

	/*************************** OPC-HI = $0D ***************************/
	case 0x0D:
	// CLR	@Rd {WORD}		|0 0|0 0 1 1 0 1| Rd!=0 |1 0 0 0
	// COM	@Rd {WORD}		|0 0|0 0 1 1 0 1| Rd!=0 |0 0 0 0
	// CP	@Rd,#data		|0 0|0 0 1 1 0|1| Rd!=0	|0 0 0 1
	// LD	@Rd,#data		|0 0|0 0 1 1 0 1| Rd!=0 |0 1 0 1
	// NEG	@Rd {WORD}		|0 0|0 0 1 1 0 1| Rd!=0 |0 0 1 0
	// PUSH	@Rd,#data		|0 0|0 0 1 1 0 1| Rd!=0 |1 0 0 1
	// TEST	@Rd {WORD}		|0 0|0 0 1 1 0 1| Rd!=0 |0 1 0 0
	// TSET	@Rd {WORD}		|0 0|0 0 1 1 0 1| Rd!=0 |0 1 1 0
	NOT_EMULATED_OPCODE();
	break;

	/*************************** OPC-HI = $4C ***************************/
	case 0x4C:
	// CLRB address {BYTE}		|0 1|0 0 1 1 0 0|0 0 0 0|1 0 0 0
	// CLRB addr(Rd) {BYTE}		|0 1|0 0 1 1 0 0| Rd!=0	|1 0 0 0
	// COMB address {BYTE}		|0 1|0 0 1 1 0 0|0 0 0 0|0 0 0 0
	// COMB addr(Rd) {BYTE}		|0 1|0 0 1 1 0 0| Rd!=0	|0 0 0 0
	// CPB	address,#data		|0 1|0 0 1 1 0|0|0 0 0 0|0 0 0 1
	// CPB	addr(Rd),#data		|0 1|0 0 1 1 0|0| Rd!=0 |0 0 0 1
	// LDB	address,#data		|0 1|0 0 1 1 0 0|0 0 0 0|0 1 0 1
	// LDB	addr(Rd),#data		|0 1|0 0 1 1 0 0| Rd!=0 |0 1 0 1
	// NEGB address {BYTE}		|0 1|0 0 1 1 0 0|0 0 0 0|0 0 1 0
	// NEGB addr(Rd) {BYTE}		|0 1|0 0 1 1 0 0| Rd!=0 |0 0 1 0
	// TESTB address {BYTE}		|0 1|0 0 1 1 0 0|0 0 0 0|0 1 0 0
	// TESTB addr(Rd) {BYTE}	|0 1|0 0 1 1 0 0| Rd!=0 |0 1 0 0
	// TSETB address {BYTE}		|0 1|0 0 1 1 0 0|0 0 0 0|0 1 1 0
	// TSETB addr(Rd) {BYTE}	|0 1|0 0 1 1 0 0| Rd!=0 |0 1 1 0
	NOT_EMULATED_OPCODE();
	break;

	/*************************** OPC-HI = $4D ***************************/
	case 0x4D:
	// CLR	address {WORD}		|0 1|0 0 1 1 0 1|0 0 0 0|1 0 0 0
	// CLR	addr(Rd) {WORD}		|0 1|0 0 1 1 0 1| Rd!=0	|1 0 0 0
	// COM	address {WORD}		|0 1|0 0 1 1 0 1|0 0 0 0|0 0 0 0
	// COM	addr(Rd) {WORD}		|0 1|0 0 1 1 0 1| Rd!=0	|0 0 0 0
	// CP	address,#data		|0 1|0 0 1 1 0|1|0 0 0 0|0 0 0 1
	// CP	addr(Rd),#data		|0 1|0 0 1 1 0|1| Rd!=0 |0 0 0 1
	// LD	address,#data		|0 1|0 0 1 1 0 1|0 0 0 0|0 1 0 1
	// LD	addr(Rd),#data		|0 1|0 0 1 1 0 1| Rd!=0 |0 1 0 1
	// NEG	address {WORD}		|0 1|0 0 1 1 0 1|0 0 0 0|0 0 1 0
	// NEG	addr(Rd) {WORD}		|0 1|0 0 1 1 0 1| Rd!=0 |0 0 1 0
	// TEST	address {WORD}		|0 1|0 0 1 1 0 1|0 0 0 0|0 1 0 0
	// TEST	addr(Rd) {WORD}		|0 1|0 0 1 1 0 1| Rd!=0 |0 1 0 0
	// TSET	address {WORD}		|0 1|0 0 1 1 0 1|0 0 0 0|0 1 1 0
	// TSET	addr(Rd) {WORD}		|0 1|0 0 1 1 0 1| Rd!=0 |0 1 1 0
	NOT_EMULATED_OPCODE();
	break;

	/*************************** OPC-HI = $8C ***************************/
	case 0x8C:
	// CLRB Rd {BYTE}		|1 0|0 0 1 1 0 0| Rd    |1 0 0 0
	// COMB Rd {BYTE}		|1 0|0 0 1 1 0 0| Rd    |0 0 0 0
	// LDCTLB FLAGS,Rbs		|1 0 0 0 1 1 0 0| Rbs   |1 0 0 1
	// LDCTLB Rbd,FLAGS		|1 0 0 0 1 1 0 0| Rbd	|0 0 0 1
	// NEGB Rd {BYTE}		|1 0|0 0 1 1 0 0| Rd    |0 0 1 0
	// TESTB Rd {BYTE}		|1 0|0 0 1 1 0 0| Rd    |0 1 0 0
	// TSETB Rd {BYTE}		|1 0|0 0 1 1 0 0| Rd    |0 1 1 0
	NOT_EMULATED_OPCODE();
	break;

	/*************************** OPC-HI = $8D ***************************/
	case 0x8D:
	// CLR	Rd {WORD}		|1 0|0 0 1 1 0 1| Rd    |1 0 0 0
	// COM	Rd {WORD}		|1 0|0 0 1 1 0 1| Rd    |0 0 0 0
	// COMFLG	flags		|1 0 0 0 1 1 0 1| flags |0 1 0 1
	// NEG	Rd {WORD}		|1 0|0 0 1 1 0 1| Rd    |0 0 1 0
	// NOP				|1 0 0 0 1 1 0 1|0 0 0 0 0 1 1 1
	// RESFLG flags			|1 0|0 0 1 1 0 1| flags |0 0 1 1
	// SETFLG flags			|1 0 0 0 1 1 0 1| flags |0 0 0 1
	// TEST	Rd {WORD}		|1 0|0 0 1 1 0 1| Rd    |0 1 0 0
	// TSET	Rd {WORD}		|1 0|0 0 1 1 0 1| Rd    |0 1 1 0
	NOT_EMULATED_OPCODE();
	break;

	/*************************** OPC-HI = $0E ***************************/
	case 0x0E:
	RESERVED_OPCODE();
	break;

	/*************************** OPC-HI = $0F ***************************/
	case 0x0F:
	// EPU2MEM @Rd,#n		|0 0|0 0 1 1 1 1| Rd!=0 |1 1|x x|x x x x x x x x x x x x| n-1
	// MEM2EPU @Rd,#n		|0 0|0 0 1 1 1 1| Rd!=0 |0 1|x x|x x x x x x x x x x x x| n-1
	NOT_EMULATED_OPCODE();
	break;

	/*************************** OPC-HI = $4E ***************************/
	case 0x4E:
	RESERVED_OPCODE();
	break;

	/*************************** OPC-HI = $4F ***************************/
	case 0x4F:
	// EPU2MEM	addr(Rd),#n	|0 1|0 0 1 1 1 1| Rd!=0 |1 1|x x|x x x x x x x x x x x x| n-1
	// EPU2MEM	address,#n	|0 1|0 0 1 1 1 1|0 0 0 0|1 1|x x|x x x x x x x x x x x x| n-1
	// MEM2EPU	addr(Rd),#n	|0 1|0 0 1 1 1 1| Rd!=0 |0 1|x x|x x x x x x x x x x x x| n-1
	// MEM2EPU	address,#n	|0 1|0 0 1 1 1 1|0 0 0 0|0 1|x x|x x x x x x x x x x x x| n-1
	NOT_EMULATED_OPCODE();
	break;

	/*************************** OPC-HI = $8E ***************************/
	case 0x8E:
	// FCW2EPU			|1 0|0 0 1 1 1 0|x x x x|1 0|x x|x x x x|0 0 0 0|x x x x|0 0 0 0
	// EPUINT #n			|1 0|0 0 1 1 1 0|x x x x|0 1|x x x x x x x x x x x x x x| n-1
	NOT_EMULATED_OPCODE();
	break;

	/*************************** OPC-HI = $8F ***************************/
	case 0x8F:
	// EPU2CPU Rd,#n		|1 0|0 0 1 1 1 1|0|x x x|0 0|x x|x x x x| Rd    |x x x x| n-1
	// CPU2EPU Rs,#n		|1 0|0 0 1 1 1 1|0|x x x|1 0|x x|x x x x| Rs    |x x x x| n-1
	// EPU2FCW			|1 0|0 0 1 1 1 1|x x x x|0 0|x x|x x x x|0 0 0 0|x x x x|0 0 0 0
	NOT_EMULATED_OPCODE();
	break;

	/*************************** OPC-HI = $10 ***************************/
	case 0x10:
	// CPL	RRd,#data		|0 0|0 1 0 0 0 0|0 0 0 0| RRd
	// CPL	RRd,@Rs			|0 0|0 1 0 0 0 0| Rs!=0 | RRd
	NOT_EMULATED_OPCODE();
	break;

	/*************************** OPC-HI = $11 ***************************/
	case 0x11:
	// PUSHL	@Rd,@Rs		|0 0|0 1 0 0 0 1| Rd!=0 | Rs!=0
	NOT_EMULATED_OPCODE();
	break;

	/*************************** OPC-HI = $50 ***************************/
	case 0x50:
	// CPL	RRd,address		|0 1|0 1 0 0 0 0|0 0 0 0| RRd
	// CPL	RRd,addr(Rs)		|0 1|0 1 0 0 0 0| Rs!=0	| RRd
	NOT_EMULATED_OPCODE();
	break;

	/*************************** OPC-HI = $51 ***************************/
	case 0x51:
	// PUSHL	@Rd,address	|0 1|0 1 0 0 0 1| Rd!=0 |0 0 0 0
	// PUSHL	@Rd,addr(Rs)	|0 1|0 1 0 0 0 1| Rd!=0 | Rs!=0
	NOT_EMULATED_OPCODE();
	break;

	/*************************** OPC-HI = $90 ***************************/
	case 0x90:
	// CPL	RRd,RRs			|1 0|0 1 0 0 0 0| RRs   | RRd
	NOT_EMULATED_OPCODE();
	break;

	/*************************** OPC-HI = $91 ***************************/
	case 0x91:
	// PUSHL @Rd,RRs		|1 0|0 1 0 0 0 1| Rd!=0 | RRs
	NOT_EMULATED_OPCODE();
	break;

	/*************************** OPC-HI = $12 ***************************/
	case 0x12:
	// SUBL	RRd,#data		|0 0|0 1 0 0 1 0|0 0 0 0| RRd
	// SUBL	RRd,@Rs			|0 0|0 1 0 0 1 0| Rs!=0 | RRd
	NOT_EMULATED_OPCODE();
	break;

	/*************************** OPC-HI = $13 ***************************/
	case 0x13:
	// PUSH	@Rd,@Rs			|0 0|0 1 0 0 1 1| Rd!=0 | Rs!=0
	NOT_EMULATED_OPCODE();
	break;

	/*************************** OPC-HI = $52 ***************************/
	case 0x52:
	// SUBL	RRd,address		|0 1|0 1 0 0 1 0|0 0 0 0| RRd
	// SUBL	RRd,addr(Rs)		|0 1|0 1 0 0 1 0| Rs!=0 | RRd
	NOT_EMULATED_OPCODE();
	break;

	/*************************** OPC-HI = $53 ***************************/
	case 0x53:
	// PUSH	@Rd,address		|0 1|0 1 0 0 1 1| Rd!=0 |0 0 0 0
	// PUSH	@Rd,addr(Rs)		|0 1|0 1 0 0 1 1| Rd!=0 | Rs!=0
	NOT_EMULATED_OPCODE();
	break;

	/*************************** OPC-HI = $92 ***************************/
	case 0x92:
	// SUBL	RRd,RRs			|1 0|0 1 0 0 1 0| RRs   | RRd
	NOT_EMULATED_OPCODE();
	break;

	/*************************** OPC-HI = $93 ***************************/
	case 0x93:
	// PUSH	@Rd,Rs			|1 0|0 1 0 0 1 1| Rd!=0 | Rs
	NOT_EMULATED_OPCODE();
	break;

	/*************************** OPC-HI = $14 ***************************/
	case 0x14:
	// LDL	RRd,@Rs			|0 0|0 1 0 1 0 0| Rs!=0 | RRd
	// LDL	RRd,#data		|0 0|0 1 0 1 0 0|0 0 0 0| RRd
	if (OPCNIB1) {
		NOT_EMULATED_OPCODE_VARIANT();
	} else {
		Uint32 val = READCODE() << 16;
		val |= READCODE();
		DISASM("%04X %04X|%s\t%s,#$%08X", val >> 16, val & 0xFFFF, "LDL", reg32names(OPCNIB0), val);
		if (DO_EXEC) {
			SetReg32(OPCNIB0, val);
			cycles += 11;
		}
	}
	break;

	/*************************** OPC-HI = $15 ***************************/
	case 0x15:
	// POPL	@Rd,@Rs			|0 0|0 1 0 1 0 1| Rs!=0 | Rd!=0
	NOT_EMULATED_OPCODE();
	break;

	/*************************** OPC-HI = $54 ***************************/
	case 0x54:
	// LDL	RRd,address		|0 1|0 1 0 1 0 0|0 0 0 0| RRd
	// LDL	RRd,addr(Rs)		|0 1|0 1 0 1 0 0| Rs!=0 | RRd
	NOT_EMULATED_OPCODE();
	break;

	/*************************** OPC-HI = $55 ***************************/
	case 0x55:
	// POPL	address,@Rs		|0 1|0 1 0 1 0 1| Rs!=0 |0 0 0 0
	// POPL	addr(Rd),@Rs		|0 1|0 1 0 1 0 1| Rs!=0 | Rd!=0
	NOT_EMULATED_OPCODE();
	break;

	/*************************** OPC-HI = $94 ***************************/
	case 0x94:
	// LDL	RRd,RRs			|1 0|0 1 0 1 0 0| RRs   | RRd
	NOT_EMULATED_OPCODE();
	break;

	/*************************** OPC-HI = $95 ***************************/
	case 0x95:
	// POPL	RRd,@Rs			|1 0|0 1 0 1 0 1| Rs!=0 | RRd
	NOT_EMULATED_OPCODE();
	break;

	/*************************** OPC-HI = $16 ***************************/
	case 0x16:
	// ADDL	RRd,#data		|0 0|0 1 0 1 1 0|0 0 0 0| RRd
	// ADDL	RRd,@Rs			|0 0|0 1 0 1 1 0| Rs!=0 | RRd
	NOT_EMULATED_OPCODE();
	break;

	/*************************** OPC-HI = $17 ***************************/
	case 0x17:
	// POP	@Rd,@Rs			|0 0|0 1 0 1 1 1| Rs!=0 | Rd!=0
	NOT_EMULATED_OPCODE();
	break;

	/*************************** OPC-HI = $56 ***************************/
	case 0x56:
	// ADDL	RRd,address		|0 1|0 1 0 1 1 0|0 0 0 0| RRd
	// ADDL	RRd,addr(Rs)		|0 1|0 1 0 1 1 0| Rs!=0	| RRd
	NOT_EMULATED_OPCODE();
	break;

	/*************************** OPC-HI = $57 ***************************/
	case 0x57:
	// POP	address,@Rs		|0 1|0 1 0 1 1 1| Rs!=0 |0 0 0 0
	// POP	addr(Rd),@Rs		|0 1|0 1 0 1 1 1| Rs!=0 | Rd!=0
	NOT_EMULATED_OPCODE();
	break;

	/*************************** OPC-HI = $96 ***************************/
	case 0x96:
	// ADDL	RRd,RRs			|1 0|0 1 0 1 1 0| RRs   | RRd
	NOT_EMULATED_OPCODE();
	break;

	/*************************** OPC-HI = $97 ***************************/
	case 0x97:
	// POP	Rd,@Rs			|1 0|0 1 0 1 1 1| Rs!=0 | Rd
	NOT_EMULATED_OPCODE();
	break;

	/*************************** OPC-HI = $18 ***************************/
	case 0x18:
	// MULTL RQd,#data		|0 0|0 1 1 0 0 0|0 0 0 0| RQd
	// MULTL RQd,@Rs		|0 0|0 1 1 0 0 0| Rs!=0 | RQd
	NOT_EMULATED_OPCODE();
	break;

	/*************************** OPC-HI = $19 ***************************/
	case 0x19:
	// MULT	RRd,#data		|0 0|0 1 1 0 0 1|0 0 0 0| RRd
	// MULT	RRd,@Rs			|0 0|0 1 1 0 0 1| Rs!=0 | RRd
	NOT_EMULATED_OPCODE();
	break;

	/*************************** OPC-HI = $58 ***************************/
	case 0x58:
	// MULTL RQd,address		|0 1|0 1 1 0 0 0|0 0 0 0| RQd
	// MULTL RQd,addr(Rs)		|0 1|0 1 1 0 0 0| Rs!=0 | RQd
	NOT_EMULATED_OPCODE();
	break;

	/*************************** OPC-HI = $59 ***************************/
	case 0x59:
	// MULT	RRd,address		|0 1|0 1 1 0 0 1|0 0 0 0| RRd
	// MULT	RRd,addr(Rs)		|0 1|0 1 1 0 0 1| Rs!=0 | RRd
	NOT_EMULATED_OPCODE();
	break;

	/*************************** OPC-HI = $98 ***************************/
	case 0x98:
	// MULTL RQd,RRs		|1 0|0 1 1 0 0 0| Rs    | RRd
	NOT_EMULATED_OPCODE();
	break;

	/*************************** OPC-HI = $99 ***************************/
	case 0x99:
	// MULT	RRd,Rs			|1 0|0 1 1 0 0 1| Rs    | RRd
	NOT_EMULATED_OPCODE();
	break;

	/*************************** OPC-HI = $1A ***************************/
	case 0x1A:
	// DIVL	RQd,#data		|0 0|0 1 1 0 1 0|0 0 0 0| RQd
	// DIVL	RQd,@Rs			|0 0|0 1 1 0 1 0| Rs!=0 | RQd
	NOT_EMULATED_OPCODE();
	break;

	/*************************** OPC-HI = $1B ***************************/
	case 0x1B:
	// DIV	RRd,#data		|0 0|0 1 1 0 1 1|0 0 0 0| RRd
	// DIV	RRd,@Rs			|0 0|0 1 1 0 1 1| Rs!=0 | RRd
	NOT_EMULATED_OPCODE();
	break;

	/*************************** OPC-HI = $5A ***************************/
	case 0x5A:
	// DIVL	RQD,address		|0 1|0 1 1 0 1 0|0 0 0 0| RQd
	// DIVL	RQd,addr(Rs)		|0 1|0 1 1 0 1 0| Rs!=0 | RQd
	NOT_EMULATED_OPCODE();
	break;

	/*************************** OPC-HI = $5B ***************************/
	case 0x5B:
	// DIV	RRd,address		|0 1|0 1 1 0 1 1|0 0 0 0| RRd
	// DIV	RRd,addr(Rs)		|0 1|0 1 1 0 1 1| Rs!=0 | RRd
	NOT_EMULATED_OPCODE();
	break;

	/*************************** OPC-HI = $9A ***************************/
	case 0x9A:
	// DIVL	RQd,RRs			|1 0|0 1 1 0 1 0| RRs   | RQd
	NOT_EMULATED_OPCODE();
	break;

	/*************************** OPC-HI = $9B ***************************/
	case 0x9B:
	// DIV	RRd,Rs			|1 0|0 1 1 0 1 1| Rs    | RRd
	NOT_EMULATED_OPCODE();
	break;

	/*************************** OPC-HI = $1C ***************************/
	case 0x1C:
	// LDM	Rd,@Rs,#n		|0 0|0 1 1 1 0 0| Rs!=0 |0 0 0 1|0 0 0 0| Rd    |0 0 0 0| n-1
	// LDM	@Rd,Rs,#n		|0 0|0 1 1 1 0 0| Rd!=0 |1 0 0 1|0 0 0 0| Rs    |0 0 0 0| n-1
	// TESTL	@Rd		|0 0|0 1 1 1 0 0| Rd!=0 |1 0 0 0
	NOT_EMULATED_OPCODE();
	break;

	/*************************** OPC-HI = $1D ***************************/
	case 0x1D:
	// LDL	@Rd,RRs			|0 0|0 1 1 1 0 1| Rd!=0 | RRs
	NOT_EMULATED_OPCODE();
	break;

	/*************************** OPC-HI = $5C ***************************/
	case 0x5C:
	// LDM	Rd,address,#n		|0 1|0 1 1 1 0 0|0 0 0 0|0 0 0 1|0 0 0 0| Rd    |0 0 0 0| n-1
	// LDM	Rd,addr(Rs),#n		|0 1|0 1 1 1 0 0| Rs!=0 |0 0 0 1|0 0 0 0| Rd    |0 0 0 0| n-1
	// LDM	address,Rs,#n		|0 1|0 1 1 1 0 0|0 0 0 0|1 0 0 1|0 0 0 0| Rs    |0 0 0 0| n-1
	// LDM	addr(Rd),Rs,#n		|0 1|0 1 1 1 0 0| Rd!=0 |1 0 0 1|0 0 0 0| Rs    |0 0 0 0| n-1
	// TESTL	address		|0 1|0 1 1 1 0 0|0 0 0 0|1 0 0 0
	// TESTL	addr(Rd)	|0 1|0 1 1 1 0 0| Rd!=0 |1 0 0 0
	NOT_EMULATED_OPCODE();
	break;

	/*************************** OPC-HI = $5D ***************************/
	case 0x5D:
	// LDL	address,RRs		|0 1|0 1 1 1 0 1|0 0 0 0| RRs
	// LDL	addr(Rd),RRs		|0 1|0 1 1 1 0 1| Rd!=0 | RRs
	NOT_EMULATED_OPCODE();
	break;

	/*************************** OPC-HI = $9C ***************************/
	case 0x9C:
	// TESTL	RRd		|1 0|0 1 1 1 0 0| RRd   |1 0 0 0
	NOT_EMULATED_OPCODE();
	break;

	/*************************** OPC-HI = $9D ***************************/
	case 0x9D:
	RESERVED_OPCODE();
	break;

	/*************************** OPC-HI = $1E ***************************/
	case 0x1E:
	// JP	cc,@Rd			|0 0|0 1 1 1 1 0| Rd!=0 | cc
	// in segmented mode: RRd!
	NOT_EMULATED_OPCODE();
	break;

	/*************************** OPC-HI = $1F ***************************/
	case 0x1F:
	// CALL	@Rd			|0 0|0 1 1 1 1 1| Rd!=0 |0 0 0 0
	NOT_EMULATED_OPCODE();
	break;

	/*************************** OPC-HI = $5E ***************************/
	case 0x5E:
	// JP	cc,address		|0 1|0 1 1 1 1 0|0 0 0 0| cc
	// JP	cc,addr(Rd)		|0 1|0 1 1 1 1 0| Rd!=0 | cc
	if (OPCNIB1) {
		NOT_EMULATED_OPCODE_VARIANT();
	} else {
		get_address();
		DISASM("%s|%s\t%s,%s", disasm_get_address_code, "JP", ccnames(OPCNIB0), disasm_get_address);
		if (DO_EXEC) {
			if (check_cc(OPCNIB0)) {
				z8k1.codeseg = z8k1.use_seg;
				z8k1.pc = z8k1.use_ofs;
			}
			static const int jp_cycles[] = { 7, 8, 10 };
			cycles += jp_cycles[z8k1.get_address_mode];
		}
	}
	break;

	/*************************** OPC-HI = $5F ***************************/
	case 0x5F:
	// CALL	address			|0 1|0 1 1 1 1 1|0 0 0 0|0 0 0 0
	// CALL	addr(Rd)		|0 1|0 1 1 1 1 1| Rd!=0 |0 0 0 0
	NOT_EMULATED_OPCODE();
	break;

	/*************************** OPC-HI = $9E ***************************/
	case 0x9E:
	// RET	cc			|1 0|0 1 1 1 1 0|0 0 0 0| cc
	NOT_EMULATED_OPCODE();
	break;

	/*************************** OPC-HI = $9F ***************************/
	case 0x9F:
	RESERVED_OPCODE();
	break;

	/*************************** OPC-HI = $20 ***************************/
	case 0x20:
	// LDB Rd,@Rs {BYTE}		|0 0|1 0 0 0 0 0| Rs!=0 | Rd
	// LDB	Rbd,#data		|0 0|1 0 0 0 0 0|0 0 0 0| Rbd
	NOT_EMULATED_OPCODE();
	break;

	/*************************** OPC-HI = $21 ***************************/
	case 0x21:
	// LD	Rd,@Rs {WORD}		|0 0|1 0 0 0 0 1| Rs!=0 | Rd
	// LD	Rd,#data		|0 0|1 0 0 0 0 1|0 0 0 0| Rd
	if (OPCNIB1) {
		NOT_EMULATED_OPCODE_VARIANT();
	} else {
		Uint16 data = READCODE();
		DISASM("%04X|%s\t%s,#$%04X", data, "LD", reg16names(OPCNIB0), data);
		if (DO_EXEC) {
			SetReg16(OPCNIB0, data);
			cycles += 7;
		}
	}
	break;

	/*************************** OPC-HI = $60 ***************************/
	case 0x60:
	// LDB Rd,address {BYTE}	|0 1|1 0 0 0 0 0|0 0 0 0| Rd
	// LDB Rd,addr(Rs) {BYTE}	|0 1|1 0 0 0 0 0| Rs!=0 | Rd
	NOT_EMULATED_OPCODE();
	break;

	/*************************** OPC-HI = $61 ***************************/
	case 0x61:
	// LD	Rd,address {WORD}	|0 1|1 0 0 0 0 1|0 0 0 0| Rd
	// LD	Rd,addr(Rs) {WORD}	|0 1|1 0 0 0 0 1| Rs!=0 | Rd
	NOT_EMULATED_OPCODE();
	break;

	/*************************** OPC-HI = $A0 ***************************/
	case 0xA0:
	// LDB Rd,Rs {BYTE}		|1 0|1 0 0 0 0 0| Rs    | Rd
	DISASM("|%s\t%s,%s", "LDB", reg8names(OPCNIB0), reg8names(OPCNIB1));
	if (DO_EXEC) {
		SetReg8(OPCNIB0, GetReg8(OPCNIB1));
		cycles += 3;
	}
	break;

	/*************************** OPC-HI = $A1 ***************************/
	case 0xA1:
	// LD	Rd,Rs {WORD}		|1 0|1 0 0 0 0 1| Rs    | Rd
	DISASM("|%s\t%s,%s", "LDB", reg16names(OPCNIB0), reg16names(OPCNIB1));
	if (DO_EXEC) {
		SetReg16(OPCNIB0, GetReg16(OPCNIB1));
		cycles += 3;
	}
	break;

	/*************************** OPC-HI = $22 ***************************/
	case 0x22:
	// RESB @Rd,#b {BYTE}		|0 0|1 0 0 0 1 0| Rd!=0 | b
	// RESB Rd,Rs {BYTE}		|0 0|1 0 0 0 1 0|0 0 0 0| Rs    |0 0 0 0| Rd    |0 0 0 0|0 0 0 0
	NOT_EMULATED_OPCODE();
	break;

	/*************************** OPC-HI = $23 ***************************/
	case 0x23:
	// RES	@Rd,#b {WORD}		|0 0|1 0 0 0 1 1| Rd!=0 | b
	// RES	Rd,Rs {WORD}		|0 0|1 0 0 0 1 1|0 0 0 0| Rs    |0 0 0 0| Rd    |0 0 0 0|0 0 0 0
	NOT_EMULATED_OPCODE();
	break;

	/*************************** OPC-HI = $62 ***************************/
	case 0x62:
	// RESB address,#b {BYTE}	|0 1|1 0 0 0 1 0|0 0 0 0| b
	// RESB addr(Rd),#b {BYTE}	|0 1|1 0 0 0 1 0| Rd!=0 | b
	NOT_EMULATED_OPCODE();
	break;

	/*************************** OPC-HI = $63 ***************************/
	case 0x63:
	// RES	address,#b {WORD}	|0 1|1 0 0 0 1 1|0 0 0 0| b
	// RES	addr(Rd),#b {WORD}	|0 1|1 0 0 0 1 1| Rd!=0 | b
	NOT_EMULATED_OPCODE();
	break;

	/*************************** OPC-HI = $A2 ***************************/
	case 0xA2:
	// RESB Rd,#b {BYTE}		|1 0|1 0 0 0 1 0| Rd    | b
	NOT_EMULATED_OPCODE();
	break;

	/*************************** OPC-HI = $A3 ***************************/
	case 0xA3:
	// RES	Rd,#b {WORD}		|1 0|1 0 0 0 1 1| Rd    | b
	NOT_EMULATED_OPCODE();
	break;

	/*************************** OPC-HI = $24 ***************************/
	case 0x24:
	// SETB @Rd,#b {BYTE}		|0 0|1 0 0 1 0 0| Rd!=0 | b
	// SETB Rd,Rs {BYTE}		|0 0|1 0 0 1 0 0|0 0 0 0| Rs    |0 0 0 0| Rd    |0 0 0 0 0 0 0 0
	NOT_EMULATED_OPCODE();
	break;

	/*************************** OPC-HI = $25 ***************************/
	case 0x25:
	// SET	@Rd,#b {WORD}		|0 0|1 0 0 1 0 1| Rd!=0 | b
	// SET	Rd,Rs {WORD}		|0 0|1 0 0 1 0 1|0 0 0 0| Rs    |0 0 0 0| Rd    |0 0 0 0 0 0 0 0
	NOT_EMULATED_OPCODE();
	break;

	/*************************** OPC-HI = $64 ***************************/
	case 0x64:
	// SETB address,#b {BYTE}	|0 1|1 0 0 1 0 0|0 0 0 0| b
	// SETB addr(Rd),#b {BYTE}	|0 1|1 0 0 1 0 0| Rd!=0 | b
	NOT_EMULATED_OPCODE();
	break;

	/*************************** OPC-HI = $65 ***************************/
	case 0x65:
	// SET	address,#b {WORD}	|0 1|1 0 0 1 0 1|0 0 0 0| b
	// SET	addr(Rd),#b {WORD}	|0 1|1 0 0 1 0 1| Rd!=0 | b
	NOT_EMULATED_OPCODE();
	break;

	/*************************** OPC-HI = $A4 ***************************/
	case 0xA4:
	// SETB Rd,#b {BYTE}		|1 0|1 0 0 1 0 0| Rd    | b
	NOT_EMULATED_OPCODE();
	break;

	/*************************** OPC-HI = $A5 ***************************/
	case 0xA5:
	// SET	Rd,#b {WORD}		|1 0|1 0 0 1 0 1| Rd    | b
	NOT_EMULATED_OPCODE();
	break;

	/*************************** OPC-HI = $26 ***************************/
	case 0x26:
	// BITB @Rd,#b {BYTE}		|0 0|1 0 0 1 1 0| Rd!=0 | b
	// BITB Rd,Rs {BYTE}		|0 0|1 0 0 1 1 0|0 0 0 0| Rs    |0 0 0 0| Rd    |0 0 0 0|0 0 0 0
	NOT_EMULATED_OPCODE();
	break;

	/*************************** OPC-HI = $27 ***************************/
	case 0x27:
	// BIT	@Rd,#b {WORD}		|0 0|1 0 0 1 1 1| Rd!=0 | b
	// BIT	Rd,Rs {WORD}		|0 0|1 0 0 1 1 1|0 0 0 0| Rs    |0 0 0 0| Rd    |0 0 0 0|0 0 0 0
	NOT_EMULATED_OPCODE();
	break;

	/*************************** OPC-HI = $66 ***************************/
	case 0x66:
	// BITB address,#b {BYTE}	|0 1|1 0 0 1 1 0|0 0 0 0| b
	// BITB addr(Rd),#b {BYTE}	|0 1|1 0 0 1 1 0| Rd!=0 | b
	NOT_EMULATED_OPCODE();
	break;

	/*************************** OPC-HI = $67 ***************************/
	case 0x67:
	// BIT	address,#b {WORD}	|0 1|1 0 0 1 1 1|0 0 0 0| b
	// BIT	addr(Rd),#b {WORD}	|0 1|1 0 0 1 1 1| Rd!=0 | b
	NOT_EMULATED_OPCODE();
	break;

	/*************************** OPC-HI = $A6 ***************************/
	case 0xA6:
	// BITB Rd,#b {BYTE}		|1 0|1 0 0 1 1 0| Rd    | b
	NOT_EMULATED_OPCODE();
	break;

	/*************************** OPC-HI = $A7 ***************************/
	case 0xA7:
	// BIT	Rd,#b {WORD}		|1 0|1 0 0 1 1 1| Rd    | b
	NOT_EMULATED_OPCODE();
	break;

	/*************************** OPC-HI = $28 ***************************/
	case 0x28:
	// INCB @Rd,#n {BYTE}		|0 0|1 0 1 0 0 0| Rd!=0 | n-1
	NOT_EMULATED_OPCODE();
	break;

	/*************************** OPC-HI = $29 ***************************/
	case 0x29:
	// INC	@Rd,#n {WORD}		|0 0|1 0 1 0 0 1| Rd!=0 | n-1
	NOT_EMULATED_OPCODE();
	break;

	/*************************** OPC-HI = $68 ***************************/
	case 0x68:
	// INCB address,#n {BYTE}	|0 1|1 0 1 0 0 0|0 0 0 0| n-1
	// INCB addr(Rd),#n {BYTE}	|0 1|1 0 1 0 0 0| Rd!=0 | n-1
	NOT_EMULATED_OPCODE();
	break;

	/*************************** OPC-HI = $69 ***************************/
	case 0x69:
	// INC	address,#n {WORD}	|0 1|1 0 1 0 0 1|0 0 0 0| n-1
	// INC	addr(Rd),#n {WORD}	|0 1|1 0 1 0 0 1| Rd!=0 | n-1
	NOT_EMULATED_OPCODE();
	break;

	/*************************** OPC-HI = $A8 ***************************/
	case 0xA8:
	// INCB Rd,#n {BYTE}		|1 0|1 0 1 0 0 0| Rd    | n-1
	DISASM("|%s\t%s,#$%02X", "INCB", reg8names(OPCNIB1), OPCNIB0 + 1);
	if (DO_EXEC) {
		int n = OPCNIB0 + 1, d = GetReg8(OPCNIB1), r = n + d;
		SetReg8(OPCNIB1, r);
		z8k1.flags = (z8k1.flags & (FLAG_CARRY | FLAG_DA | FLAG_HC)) | F_ZERO_BY16(r) | F_SIGN_BY16(r) | F_OVERFLOW_BY16(d,n,r);
		cycles += 4;
	}
	break;

	/*************************** OPC-HI = $A9 ***************************/
	case 0xA9:
	// INC	Rd,#n {WORD}		|1 0|1 0 1 0 0 1| Rd    | n-1
	NOT_EMULATED_OPCODE();
	break;

	/*************************** OPC-HI = $2A ***************************/
	case 0x2A:
	// DECB @Rd,#n {BYTE}		|0 0|1 0 1 0 1 0| Rd!=0 | n-1
	NOT_EMULATED_OPCODE();
	break;

	/*************************** OPC-HI = $2B ***************************/
	case 0x2B:
	// DEC	@Rd,#n {WORD}		|0 0|1 0 1 0 1 1| Rd!=0 | n-1
	NOT_EMULATED_OPCODE();
	break;

	/*************************** OPC-HI = $6A ***************************/
	case 0x6A:
	// DECB address,#n {BYTE}	|0 1|1 0 1 0 1 0|0 0 0 0| n-1
	// DECB addr(Rd),#n {BYTE}	|0 1|1 0 1 0 1 0| Rd!=0 | n-1
	NOT_EMULATED_OPCODE();
	break;

	/*************************** OPC-HI = $6B ***************************/
	case 0x6B:
	// DEC	address,#n {WORD}	|0 1|1 0 1 0 1 1|0 0 0 0| n-1
	// DEC	addr(Rd),#n {WORD}	|0 1|1 0 1 0 1 1| Rd!=0 | n-1
	NOT_EMULATED_OPCODE();
	break;

	/*************************** OPC-HI = $AA ***************************/
	case 0xAA:
	// DECB Rd,#n {BYTE}		|1 0|1 0 1 0 1 0| Rd    | n-1
	NOT_EMULATED_OPCODE();
	break;

	/*************************** OPC-HI = $AB ***************************/
	case 0xAB:
	// DEC	Rd,#n {WORD}		|1 0|1 0 1 0 1 1| Rd    | n-1
	NOT_EMULATED_OPCODE();
	break;

	/*************************** OPC-HI = $2C ***************************/
	case 0x2C:
	// EXB Rd,@Rs {BYTE}		|0 0|1 0 1 1 0 0| Rs!=0 | Rd
	NOT_EMULATED_OPCODE();
	break;

	/*************************** OPC-HI = $2D ***************************/
	case 0x2D:
	// EX	Rd,@Rs {WORD}		|0 0|1 0 1 1 0 1| Rs!=0 | Rd
	NOT_EMULATED_OPCODE();
	break;

	/*************************** OPC-HI = $6C ***************************/
	case 0x6C:
	// EXB Rd,address {BYTE}	|0 1|1 0 1 1 0 0|0 0 0 0| Rd
	// EXB Rd,addr(Rs) {BYTE}	|0 1|1 0 1 1 0 0| Rs!=0 | Rd
	NOT_EMULATED_OPCODE();
	break;

	/*************************** OPC-HI = $6D ***************************/
	case 0x6D:
	// EX	Rd,address {WORD}	|0 1|1 0 1 1 0 1|0 0 0 0| Rd
	// EX	Rd,addr(Rs) {WORD}	|0 1|1 0 1 1 0 1| Rs!=0 | Rd
	NOT_EMULATED_OPCODE();
	break;

	/*************************** OPC-HI = $AC ***************************/
	case 0xAC:
	// EXB Rd,Rs {BYTE}		|1 0|1 0 1 1 0 0| Rs    | Rd
	NOT_EMULATED_OPCODE();
	break;

	/*************************** OPC-HI = $AD ***************************/
	case 0xAD:
	// EX	Rd,Rs {WORD}		|1 0|1 0 1 1 0 1| Rs    | Rd
	NOT_EMULATED_OPCODE();
	break;

	/*************************** OPC-HI = $2E ***************************/
	case 0x2E:
	// LDB @Rd,Rs {BYTE}		|0 0|1 0 1 1 1 0| Rd!=0 | Rs
	NOT_EMULATED_OPCODE();
	break;

	/*************************** OPC-HI = $2F ***************************/
	case 0x2F:
	// LD	@Rd,Rs {WORD}		|0 0|1 0 1 1 1 1| Rd!=0 | Rs
	NOT_EMULATED_OPCODE();
	break;

	/*************************** OPC-HI = $6E ***************************/
	case 0x6E:
	// LDB address,Rs {BYTE}	|0 1|1 0 1 1 1 0|0 0 0 0| Rs
	// LDB addr(Rd),Rs {BYTE}	|0 1|1 0 1 1 1 0| Rd!=0 | Rs
	NOT_EMULATED_OPCODE();
	break;

	/*************************** OPC-HI = $6F ***************************/
	case 0x6F:
	// LD	address,Rs {WORD}	|0 1|1 0 1 1 1 1|0 0 0 0| Rs
	// LD	addr(Rd),Rs {WORD}	|0 1|1 0 1 1 1 1| Rd!=0 | Rs
	NOT_EMULATED_OPCODE();
	break;

	/*************************** OPC-HI = $AE ***************************/
	case 0xAE:
	// TCCB cc,Rd {BYTE}		|1 0|1 0 1 1 1 0| Rd    | cc
	NOT_EMULATED_OPCODE();
	break;

	/*************************** OPC-HI = $AF ***************************/
	case 0xAF:
	// TCC	cc,Rd {WORD}		|1 0|1 0 1 1 1 1| Rd    | cc
	NOT_EMULATED_OPCODE();
	break;

	/*************************** OPC-HI = $30 ***************************/
	case 0x30:
	// LDB Rd,Rs(#disp16) {BYTE}	|0 0|1 1 0 0 0 0| Rs!=0 | Rd    | disp16
	// LDRB Rd,disp16 {BYTE}	|0 0 1 1 0 0 0 0|0 0 0 0| Rd    | disp16
	NOT_EMULATED_OPCODE();
	break;

	/*************************** OPC-HI = $31 ***************************/
	case 0x31:
	// LD	Rd,Rs(#disp16) {WORD}	|0 0|1 1 0 0 0 1| Rs!=0 | Rd    | disp16
	// LDR	Rd,disp16 {WORD}	|0 0 1 1 0 0 0 1|0 0 0 0| Rd    | disp16
	NOT_EMULATED_OPCODE();
	break;

	/*************************** OPC-HI = $70 ***************************/
	case 0x70:
	// LDB Rd,Rs(Rx) {BYTE}		|0 1|1 1 0 0 0 0| Rs!=0 | Rd    |0 0 0 0| Rx    |0 0 0 0 0 0 0 0
	NOT_EMULATED_OPCODE();
	break;

	/*************************** OPC-HI = $71 ***************************/
	case 0x71:
	// LD	Rd,Rs(Rx) {WORD}	|0 1|1 1 0 0 0 1| Rs!=0 | Rd    |0 0 0 0| Rx    |0 0 0 0 0 0 0 0
	NOT_EMULATED_OPCODE();
	break;

	/*************************** OPC-HI = $B0 ***************************/
	case 0xB0:
	// DAB	Rbd			|1 0|1 1 0 0 0 0| Rbd   |0 0 0 0
	NOT_EMULATED_OPCODE();
	break;

	/*************************** OPC-HI = $B1 ***************************/
	case 0xB1:
	// EXTSB Rd			|1 0|1 1 0 0 0 1| Rd    |0 0 0 0
	// EXTS	RRd			|1 0|1 1 0 0 0 1| RRd   |1 0 1 0
	// EXTSL RQd			|1 0|1 1 0 0 0 1| RQd   |0 1 1 1
	switch (OPCNIB0) {
		case 0:
			DISASM("|EXTSB\t%s", reg16names(OPCNIB1));
			if (DO_EXEC) {
				if (z8k1.regs[OPCNIB1] & 0x80)
					z8k1.regs[OPCNIB1] |= 0xFF00;
				else
					z8k1.regs[OPCNIB1] &= 0x00FF;
				cycles += 11;
			}
			break;
		case 9:
			DISASM("|EXTS\t%s", reg32names(OPCNIB1));
			if (DO_EXEC) {
				z8k1.regs[OPCNIB1 & 0xE] = (z8k1.regs[OPCNIB1 | 1] & 0x8000) ? 0xFFFF : 0x0000;
				cycles += 11;
			}
			break;
		case 7:
			DISASM("|EXTSL\t%s", reg64names(OPCNIB1));
			if (DO_EXEC) {
				z8k1.regs[OPCNIB1 & 0xC] = z8k1.regs[(OPCNIB1 & 0xC) | 1] = (z8k1.regs[(OPCNIB1 & 0xC) | 2] & 0x8000) ? 0xFFFF : 0x0000;
				cycles += 11;
			}
			break;
		default:
			NOT_EMULATED_OPCODE_VARIANT();
	}
	break;

	/*************************** OPC-HI = $32 ***************************/
	case 0x32:
	// LDB Rd(#disp16),Rs {BYTE}	|0 0|1 1 0 0 1 0| Rd!=0 | Rs    | disp16
	// LDRB disp16,Rs {BYTE}	|0 0 1 1 0 0 1 0|0 0 0 0| Rs    | disp16
	NOT_EMULATED_OPCODE();
	break;

	/*************************** OPC-HI = $33 ***************************/
	case 0x33:
	// LD	Rd(#disp16),Rs {WORD}	|0 0|1 1 0 0 1 1| Rd!=0 | Rs    | disp16
	// LDR	disp16,Rs {WORD}	|0 0 1 1 0 0 1 1|0 0 0 0| Rs    | disp16
	NOT_EMULATED_OPCODE();
	break;

	/*************************** OPC-HI = $72 ***************************/
	case 0x72:
	// LDB Rd(Rx),Rs {BYTE}		|0 1|1 1 0 0 1 0| Rd!=0 | Rs	|0 0 0 0| Rx    |0 0 0 0 0 0 0 0
	NOT_EMULATED_OPCODE();
	break;

	/*************************** OPC-HI = $73 ***************************/
	case 0x73:
	// LD	Rd(Rx),Rs {WORD}	|0 1|1 1 0 0 1 1| Rd!=0 | Rs	|0 0 0 0| Rx    |0 0 0 0 0 0 0 0
	NOT_EMULATED_OPCODE();
	break;

	/*************************** OPC-HI = $B2 ***************************/
	case 0xB2:
	// RLB Rd,#S {BYTE}		|1 0|1 1 0 0 1 0| Rd    |0 0|S|0
	// RLCB Rd,#S {BYTE}		|1 0|1 1 0 0 1 0| Rd    |1 0|S|0
	// RRB Rd,#S {BYTE}		|1 0|1 1 0 0 1 0| Rd    |0 1|S|0
	// RRCB Rd,#S {BYTE}		|1 0|1 1 0 0 1 0| Rd    |1 1|S|0
	// SDAB	Rbd,Rs			|1 0|1 1 0 0 1 0| Rbd   |1 0 1 1|0 0 0 0| Rs    |0 0 0 0 0 0 0 0
	// SDLB	Rbd,Rs			|1 0|1 1 0 0 1 0| Rbd   |0 0 1 1|0 0 0 0| Rs    |0 0 0 0 0 0 0 0
	// SLAB	Rbd,#b			|1 0|1 1 0 0 1 0| Rbd   |1 0 0 1| b
	// SLLB	Rbd,#b			|1 0|1 1 0 0 1 0| Rbd   |0 0 0 1| b
	NOT_EMULATED_OPCODE();
	break;

	/*************************** OPC-HI = $B3 ***************************/
	case 0xB3:
	// RL	Rd,#S {WORD}		|1 0|1 1 0 0 1 1| Rd    |0 0|S|0
	// RLC	Rd,#S {WORD}		|1 0|1 1 0 0 1 1| Rd    |1 0|S|0
	// RR	Rd,#S {WORD}		|1 0|1 1 0 0 1 1| Rd    |0 1|S|0
	// RRC	Rd,#S {WORD}		|1 0|1 1 0 0 1 1| Rd    |1 1|S|0
	// SDA	Rd,Rs			|1 0|1 1 0 0 1 1| Rd    |1 0 1 1|0 0 0 0| Rs    |0 0 0 0 0 0 0 0
	// SDAL	RRd,Rs			|1 0|1 1 0 0 1 1| RRd   |1 1 1 1|0 0 0 0| Rs    |0 0 0 0 0 0 0 0
	// SDL	Rd,Rs			|1 0|1 1 0 0 1 1| Rd    |0 0 1 1|0 0 0 0| Rs    |0 0 0 0 0 0 0 0
	// SDLL	RRd,Rs			|1 0|1 1 0 0 1 1| RRd   |0 1 1 1|0 0 0 0| Rs    |0 0 0 0 0 0 0 0
	// SLA	Rd,#b			|1 0|1 1 0 0 1 1| Rd    |1 0 0 1| b
	// SLAL	RRd,#b			|1 0|1 1 0 0 1 1| RRd   |1 1 0 1| b
	// SLL	Rd,#b			|1 0|1 1 0 0 1 1| Rd    |0 0 0 1| b
	// SLLL	RRd,#b			|1 0|1 1 0 0 1 1| RRd   |0 1 0 1| b
	NOT_EMULATED_OPCODE();
	break;

	/*************************** OPC-HI = $34 ***************************/
	case 0x34:
	// LDA	Sd,Rs(#disp16)		|0 0|1 1 0 1 0 0| Rs!=0 | Sd    | disp16
	// LDAR	Rd,disp16		|0 0 1 1 0 1 0 0|0 0 0 0| Rd    | disp16
	if (OPCNIB1) {
		NOT_EMULATED_OPCODE_VARIANT();
	} else {
		Uint16 disp = READCODE();
		Uint16 val = z8k1.pc + (Sint16)disp;
		if (IS_SEGMENTED_MODE) {
			DISASM("%04X|%s\t%s,$%04X", disp, "LDAR", reg32names(OPCNIB0), val);
			if (DO_EXEC) SetReg32(OPCNIB0, (z8k1.codeseg << 16) | val);
		} else {
			DISASM("%04X|%s\t%s,$%04X", disp, "LDAR", reg16names(OPCNIB0), val);
			if (DO_EXEC) SetReg16(OPCNIB0, val);
		}
		if (DO_EXEC) cycles += 15;
	}
	break;

	/*************************** OPC-HI = $35 ***************************/
	case 0x35:
	// LDL	RRd,Rs(#disp16)		|0 0 1 1 0 1 0 1| Rs!=0 | RRd   | disp16
	// LDRL	RRd,disp16		|0 0 1 1 0 1 0 1|0 0 0 0| RRd   | disp16
	NOT_EMULATED_OPCODE();
	break;

	/*************************** OPC-HI = $74 ***************************/
	case 0x74:
	// LDA	Sd,Rs(Rx)		|0 1|1 1 0 1 0 0| Rs!=0	| Sd    |0 0 0 0| Rx    |0 0 0 0|0 0 0 0
	NOT_EMULATED_OPCODE();
	break;

	/*************************** OPC-HI = $75 ***************************/
	case 0x75:
	// LDL	RRd,Rs(Rx)		|0 1|1 1 0 1 0|1| Rs!=0 | RRd   |0 0 0 0| Rx    |0 0 0 0 0 0 0 0
	NOT_EMULATED_OPCODE();
	break;

	/*************************** OPC-HI = $B4 ***************************/
	case 0xB4:
	// ADCB Rd,Rs {BYTE}		|1 0|1 1 0 1 0 0| Rs    | Rd
	DISASM("|%s\t%s,%s", "ADCB", reg8names(OPCNIB0), reg8names(OPCNIB1));
	if (DO_EXEC) {
		int s = GetReg8(OPCNIB1), d = GetReg8(OPCNIB0), r = s + d + F_CARRY_BOOL;
		z8k1.flags = F_CARRY_BY8(r) | F_ZERO_BY8(r) | F_SIGN_BY8(r) | F_OVERFLOW_BY8(s,d,r) | F_HALFCARRY_BY8(s,d,r);
		SetReg8(OPCNIB0, r);
		cycles += 5;
	}
	break;

	/*************************** OPC-HI = $B5 ***************************/
	case 0xB5:
	// ADC	Rd,Rs {WORD}		|1 0|1 1 0 1 0 1| Rs    | Rd
	DISASM("|%s\t%s,%s", "ADC", reg16names(OPCNIB0), reg16names(OPCNIB1));
	if (DO_EXEC) {
		int s = GetReg16(OPCNIB1), d = GetReg16(OPCNIB0), r = s + d + F_CARRY_BOOL;
		z8k1.flags = (z8k1.flags & (FLAG_DA | FLAG_HC)) | F_CARRY_BY16(r) | F_ZERO_BY16(r) | F_SIGN_BY16(r) | F_OVERFLOW_BY16(s,d,r);
		SetReg16(OPCNIB0, r);
		cycles += 5;
	}
	break;

	/*************************** OPC-HI = $36 ***************************/
	case 0x36:
	RESERVED_OPCODE();
	break;

	/*************************** OPC-HI = $37 ***************************/
	case 0x37:
	// LDL	Rd(#disp16),RRs		|0 0|1 1 0 1 1 1| Rd!=0 | RRs   | disp16
	// LDRL	disp16,RRs		|0 0 1 1 0 1 1 1|0 0 0 0| RRs   | disp16
	NOT_EMULATED_OPCODE();
	break;

	/*************************** OPC-HI = $76 ***************************/
	case 0x76:
	// LDA	Sd,address		|0 1|1 1 0 1 1 0|0 0 0 0| Sd
	// LDA	Sd,addr(Rs)		|0 1|1 1 0 1 1 0| Rs!=0 | Sd
	NOT_EMULATED_OPCODE();
	break;

	/*************************** OPC-HI = $77 ***************************/
	case 0x77:
	// LDL	Rd(Rx),RRs		|0 1|1 1 0 1 1 1| Rd!=0 | RRs	|0 0 0 0| Rx    |0 0 0 0 0 0 0 0
	NOT_EMULATED_OPCODE();
	break;

	/*************************** OPC-HI = $B6 ***************************/
	case 0xB6:
	// SBCB Rd,Rs {BYTE}		|1 0|1 1 0 1 1 0| Rs    | Rd
	NOT_EMULATED_OPCODE();
	break;

	/*************************** OPC-HI = $B7 ***************************/
	case 0xB7:
	// SBC	Rd,Rs {WORD}		|1 0|1 1 0 1 1 1| Rs    | Rd
	NOT_EMULATED_OPCODE();
	break;

	/*************************** OPC-HI = $38 ***************************/
	case 0x38:
	RESERVED_OPCODE();
	break;

	/*************************** OPC-HI = $39 ***************************/
	case 0x39:
	// LDPS	@Rs			|0 0|1 1 1 0 0 1| Rs!=0 |0 0 0 0
	NOT_EMULATED_OPCODE();
	break;

	/*************************** OPC-HI = $78 ***************************/
	case 0x78:
	RESERVED_OPCODE();
	break;

	/*************************** OPC-HI = $79 ***************************/
	case 0x79:
	// LDPS	address			|0 1|1 1 1 0 0 1|0 0 0 0|0 0 0 0
	// LDPS	addr(Rs)		|0 1|1 1 1 0 0 1| Rs!=0 |0 0 0 0
	NOT_EMULATED_OPCODE();
	break;

	/*************************** OPC-HI = $B8 ***************************/
	case 0xB8:
	// TRDB	  @Rd,@Rs,r		|1 0|1 1 1 0 0 0| Rd!=0 |1 0 0 0|0 0 0 0| r     | Rs!=0 |0 0 0 0
	// TRDBR  @Rd,@Rs,r		|1 0|1 1 1 0 0 0| Rd!=0 |1 1 0 0|0 0 0 0| r     | Rs!=0 |0 0 0 0
	// TRIB	  @Rd,@Rs,r		|1 0|1 1 1 0 0 0| Rd!=0 |0 0 0 0|0 0 0 0| r     | Rs!=0 |0 0 0 0
	// TRIBR  @Rd,@Rs,r		|1 0|1 1 1 0 0 0| Rd!=0 |0 1 0 0|0 0 0 0| r     | Rs!=0 |0 0 0 0
	// TRTDB  @Rs1,@Rs2,r		|1 0|1 1 1 0 0 0| Rs1!=0|1 0 1 0|0 0 0 0| r     | Rs2!=0|0 0 0 0
	// TRTDRB @Rd,@Rs,r		|1 0|1 1 1 0 0 0| Rd!=0 |1 1 1 0|0 0 0 0| r     | Rs!=0 |1 1 1 0
	// TRTIB  @Rs1,@Rs2,r		|1 0|1 1 1 0 0 0| Rs1!=0|0 0 1 0|0 0 0 0| r     | Rs2!=0|0 0 0 0
	// TRTIRB @Rd,@Rs,r		|1 0|1 1 1 0 0 0| Rd!=0 |0 1 1 0|0 0 0 0| r     | Rs!=0 |1 1 1 0
	NOT_EMULATED_OPCODE();
	break;

	/*************************** OPC-HI = $B9 ***************************/
	case 0xB9:
	RESERVED_OPCODE();
	break;

	/*************************** OPC-HI = $3A ***************************/
	case 0x3A:
	// INB   S,Rd,port {BYTE}	|0 0|1 1 1 0 1 0| Rd    |0 1 0|S| port
	// INDB  S,@Rd,@Rs,r {BYTE}	|0 0 1 1 1 0 1 0| Rs!=0 |0 0 0|S|0 0 0 0| r     | Rd!=0 |1 0 0 0
	// INDRB S,@Rd,@Rs,r {BYTE}	|0 0 1 1 1 0 1 0| Rs!=0 |1 0 0|S|0 0 0 0| r     | Rd!=0 |0 0 0 0
	// INIB  S,@Rd,@Rs,r {BYTE}	|0 0 1 1 1 0 1 0| Rs!=0 |1 0 0|S|0 0 0 0| r     | Rd!=0 |1 0 0 0
	// INIRB S,@Rd,@Rs,r {BYTE}	|0 0 1 1 1 0 1 0| Rs!=0 |0 0 0|S|0 0 0 0| r     | Rd!=0 |0 0 0 0
	// OTDRB S,@Rd,@Rs,r {BYTE}	|0 0 1 1 1 0 1 0| Rs!=0 |1 0 1|S|0 0 0 0| r     | Rd!=0 |0 0 0 0
	// OTIRB S,@Rd,@Rs,r {BYTE}	|0 0 1 1 1 0 1 0| Rs!=0 |0 0 1|S|0 0 0 0| r     | Rd!=0 |0 0 0 0
	// OUTB  S,port,Rs {BYTE}	|0 0 1 1 1 0 1 0| Rs    |0 1 1|S| port
	// OUTDB S,@Rd,@Rs,r {BYTE}	|0 0 1 1 1 0 1 0| Rs!=0 |1 0 1|S|0 0 0 0| r     | Rd!=0 |1 0 0 0
	// OUTIB S,@Rd,@Rs,r {BYTE}	|0 0 1 1 1 0 1 0| Rs!=0 |0 0 1|S|0 0 0 0| r     | Rd!=0 |1 0 0 0
	switch ((opc >> 1) & 7) {
		case 3:
			BEGIN
			Uint16 port = READCODE();
			DISASM("%04X|%s\t$%04X,%s", port, (opc & 1) ? "SOUTB" : "OUTB", port, reg8names(OPCNIB1));
			if (DO_EXEC) {
				cycles += 12;
				z8k1_out_byte_cb(opc & 1, port, GetReg8(OPCNIB1));
			}
			END;
		default:
			NOT_EMULATED_OPCODE_VARIANT();
	}
	break;

	/*************************** OPC-HI = $3B ***************************/
	case 0x3B:
	// IN	S,Rd,port {WORD}	|0 0|1 1 1 0 1 1| Rd    |0 1 0|S| port
	// IND	S,@Rd,@Rs,r {WORD}	|0 0 1 1 1 0 1 1| Rs!=0 |0 0 0|S|0 0 0 0| r     | Rd!=0 |1 0 0 0
	// INDR	S,@Rd,@Rs,r {WORD}	|0 0 1 1 1 0 1 1| Rs!=0 |1 0 0|S|0 0 0 0| r     | Rd!=0 |0 0 0 0
	// INI	S,@Rd,@Rs,r {WORD}	|0 0 1 1 1 0 1 1| Rs!=0 |1 0 0|S|0 0 0 0| r     | Rd!=0 |1 0 0 0
	// INIR	S,@Rd,@Rs,r {WORD}	|0 0 1 1 1 0 1 1| Rs!=0 |0 0 0|S|0 0 0 0| r     | Rd!=0 |0 0 0 0
	// OTDR	S,@Rd,@Rs,r {WORD}	|0 0 1 1 1 0 1 1| Rs!=0 |1 0 1|S|0 0 0 0| r     | Rd!=0 |0 0 0 0
	// OTIR	S,@Rd,@Rs,r {WORD}	|0 0 1 1 1 0 1 1| Rs!=0 |0 0 1|S|0 0 0 0| r     | Rd!=0 |0 0 0 0
	// OUT	S,port,Rs {WORD}	|0 0 1 1 1 0 1 1| Rs    |0 1 1|S| port
	// OUTD	S,@Rd,@Rs,r {WORD}	|0 0 1 1 1 0 1 1| Rs!=0 |1 0 1|S|0 0 0 0| r     | Rd!=0 |1 0 0 0
	// OUTI	S,@Rd,@Rs,r {WORD}	|0 0 1 1 1 0 1 1| Rs!=0 |0 0 1|S|0 0 0 0| r     | Rd!=0 |1 0 0 0
	NOT_EMULATED_OPCODE();
	break;

	/*************************** OPC-HI = $7A ***************************/
	case 0x7A:
	// HALT				|0 1 1 1 1 0 1 0|0 0 0 0 0 0 0 0
	NOT_EMULATED_OPCODE();
	break;

	/*************************** OPC-HI = $7B ***************************/
	case 0x7B:
	// IRET				|0 1 1 1 1 0 1 1|0 0 0 0 0 0 0 0
	// MBIT				|0 1 1 1 1 0 1 1|0 0 0 0 1 0 1 0
	// MREQ	Rd			|0 1|1 1 1 0 1 1| Rd    |1 1 0 1
	// MRES				|0 1 1 1 1 0 1 1|0 0 0 0 1 0 0 1
	// MSET				|0 1 1 1 1 0 1 1|0 0 0 0 1 0 0 0
	NOT_EMULATED_OPCODE();
	break;

	/*************************** OPC-HI = $BA ***************************/
	case 0xBA:
	// CPDB   Rd,@Rs,r,cc {BYTE}	|1 0|1 1 1 0 1 0| Rs!=0 |1 0 0 0|0 0 0 0| r     | Rd    | cc
	// CPDRB  Rd,@Rs,r,cc {BYTE}	|1 0|1 1 1 0 1 0| Rs!=0 |1 1 0 0|0 0 0 0| r     | Rd    | cc
	// CPIB   Rd,@Rs,r,cc {BYTE}	|1 0|1 1 1 0 1 0| Rs!=0 |0 0 0 0|0 0 0 0| r     | Rd    | cc
	// CPIRB  Rd,@Rs,r,cc {BYTE}	|1 0|1 1 1 0 1 0| Rs!=0 |0 1 0 0|0 0 0 0| r     | Rd    | cc
	// CPSDB  @Rd,@Rs,r,cc {BYTE}	|1 0|1 1 1 0 1 0| Rs!=0 |1 0 1 0|0 0 0 0| r     | Rd!=0 | cc
	// CPSDRB @Rd,@Rs,r,cc {BYTE}	|1 0|1 1 1 0 1 0| Rs!=0 |1 1 1 0|0 0 0 0| r     | Rd!=0 | cc
	// CPSIB  @Rd,@Rs,r,cc {BYTE}	|1 0|1 1 1 0 1 0| Rs!=0 |0 0 1 0|0 0 0 0| r     | Rd!=0 | cc
	// CPSIRB @Rd,@Rs,r,cc {BYTE}	|1 0|1 1 1 0 1 0| Rs!=0 |0 1 1 0|0 0 0 0| r     | Rd!=0 | cc
	// LDDB   @Rd,@Rs,r {BYTE}	|1 0 1 1 1 0 1 0| Rs!=0 |1 0 0 1|0 0 0 0| r     | Rd!=0 |1 0 0 0
	// LDDRB  @Rd,@Rs,r {BYTE}	|1 0 1 1 1 0 1 0| Rs!=0 |1 0 0 1|0 0 0 0| r     | Rd!=0 |0 0 0 0
	// LDIB   @Rd,@Rs,r {BYTE}	|1 0 1 1 1 0 1 0| Rs!=0 |0 0 0 1|0 0 0 0| r     | Rd!=0 |1 0 0 0
	// LDIRB  @Rd,@Rs,r {BYTE}	|1 0 1 1 1 0 1 0| Rs!=0 |0 0 0 1|0 0 0 0| r     | Rd!=0 |0 0 0 0
	NOT_EMULATED_OPCODE();
	break;

	/*************************** OPC-HI = $BB ***************************/
	case 0xBB:
	// CPD	 Rd,@Rs,r,cc {WORD}	|1 0|1 1 1 0 1 1| Rs!=0 |1 0 0 0|0 0 0 0| r     | Rd    | cc
	// CPDR	 Rd,@Rs,r,cc {WORD}	|1 0|1 1 1 0 1 1| Rs!=0 |1 1 0 0|0 0 0 0| r     | Rd    | cc
	// CPI	 Rd,@Rs,r,cc {WORD}	|1 0|1 1 1 0 1 1| Rs!=0 |0 0 0 0|0 0 0 0| r     | Rd    | cc
	// CPIR	 Rd,@Rs,r,cc {WORD}	|1 0|1 1 1 0 1 1| Rs!=0 |0 1 0 0|0 0 0 0| r     | Rd    | cc
	// CPSD	 @Rd,@Rs,r,cc {WORD}	|1 0|1 1 1 0 1 1| Rs!=0 |1 0 1 0|0 0 0 0| r     | Rd!=0 | cc
	// CPSDR @Rd,@Rs,r,cc {WORD}	|1 0|1 1 1 0 1 1| Rs!=0 |1 1 1 0|0 0 0 0| r     | Rd!=0 | cc
	// CPSI	 @Rd,@Rs,r,cc {WORD}	|1 0|1 1 1 0 1 1| Rs!=0 |0 0 1 0|0 0 0 0| r     | Rd!=0 | cc
	// CPSIR @Rd,@Rs,r,cc {WORD}	|1 0|1 1 1 0 1 1| Rs!=0 |0 1 1 0|0 0 0 0| r     | Rd!=0 | cc
	// LDD 	 @Rd,@Rs,r {WORD}	|1 0 1 1 1 0 1 1| Rs!=0 |1 0 0 1|0 0 0 0| r     | Rd!=0 |1 0 0 0
	// LDDR	 @Rd,@Rs,r {WORD}	|1 0 1 1 1 0 1 1| Rs!=0 |1 0 0 1|0 0 0 0| r     | Rd!=0 |0 0 0 0
	// LDI	 @Rd,@Rs,r {WORD}	|1 0 1 1 1 0 1 1| Rs!=0 |0 0 0 1|0 0 0 0| r     | Rd!=0 |1 0 0 0
	// LDIR	 @Rd,@Rs,r {WORD}	|1 0 1 1 1 0 1 1| Rs!=0 |0 0 0 1|0 0 0 0| r     | Rd!=0 |0 0 0 0
	NOT_EMULATED_OPCODE();
	break;

	/*************************** OPC-HI = $3C ***************************/
	case 0x3C:
	// INB Rd,@Rs {BYTE}		|0 0|1 1 1 1 0 0| Rs!=0 | Rd
	NOT_EMULATED_OPCODE();
	break;

	/*************************** OPC-HI = $3D ***************************/
	case 0x3D:
	// IN	Rd,@Rs {WORD}		|0 0|1 1 1 1 0 1| Rs!=0 | Rd
	NOT_EMULATED_OPCODE();
	break;

	/*************************** OPC-HI = $7C ***************************/
	case 0x7C:
	// DI	int			|0 1|1 1 1 1 0 0|0 0 0 0 0 0|int
	// EI	int			|0 1 1 1 1 1 0 0|0 0 0 0 0 1|int
	NOT_EMULATED_OPCODE();
	break;

	/*************************** OPC-HI = $7D ***************************/
	case 0x7D:	//	Privileged instruction!
	// LDCTL FCW,Rs			|0 1 1 1 1 1 0 1| Rs    |1 0 1 0
	// LDCTL REFRESH,Rs		|0 1 1 1 1 1 0 1| Rs    |1 0 1 1
	// LDCTL PSAPSEG,Rs		|0 1 1 1 1 1 0 1| Rs    |1 1 0 0
	// LDCTL PSAPOFF,Rs		|0 1 1 1 1 1 0 1| Rs    |1 1 0 1
	// LDCTL NSPSEG,Rs		|0 1 1 1 1 1 0 1| Rs    |1 1 1 0
	// LDCTL NSPOFF,Rs		|0 1 1 1 1 1 0 1| Rs    |1 1 1 1
	// LDCTL Rd,FCW			|0 1 1 1 1 1 0 1| Rd    |0 0 1 0
	// LDCTL Rd,REFRESH		|0 1 1 1 1 1 0 1| Rd    |0 0 1 1
	// LDCTL Rd,PSAPSEG		|0 1 1 1 1 1 0 1| Rd    |0 1 0 0
	// LDCTL Rd,PSAPOFF		|0 1 1 1 1 1 0 1| Rd    |0 1 0 1
	// LDCTL Rd,NSPSEG		|0 1 1 1 1 1 0 1| Rd    |0 1 1 0
	// LDCTL Rd,NSPOFF		|0 1 1 1 1 1 0 1| Rd    |0 1 1 1
	switch (OPCNIB0) {
		case 2:
			DISASM("|%s\t%s,%s", "LDCTL", reg16names(OPCNIB1), "FCW");
			if (DO_EXEC) SetReg16(OPCNIB1, (z8k1.fcw << 8) | z8k1.flags);
			break;
		case 3:
			DISASM("|%s\t%s,%s", "LDCTL", reg16names(OPCNIB1), "REFRESH");
			if (DO_EXEC) SetReg16(OPCNIB1, z8k1.refresh);
			break;
		case 4:
			DISASM("|%s\t%s,%s", "LDCTL", reg16names(OPCNIB1), "PSAPSEG");
			if (DO_EXEC) SetReg16(OPCNIB1, z8k1.psaseg << 8);
			break;
		case 5:
			DISASM("|%s\t%s,%s", "LDCTL", reg16names(OPCNIB1), "PSAPOFF");
			if (DO_EXEC) SetReg16(OPCNIB1, z8k1.psaofs);
			break;
		case 6:
			DISASM("|%s\t%s,%s", "LDCTL", reg16names(OPCNIB1), "NSPSEG");
			if (DO_EXEC) SetReg16(OPCNIB1, z8k1.stacksegusr << 8);
			break;
		case 7:
			DISASM("|%s\t%s,%s", "LDCTL", reg16names(OPCNIB1), "NSPOFF");
			if (DO_EXEC) SetReg16(OPCNIB1, z8k1.stackptrusr);
			break;
		case 10:
			DISASM("|%s\t%s,%s", "LDCTL", "FCW", reg16names(OPCNIB1));
			if (DO_EXEC) {
				Uint16 temp = GetReg16(OPCNIB1);
				set_fcw_byte(temp >> 8);
				z8k1.flags = temp & 0xFC;
			}
			break;
		case 11:
			DISASM("|%s\t%s,%s", "LDCTL", "REFRESH", reg16names(OPCNIB1));
			if (DO_EXEC) z8k1.refresh = GetReg16(OPCNIB1) & 0xFFFE;
			break;
		case 12:
			DISASM("|%s\t%s,%s", "LDCTL", "PSAPSEG", reg16names(OPCNIB1));
			if (DO_EXEC) z8k1.psaseg = (GetReg16(OPCNIB1) >> 8) & 0x7F;
			break;
		case 13:
			DISASM("|%s\t%s,%s", "LDCTL", "PSAPOFF", reg16names(OPCNIB1));
			if (DO_EXEC) z8k1.psaofs = GetReg16(OPCNIB1);
			break;
		case 14:
			DISASM("|%s\t%s,%s", "LDCTL", "NSPSEG", reg16names(OPCNIB1));
			if (DO_EXEC) z8k1.stacksegusr = (GetReg16(OPCNIB1) >> 8) & 0x7F;
			break;
		case 15:
			DISASM("|%s\t%s,%s", "LDCTL", "NSPOFF", reg16names(OPCNIB1));
			if (DO_EXEC) z8k1.stackptrusr = GetReg16(OPCNIB1);
			break;
		default:
			NOT_EMULATED_OPCODE_VARIANT();

	}
	if (DO_EXEC)
		cycles += 7;
	break;

	/*************************** OPC-HI = $BC ***************************/
	case 0xBC:
	// RRDB	Rbl,Rbs			|1 0|1 1 1 1 0 0| Rbs   | Rbl
	NOT_EMULATED_OPCODE();
	break;

	/*************************** OPC-HI = $BD ***************************/
	case 0xBD:
	// LDK	Rd,#nibble		|1 0|1 1 1 1 0 1| Rd    | nibble
	DISASM("%s\t%s,#$%X", "LDK", reg16names(OPCNIB1), OPCNIB0);
	if (DO_EXEC) {
		SetReg16(OPCNIB1, OPCNIB0);
		cycles += 5;
	}
	break;

	/*************************** OPC-HI = $3E ***************************/
	case 0x3E:
	// OUTB @Rd,Rs {BYTE}		|0 0 1 1 1 1 1 0| Rd!=0 | Rs
	NOT_EMULATED_OPCODE();
	break;

	/*************************** OPC-HI = $3F ***************************/
	case 0x3F:
	// OUT	@Rd,Rs {WORD}		|0 0 1 1 1 1 1 1| Rd!=0 | Rs
	NOT_EMULATED_OPCODE();
	break;

	/*************************** OPC-HI = $7E ***************************/
	case 0x7E:
	RESERVED_OPCODE();
	break;

	/*************************** OPC-HI = $7F ***************************/
	case 0x7F:
	// SC	#src			|0 1 1 1 1 1 1 1| src
	NOT_EMULATED_OPCODE();
	break;

	/*************************** OPC-HI = $BE ***************************/
	case 0xBE:
	// RLDB	Rbl,Rbs			|1 0|1 1 1 1 1|0| Rbs   | Rbl
	NOT_EMULATED_OPCODE();
	break;

	/*************************** OPC-HI = $BF ***************************/
	case 0xBF:
	RESERVED_OPCODE();
	break;



#if 0
		// ADCB
		case 0xB4:
			BEGIN
			int vals = GetReg8(opc2 >> 4), vald = GetReg8(opc2 & 0xF);
			int temp = vals + vald + F_CARRY_BOOL;
			flags =
				(temp & 0x100 ? FLAG_CARRY : 0) |
				(temp & 0x0FF ? 0 : FLAG_ZERO)  |
				(temp & 0x080 ? FLAG_SIGN : 0)  |
				(((vals & 0x80) == (vald & 0x80)) && ((temp & 0x80) != (vals & 0x80))) ? FLAG_PV : 0;
			SetReg8(opc & 0xF, temp & 0xFF);
			END;
#endif
		// ADC
		//case 0xB5:
#if 0
		// 0x20:
		// -- or: LDB R8,#data -> though this is a longer form of the LDB with the same effect, use $CX opcodes instead! shorter in size and cycles too!
		case 0x20:
			if ((opc2 & 0xF0)) {

			} else {	// see the comment at the top of this "case", not so useful
				cycles += 7;
				Uint8 datah = READCODE();
				Uint8 datal = READCODE();
				Uint8 data = (opc2 & 8) ? datal : datah;	// FIXME: just guessing this the "data byte twice" strange situation!
				DISASM("%02X:%04X %02X%02X%02X%02X\tLDB*\t%s,#%02X",
					codeseg, pc_orig,
					opc, opc2, datah, datal,
					reg8names[opc2 & 0xF],
					data
				);
				if (DO_EXEC)
					SetReg8(opc2 & 0xF, data);
			}
			break;
#endif

		/************************************************************************************
		 * "Compact" instruction formats $C0-$FF, not following the generic opcode scheme.
		 * This is in fact good, since some opcodes can have shorter (only one word for these)
		 * construction. Some instructions have longer version as well (above) but not so
		 * sane idea to use them for sure. These compact opcodes are only four different
		 * instructions for real, but all of them uses only the four high bits of the opcode
		 * word, and use the lower 4 for other purposes to help to shorten the opcode.
		 ************************************************************************************/

		// $C0-$CF: LDB (load immediate byte)
		// Low nybl of opcode selects a 8 bit register
		// low byte of opcode is the 8 bit immediate data
		case 0xC0: case 0xC1: case 0xC2: case 0xC3: case 0xC4: case 0xC5: case 0xC6: case 0xC7:
		case 0xC8: case 0xC9: case 0xCA: case 0xCB: case 0xCC: case 0xCD: case 0xCE: case 0xCF:
			DISASM("|%s\t%s,#$%02X", "LDB", reg8names(OPCNIB2), opc & 0xFF);
			if (DO_EXEC) {
				SetReg8(OPCNIB2, opc & 0xFF);
				cycles += 5;
			}
			break;
		// $D0-$DF: CALR, cal relative
		// lower 12 bits are the relative offset, high 4 bits the "opcode" itself only.
		// the offset itself is a signed 12 bit value, though divided by 2
		case 0xD0: case 0xD1: case 0xD2: case 0xD3: case 0xD4: case 0xD5: case 0xD6: case 0xD7:
		case 0xD8: case 0xD9: case 0xDA: case 0xDB: case 0xDC: case 0xDD: case 0xDE: case 0xDF:
			BEGIN
			Uint16 pc_new = ((opc & 0x800) ? (
				(opc & 0xFFF) - 0x1000
			) : (
				opc & 0xFFF
			)) * 2 + z8k1.pc;
			DISASM("|%s\t$%04X", "CALR", pc_new);
			if (DO_EXEC) {
				//PUSH_PC(); // FIXME TODO
				z8k1.pc = pc_new;
				cycles += IS_SEGMENTED_MODE ? 15 : 10;
			}
			END;
		// $E0-$EF: JR CC,...
		// Low nybl of opcode contains the condition (CC)
		// Low byte of opcode is the offset, which is a signed value and multiplied by 2 if jump is taken before added to PC
		case 0xE0: case 0xE1: case 0xE2: case 0xE3: case 0xE4: case 0xE5: case 0xE6: case 0xE7:
		case 0xE8: case 0xE9: case 0xEA: case 0xEB: case 0xEC: case 0xED: case 0xEE: case 0xEF:
			BEGIN
			Uint16 pc_new = z8k1.pc + (2 * (int)(Sint8)(opc & 0xFF));
			DISASM("|%s\t%s,$%04X", "JR", ccnames(OPCNIB2), pc_new);
			if (DO_EXEC && check_cc(OPCNIB2))
				z8k1.pc = pc_new;
			if (DO_EXEC)
				cycles += 6;
			END;
		// $F0-$FF: DJNZ (Decrement Jump if Non-Zero -> DJNZ), DBJNZ if byte register is selected
		// low nybl of opcode is the register selection (byte or word reg, see below)
		// low byte of opc is offset*2 (but bit7 is byte/word register select instead!)
		// Note: DJNZ offset is ALWAYS negative, ie substracted from PC ...
		// ... that is, you can't DJNZ forwards, only backwards ...
		case 0xF0: case 0xF1: case 0xF2: case 0xF3: case 0xF4: case 0xF5: case 0xF6: case 0xF7:
		case 0xF8: case 0xF9: case 0xFA: case 0xFB: case 0xFC: case 0xFD: case 0xFE: case 0xFF:
			BEGIN
			Uint16 pc_new = z8k1.pc - ((opc & 0x7F) << 1);
			if (opc & 0x80) {
				DISASM("|%s\t%s,$%04X", "DJNZ", reg16names(OPCNIB2), pc_new);
				if (DO_EXEC && IncReg16(OPCNIB2, -1))
					z8k1.pc = pc_new;
			} else {
				DISASM("|%s\t%s,$%04X", "DBJNZ", reg8names(OPCNIB2), pc_new);
				if (DO_EXEC && IncReg8(OPCNIB2, -1))
					z8k1.pc = pc_new;
			}
			if (DO_EXEC)
				cycles += 11;
			END;
#if 0
		default:
			printf("Unknown opcode $%02X\n", opc);
			return -1;
#endif
	}
	} while (cycles < cycles_limit);
	return cycles;
}
