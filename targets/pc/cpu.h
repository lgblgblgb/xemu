/*
  Fake86: A portable, open-source 8086 PC emulator.
  Copyright (C)2010-2012 Mike Chambers
            (C)2022      LGB - Gabor Lenart

  This program is free software; you can redistribute it and/or
  modify it under the terms of the GNU General Public License
  as published by the Free Software Foundation; either version 2
  of the License, or (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
*/

#ifndef XEMU_PC_CPU_H_INCLUDED
#define XEMU_PC_CPU_H_INCLUDED

extern void reset86 ( void );
extern uint32_t exec86 ( uint32_t execloops );
extern void iret86 ( void );

extern int into_opcode ( void );	// must be defined somewhere! it must return non-zero if INTO (opcode 0CEh) can be executed, otherwise can be trapped, etc ...

#if 0
#define X86_A20_GATE_MASK_ON	0xFFFFFFFFU
#define X86_A20_GATE_MASK_OFF	0xFFEFFFFFU
extern uint32_t x86_a20_gate_mask = X86_A20_GATE_MASK_ON;
#endif

#define regax 0
#define regcx 1
#define regdx 2
#define regbx 3
#define regsp 4
#define regbp 5
#define regsi 6
#define regdi 7

#if defined(ENDIAN_UGLY) && !defined(ENDIAN_GOOD)
#warning "Using ugly endian ISA!"
#define regal 1
#define regah 0
#define regcl 3
#define regch 2
#define regdl 5
#define regdh 4
#define regbl 7
#define regbh 6
#elif defined(ENDIAN_GOOD) && !defined(ENDIAN_UGLY)
#define regal 0
#define regah 1
#define regcl 2
#define regch 3
#define regdl 4
#define regdh 5
#define regbl 6
#define regbh 7
#else
#error "Unknown endian"
#endif

union x86_bytewordregs_union {
	uint16_t	wordregs[8];
	uint8_t		byteregs[8];
};

extern union x86_bytewordregs_union x86_regs;

#define X86_AX x86_regs.wordregs[regax]
#define X86_CX x86_regs.wordregs[regcx]
#define X86_DX x86_regs.wordregs[regdx]
#define X86_BX x86_regs.wordregs[regbx]
#define X86_SP x86_regs.wordregs[regsp]
#define X86_BP x86_regs.wordregs[regbp]
#define X86_SI x86_regs.wordregs[regsi]
#define X86_DI x86_regs.wordregs[regdi]

#define X86_AL x86_regs.byteregs[regal]
#define X86_AH x86_regs.byteregs[regah]
#define X86_CL x86_regs.byteregs[regcl]
#define X86_CH x86_regs.byteregs[regch]
#define X86_DL x86_regs.byteregs[regdl]
#define X86_DH x86_regs.byteregs[regdh]
#define X86_BL x86_regs.byteregs[regbl]
#define X86_BH x86_regs.byteregs[regbh]


extern uint16_t x86_segregs[4];
#define reges 0
#define regcs 1
#define regss 2
#define regds 3
#define X86_ES x86_segregs[reges]
#define X86_CS x86_segregs[regcs]
#define X86_SS x86_segregs[regss]
#define X86_DS x86_segregs[regds]

extern uint16_t x86_ip;
#define X86_IP x86_ip

extern uint8_t x86_cf, x86_pf, x86_af, x86_zf, x86_sf, x86_tf, x86_ifl, x86_df, x86_of;
#define X86_CF	x86_cf
#define X86_PF	x86_pf
#define X86_AF	x86_af
#define X86_ZF	x86_zf
#define X86_SF	x86_sf
#define X86_TF	x86_tf
#define X86_IFL	x86_ifl
#define X86_DF	x86_df
#define X86_OF	x86_of

#define makeflagsword() ( \
	2 | (uint16_t) x86_cf | ((uint16_t) x86_pf << 2) | ((uint16_t) x86_af << 4) | ((uint16_t) x86_zf << 6) | ((uint16_t) x86_sf << 7) | \
	((uint16_t) x86_tf << 8) | ((uint16_t) x86_ifl << 9) | ((uint16_t) x86_df << 10) | ((uint16_t) x86_of << 11) \
)

#define decodeflagsword(x) { \
	uint16_t temp16 = x; \
	x86_cf = temp16 & 1; \
	x86_pf = (temp16 >> 2) & 1; \
	x86_af = (temp16 >> 4) & 1; \
	x86_zf = (temp16 >> 6) & 1; \
	x86_sf = (temp16 >> 7) & 1; \
	x86_tf = (temp16 >> 8) & 1; \
	x86_ifl = (temp16 >> 9) & 1; \
	x86_df = (temp16 >> 10) & 1; \
	x86_of = (temp16 >> 11) & 1; \
} while(0)

#endif
