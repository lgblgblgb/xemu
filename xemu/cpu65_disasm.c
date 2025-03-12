/* Xemu - emulation (running on Linux/Unix/Windows/OSX, utilizing SDL2) of some
   8 bit machines, including the Commodore LCD and Commodore 65 and MEGA65 as well.
   Copyright (C)2016-2025 LGB (Gábor Lénárt) <lgblgblgb@gmail.com>

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
#include "xemu/cpu65_disasm.h"

// TODO:
//	* currently it's MEGA65 specific, but it should support 6502, 65C02 too!
//	* introduce formatting capabilities, ie allowing upper/lower case, '$' prefixed output or none
//	* allow using symbol file and resolve references on disassemby

#if !defined(CPU_65CE02) && defined(MEGA65)
#error "CPU_65CE02 is not defined while MEGA65 is."
#endif
#if !defined(CPU_65CE02)
#error "Sorry, currently only available for CPU_65CE02"
#endif

//static const char *opcode_adm_names[] = {"","#$nn","#$nnnn","$nn","$nn,$rr","$nn,X","$nn,Y","$nnnn","$nnnn,X","$nnnn,Y","$rr","$rrrr","($nn),Y","($nn),Z","($nn,SP),Y","($nn,X)","($nnnn)","($nnnn,X)","A"};
// {'': 0, '$nn,X': 5, '($nn),Y': 12, '$nn,$rr': 4, '$nn,Y': 6, '$nnnn,Y': 9, '$nnnn,X': 8, '($nnnn,X)': 17, '$nnnn': 7, '$nn': 3, '($nnnn)': 16, '#$nn': 1, 'A': 18, '$rr': 10, '$rrrr': 11, '($nn),Z': 13, '($nn,SP),Y': 14, '($nn,X)': 15, '#$nnnn': 2}
static const char *opcode_names[256] = {	// 65CE02 (C65+MAP) + MEGA65 opcodes
//	 x0    x1    x2    x3    x4    x5    x6    x7      x8    x9    xA    xB    xC    xD    xE    xF
	"BRK","ORA","CLE","SEE","TSB","ORA","ASL","RMB0", "PHP","ORA","ASL","TSY","TSB","ORA","ASL","BBR0",	// 0x
	"BPL","ORA","ORA","BPL","TRB","ORA","ASL","RMB1", "CLC","ORA","INC","INZ","TRB","ORA","ASL","BBR1",	// 1x
	"JSR","AND","JSR","JSR","BIT","AND","ROL","RMB2", "PLP","AND","ROL","TYS","BIT","AND","ROL","BBR2",	// 2x
	"BMI","AND","AND","BMI","BIT","AND","ROL","RMB3", "SEC","AND","DEC","DEZ","BIT","AND","ROL","BBR3",	// 3x
	"RTI","EOR","NEG","ASR","ASR","EOR","LSR","RMB4", "PHA","EOR","LSR","TAZ","JMP","EOR","LSR","BBR4",	// 4x
	"BVC","EOR","EOR","BVC","ASR","EOR","LSR","RMB5", "CLI","EOR","PHY","TAB","MAP","EOR","LSR","BBR5",	// 5x
	"RTS","ADC","RTS","BSR","STZ","ADC","ROR","RMB6", "PLA","ADC","ROR","TZA","JMP","ADC","ROR","BBR6",	// 6x
	"BVS","ADC","ADC","BVS","STZ","ADC","ROR","RMB7", "SEI","ADC","PLY","TBA","JMP","ADC","ROR","BBR7",	// 7x
	"BRA","STA","STA","BRA","STY","STA","STX","SMB0", "DEY","BIT","TXA","STY","STY","STA","STX","BBS0",	// 8x
	"BCC","STA","STA","BCC","STY","STA","STX","SMB1", "TYA","STA","TXS","STX","STZ","STA","STZ","BBS1",	// 9x
	"LDY","LDA","LDX","LDZ","LDY","LDA","LDX","SMB2", "TAY","LDA","TAX","LDZ","LDY","LDA","LDX","BBS2",	// Ax
	"BCS","LDA","LDA","BCS","LDY","LDA","LDX","SMB3", "CLV","LDA","TSX","LDZ","LDY","LDA","LDX","BBS3",	// Bx
	"CPY","CMP","CPZ","DEW","CPY","CMP","DEC","SMB4", "INY","CMP","DEX","ASW","CPY","CMP","DEC","BBS4",	// Cx
	"BNE","CMP","CMP","BNE","CPZ","CMP","DEC","SMB5", "CLD","CMP","PHX","PHZ","CPZ","CMP","DEC","BBS5",	// Dx
	"CPX","SBC","LDA","INW","CPX","SBC","INC","SMB6", "INX","SBC","NOP","ROW","CPX","SBC","INC","BBS6",	// Ex
	"BEQ","SBC","SBC","BEQ","PHW","SBC","INC","SMB7", "SED","SBC","PLX","PLZ","PHW","SBC","INC","BBS7"	// Fx
};
static const Uint8 opcode_adms[256] = {
//	x0 x1 x2 x3 x4 x5 x6 x7 x8 x9 xA xB xC xD xE xF
	 0,15, 0, 0, 3, 3, 3, 3, 0, 1,18, 0, 7, 7, 7, 4,	// 0x
	10,12,13,11, 3, 5, 5, 3, 0, 9, 0, 0, 7, 8, 8, 4,	// 1x
	 7,15,16,17, 3, 3, 3, 3, 0, 1,18, 0, 7, 7, 7, 4,	// 2x
	10,12,13,11, 5, 5, 5, 3, 0, 9, 0, 0, 8, 8, 8, 4,	// 3x
	 0,15, 0, 0, 3, 3, 3, 3, 0, 1,18, 0, 7, 7, 7, 4,	// 4x
	10,12,13,11, 5, 5, 5, 3, 0, 9, 0, 0, 0, 8, 8, 4,	// 5x
	 0,15, 1,11, 3, 3, 3, 3, 0, 1,18, 0,16, 7, 7, 4,	// 6x
	10,12,13,11, 5, 5, 5, 3, 0, 9, 0, 0,17, 8, 8, 4,	// 7x
	10,15,14,11, 3, 3, 3, 3, 0, 1, 0, 8, 7, 7, 7, 4,	// 8x
	10,12,13,11, 5, 5, 6, 3, 0, 9, 0, 9, 7, 8, 8, 4,	// 9x
	 1,15, 1, 1, 3, 3, 3, 3, 0, 1, 0, 7, 7, 7, 7, 4,	// Ax
	10,12,13,11, 5, 5, 6, 3, 0, 9, 0, 8, 8, 8, 9, 4,	// Bx
	 1,15, 1, 3, 3, 3, 3, 3, 0, 1, 0, 7, 7, 7, 7, 4,	// Cx
	10,12,13,11, 3, 5, 5, 3, 0, 9, 0, 0, 7, 8, 8, 4,	// Dx
	 1,15,14, 3, 3, 3, 3, 3, 0, 1, 0, 7, 7, 7, 7, 4,	// Ex
	10,12,13,11, 2, 5, 5, 3, 0, 9, 0, 0, 7, 8, 8, 4		// Fx
};
static const Uint8 adm_sizes[] = { 0, 1, 2, 1, 2, 1, 1, 2, 2, 2, 1, 2, 1, 1, 1, 1, 2, 2, 0 };	// size of extra info in each addr modes (other than the opcode)
#ifdef MEGA65
static const char *qopcode_names[256] = {
//	 x0     x1     x2     x3     x4     x5     x6     x7      x8     x9     xA     xB     xC     xD     xE     xF
	NULL,  NULL,  NULL,  NULL,  NULL, "ORQ", "ASLQ", NULL,   NULL,  NULL, "ASLQ", NULL,  NULL, "ORQ", "ASLQ", NULL,	// 0x
	NULL,  NULL, "ORQ",  NULL,  NULL,  NULL, "ASLQ", NULL,   NULL,  NULL, "INQ",  NULL,  NULL,  NULL, "ASLQ", NULL,	// 1x
	NULL,  NULL,  NULL,  NULL, "BITQ","ANDQ","ROLQ", NULL,   NULL,  NULL, "ROLQ", NULL, "BITQ","ANDQ","ROLQ", NULL,	// 2x
	NULL,  NULL, "ANDQ", NULL,  NULL,  NULL, "ROLQ", NULL,   NULL,  NULL, "DEQ",  NULL,  NULL,  NULL, "ROLQ", NULL,	// 3x
	NULL,  NULL, "NEGQ","ASRQ","ASRQ","EORQ","LSRQ", NULL,   NULL,  NULL, "LSRQ", NULL,  NULL, "EORQ","LSRQ", NULL,	// 4x
	NULL,  NULL, "EORQ", NULL, "ASRQ","LSRQ", NULL,  NULL,   NULL,  NULL,  NULL,  NULL,  NULL,  NULL, "LSRQ", NULL,	// 5x
	NULL,  NULL,  NULL,  NULL,  NULL, "ADCQ","RORQ", NULL,   NULL,  NULL, "RORQ", NULL,  NULL, "ADCQ","RORQ", NULL,	// 6x
	NULL,  NULL, "ADCQ", NULL,  NULL,  NULL, "RORQ", NULL,   NULL,  NULL,  NULL,  NULL,  NULL,  NULL, "RORQ", NULL,	// 7x
	NULL,  NULL,  NULL,  NULL,  NULL, "STQ",  NULL,  NULL,   NULL,  NULL,  NULL,  NULL,  NULL, "STQ",  NULL,  NULL,	// 8x
	NULL,  NULL, "STQ",  NULL,  NULL,  NULL,  NULL,  NULL,   NULL,  NULL,  NULL,  NULL,  NULL,  NULL,  NULL,  NULL,	// 9x
	NULL,  NULL,  NULL,  NULL,  NULL, "LDQ",  NULL,  NULL,   NULL,  NULL,  NULL,  NULL,  NULL, "LDQ",  NULL,  NULL,	// Ax
	NULL,  NULL, "LDQ",  NULL,  NULL,  NULL,  NULL,  NULL,   NULL,  NULL,  NULL,  NULL,  NULL,  NULL,  NULL,  NULL,	// Bx
	NULL,  NULL,  NULL,  NULL,  NULL, "CMPQ","DEQ",  NULL,   NULL,  NULL,  NULL,  NULL,  NULL, "CMPQ","DEQ",  NULL,	// Cx
	NULL,  NULL, "CMPQ", NULL,  NULL,  NULL, "DEQ",  NULL,   NULL,  NULL,  NULL,  NULL,  NULL,  NULL, "DEQ",  NULL,	// Dx
	NULL,  NULL,  NULL,  NULL,  NULL, "SBCQ","INQ",  NULL,   NULL,  NULL,  NULL,  NULL,  NULL, "SBCQ","INQ",  NULL,	// Ex
	NULL,  NULL, "SBCQ", NULL,  NULL,  NULL, "INQ",  NULL,   NULL,  NULL,  NULL,  NULL,  NULL,  NULL, "INQ",  NULL	// Fx
};
#endif


static inline unsigned int rel8 ( const unsigned int addr, const unsigned int mask, const Uint8 rel )
{
	return (unsigned int)((signed)addr + (Sint8)rel) & mask;

}


static inline unsigned rel16 ( const unsigned int addr, const unsigned int mask, const Uint16 rel )
{
	return (unsigned int)((signed)addr + (Sint16)rel) & mask;
}


int cpu65_disasm ( Uint8 (*reader)(const unsigned int, const unsigned int), unsigned int addr, unsigned int mask, const char **opname_p, char *arg_p )
{
	unsigned int ofs = 0;
	Uint8 opc = reader(addr, ofs++);
#ifdef	MEGA65
	int neg_prefix = 0, nop_prefix = 0;
	if (opc == 0x42) {
		Uint8 opc_next = reader(addr, ofs++);
		if (opc_next == 0x42) {
			opc_next = reader(addr, ofs++);
			if (qopcode_names[opc_next] || opc_next == 0xEA) {
				neg_prefix = 1;
				opc = opc_next;
			} else {
				ofs = 1;
			}
		}
	}
	if (opc == 0xEA) {
		Uint8 opc_next = reader(addr, ofs++);
		if (opcode_adms[opc_next] == 13) {
			nop_prefix = 1;
			opc = opc_next;
		} else {
			neg_prefix = 0;
			ofs = 1;
		}
	}
	*opname_p = (!neg_prefix ? opcode_names : qopcode_names)[opc];
#else
	*opname_p = opcode_names[opc];
#endif
	const unsigned int adm = opcode_adms[opc];
	switch (adm) {
		case  0:	// implied
			arg_p[0] = 0;
			break;
		case  1:	// #$nn
			sprintf(arg_p, "#$%02X",      reader(addr, ofs));
			break;
#ifdef		CPU_65CE02
		case  2:	// #$nnnn
			sprintf(arg_p, "#$%04X",      reader(addr, ofs) + (reader(addr, ofs + 1) << 8));
			break;
#endif
		case  3:	// $nn
			sprintf(arg_p, "$%02X",       reader(addr, ofs));
			break;
		case  4:	// $nn,$rr
			sprintf(arg_p, "$%02X,$%04X", reader(addr, ofs), rel8(addr + 3, mask, reader(addr, ofs + 1)));
			break;
		case  5:	// $nn,X
			sprintf(arg_p, "$%02X,X",     reader(addr, ofs));
			break;
		case  6:	// $nn,Y
			sprintf(arg_p, "$%02X,Y",     reader(addr, ofs));
			break;
		case  7:	// $nnnn
			sprintf(arg_p, "$%04X",       reader(addr, ofs) + (reader(addr, ofs + 1) << 8));
			break;
		case  8:	// $nnnn,X
			sprintf(arg_p, "$%04X,X",     reader(addr, ofs) + (reader(addr, ofs + 1) << 8));
			break;
		case  9:	// $nnnn,Y
			sprintf(arg_p, "$%04X,Y",     reader(addr, ofs) + (reader(addr, ofs + 1) << 8));
			break;
		case 10:	// $rr
			sprintf(arg_p, "$%04X",       rel8( addr + 2, mask, reader(addr, ofs)));
			break;
#ifdef		CPU_65CE02
		case 11:	// $rrrr
			sprintf(arg_p, "$%04X",       rel16(addr + 2, mask, reader(addr, ofs) + (reader(addr, ofs + 1) << 8)));
			break;
#endif
		case 12:	// ($nn),Y
			sprintf(arg_p, "($%02X),Y",   reader(addr, ofs));
			break;
		case 13:	// ($nn),Z
#ifdef			MEGA65
			if (nop_prefix) {
				if ((neg_prefix && opc != 0xB2) || !neg_prefix)
					sprintf(arg_p, "[$%02X],Z", reader(addr, ofs));
				else
					sprintf(arg_p, "[$%02X]",   reader(addr, ofs));
			} else
#endif
#ifdef				CPU_65CE02
				sprintf(arg_p, "($%02X),Z",  reader(addr, ofs));	// 65CE02 (+4510) and MEGA65 without nop prefix
#else
				sprintf(arg_p, "($%02X)",    reader(addr, ofs));	// 65C02
#endif
			break;
		case 14:	// ($nn,SP),Y
			sprintf(arg_p, "($%02X,SP),Y",reader(addr, ofs));
			break;
		case 15:	// ($nn,X)
			sprintf(arg_p, "($%02X,X)",   reader(addr, ofs));
			break;
		case 16:	// ($nnnn)
			sprintf(arg_p, "($%04X)",     reader(addr, ofs) + (reader(addr, ofs + 1) << 8));
			break;
		case 17:	// ($nnnn,X)
			sprintf(arg_p, "($%04X,X)",   reader(addr, ofs) + (reader(addr, ofs + 1) << 8));
			break;
		case 18:	// A
#ifdef			MEGA65
			arg_p[0] = !neg_prefix ? 'A' : 'Q';
#else
			arg_p[0] = 'A';
#endif
			arg_p[1] = 0;
			break;
		default:
			sprintf(arg_p, "<ERROR ADM:%d for OP:$%02X>", adm, opc);	// should NOT happen!
			break;
	}
	return ofs + adm_sizes[adm];
}
