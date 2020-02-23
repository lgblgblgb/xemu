/* Xep128: Minimalistic Enterprise-128 emulator with focus on "exotic" hardware
   Copyright (C)2015,2016 LGB (Gábor Lénárt) <lgblgblgb@gmail.com>
   http://xep128.lgb.hu/

   Additional quirky Z180 emulation for Z80Ex. You should read
   file README about the (lack of ...) features.

   FIXME: This must be integrated into opcodes_ed.c!

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

#ifdef Z80EX_Z180_SUPPORT

#warning "LGB: Z180 support is _very_ limited, please read file z80ex/README."

#define MULT_OP_T_STATES 13
#define TST_OP_T_STATES 4

#define ITC_B3	0xC0
#define ITC_B2	0x80



#define TST(value)\
{\
        F = FLAG_H | sz53p_table[A & (value)];\
}



static void z180_trap ( void ) {
	opcodes_base[0xC7]();	/* = RST 0 */
}

static const z80ex_opcode_fn trapping ( Z80EX_BYTE prefix, Z80EX_BYTE series, Z80EX_BYTE opcode, Z80EX_BYTE itc76 ) {

	PC--; /* back to the byte caused the invalid opcode trap */
	if (prefix && series == 0xCB) PC--; /* odd enough, but it seems real Z180 stacks PC on the disp. byte in prefixed CB op!!! */
	/* call user handler stuff */
	z80ex_z180_cb(PC, prefix, series, opcode, itc76);
	return z180_trap; /* respond with trap handler, heh! */
}




static const int opcodes_ddfd_bad_for_z180[0x100] = {
/*	0 1 2 3 4 5 6 7 8 9 A B C D E F */

	1,1,1,1,1,1,1,1,1,0,1,1,1,1,1,1,	/* 0. */
	1,1,1,1,1,1,1,1,1,0,1,1,1,1,1,1,	/* 1. */
	1,0,0,0,1,1,1,1,1,0,0,0,1,1,1,1,	/* 2. */
	1,1,1,1,0,0,0,1,1,0,1,1,1,1,1,1,	/* 3. */
	1,1,1,1,1,1,0,1,1,1,1,1,1,1,0,1,	/* 4. */
	1,1,1,1,1,1,0,1,1,1,1,1,1,1,0,1,	/* 5. */
	1,1,1,1,1,1,0,1,1,1,1,1,1,1,0,1,	/* 6. */
	0,0,0,0,0,0,1,0,1,1,1,1,1,1,0,1,	/* 7. */
	1,1,1,1,1,1,0,1,1,1,1,1,1,1,0,1,	/* 8. */
	1,1,1,1,1,1,0,1,1,1,1,1,1,1,0,1,	/* 9. */
	1,1,1,1,1,1,0,1,1,1,1,1,1,1,0,1,	/* A. */
	1,1,1,1,1,1,0,1,1,1,1,1,1,1,0,1,	/* B. */
	1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,	/* C. */
	1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,	/* D. */
	1,0,1,0,1,0,1,1,1,0,1,1,1,1,1,1,	/* E. */
	1,1,1,1,1,1,1,1,1,0,1,1,1,1,1,1		/* F. */
};


static void zop_ED_0x00(void) {		/* 0xED 0x00 : IN0  B,(n) */
}
static void zop_ED_0x01(void) {		/* 0xED 0x01 : OUT0 (n),B */
}
static void zop_ED_0x04(void) {		/* 0xED 0x04 : TST  B */
	TST(B);
	T_WAIT_UNTIL(TST_OP_T_STATES);
}
static void zop_ED_0x08(void) {		/* 0xED 0x08 : IN0  C,(n) */
}
static void zop_ED_0x09(void) {		/* 0xED 0x09 : OUT0 (n),C */
}
static void zop_ED_0x0C(void) {		/* 0xED 0x0C : TST  C */
	TST(C);
	T_WAIT_UNTIL(TST_OP_T_STATES);
}
static void zop_ED_0x10(void) {		/* 0xED 0x10 : IN0  D,(n) */
}
static void zop_ED_0x11(void) {		/* 0xED 0x11 : OUT0 (n),D */
}
static void zop_ED_0x14(void) {		/* 0xED 0x14 : TST  D */
	TST(D);
	T_WAIT_UNTIL(TST_OP_T_STATES);
}
static void zop_ED_0x18(void) {		/* 0xED 0x18 : IN0  E,(n) */
}
static void zop_ED_0x19(void) {		/* 0xED 0x19 : OUT0 (n),E */
}
static void zop_ED_0x1C(void) {		/* 0xED 0x1C : TST  E */
	TST(E);
	T_WAIT_UNTIL(TST_OP_T_STATES);
}
static void zop_ED_0x20(void) {		/* 0xED 0x20 : IN0  H,(n) */
}
static void zop_ED_0x21(void) {		/* 0xED 0x21 : OUT0 (n),H */
}
static void zop_ED_0x24(void) {		/* 0xED 0x24 : TST  H */
	TST(H);
	T_WAIT_UNTIL(TST_OP_T_STATES);
}
static void zop_ED_0x28(void) {		/* 0xED 0x28 : IN0  L,(n) */
}
static void zop_ED_0x29(void) {		/* 0xED 0x29 : OUT0 (n),L */
}
static void zop_ED_0x2C(void) {		/* 0xED 0x2C : TST  L */
	TST(L);
	T_WAIT_UNTIL(TST_OP_T_STATES);
}
static void zop_ED_0x30(void) {		/* 0xED 0x30 : IN0  F,(n) */
}
static void zop_ED_0x34(void) {		/* 0xED 0x34 : TST  (HL) */
	READ_MEM(temp_byte, (HL), 4);
	TST(temp_byte);
	T_WAIT_UNTIL(7);
}
static void zop_ED_0x38(void) {		/* 0xED 0x38 : IN0  A,(n) */
}
static void zop_ED_0x39(void) {		/* 0xED 0x39 : OUT0 (n),A */
}
static void zop_ED_0x3C(void) {		/* 0xED 0x3C : TST  A  */
	TST(A);
	T_WAIT_UNTIL(TST_OP_T_STATES);
}
static void zop_ED_0x4C(void) {		/* 0xED 0x4C : MULT BC */
	BC = B * C;
	T_WAIT_UNTIL(MULT_OP_T_STATES);
}
static void zop_ED_0x5C(void) {		/* 0xED 0x5C : MULT DE */
	DE = D * E;
	T_WAIT_UNTIL(MULT_OP_T_STATES);
}
static void zop_ED_0x64(void) {		/* 0xED 0x64 : TST  n */
}
static void zop_ED_0x6C(void) {		/* 0xED 0x6C : MULT HL */
	HL = H * L;
	T_WAIT_UNTIL(MULT_OP_T_STATES);
}
static void zop_ED_0x74(void) {		/* 0xED 0x74 : TST  (C),n [TESTIO] */
}
static void zop_ED_0x76(void) {		/* 0xED 0x76 : SLP */
	HALT(); /* really, not sure what SLP does, since it's opcode 0xED 0x76, we try
	to execute 0x76 as it would be normal HALT opcode. The difference maybe only
	the "depth" CPU is powered down, so for the emulation it does not make too much
	different - I think - LGB */
	T_WAIT_UNTIL(4);
}
static void zop_ED_0x7C(void) {		/* 0xED 0x7C : MULT SP */
	SP = SPH * SPL;
	T_WAIT_UNTIL(MULT_OP_T_STATES);
}
static void zop_ED_0x83(void) {		/* 0xED 0x83 : OTIM */
}
static void zop_ED_0x8B(void) {		/* 0xED 0x8B : OTDM */
}
static void zop_ED_0x93(void) {		/* 0xED 0x93 : OTIMR */
}
static void zop_ED_0x9B(void) {		/* 0xED 0x9B : OTDMR */
}



static const z80ex_opcode_fn opcodes_ed_z180[0x100] = {
 zop_ED_0x00   , zop_ED_0x01   , NULL          , NULL          ,	/* 0x00 */
 zop_ED_0x04   , NULL          , NULL          , NULL          ,	/* 0x04 */
 zop_ED_0x08   , zop_ED_0x09   , NULL          , NULL          ,	/* 0x08 */
 zop_ED_0x0C   , NULL          , NULL          , NULL          ,	/* 0x0C */
 zop_ED_0x10   , zop_ED_0x11   , NULL          , NULL          ,	/* 0x10 */
 zop_ED_0x14   , NULL          , NULL          , NULL          ,	/* 0x14 */
 zop_ED_0x18   , zop_ED_0x19   , NULL          , NULL          ,	/* 0x18 */
 zop_ED_0x1C   , NULL          , NULL          , NULL          ,	/* 0x1C */
 zop_ED_0x20   , zop_ED_0x21   , NULL          , NULL          ,	/* 0x20 */
 zop_ED_0x24   , NULL          , NULL          , NULL          ,	/* 0x24 */
 zop_ED_0x28   , zop_ED_0x29   , NULL          , NULL          ,	/* 0x28 */
 zop_ED_0x2C   , NULL          , NULL          , NULL          ,	/* 0x2C */
 zop_ED_0x30   , NULL          , NULL          , NULL          ,	/* 0x30 */
 zop_ED_0x34   , NULL          , NULL          , NULL          ,	/* 0x34 */
 zop_ED_0x38   , zop_ED_0x39   , NULL          , NULL          ,	/* 0x38 */
 zop_ED_0x3C   , NULL          , NULL          , NULL          ,	/* 0x3C */
 op_ED_0x40    , op_ED_0x41    , op_ED_0x42    , op_ED_0x43    ,	/* 0x40 */
 op_ED_0x44    , op_ED_0x45    , op_ED_0x46    , op_ED_0x47    ,	/* 0x44 */
 op_ED_0x48    , op_ED_0x49    , op_ED_0x4a    , op_ED_0x4b    ,	/* 0x48 */
 zop_ED_0x4C   , op_ED_0x4d    , NULL          , op_ED_0x4f    ,	/* 0x4C */
 op_ED_0x50    , op_ED_0x51    , op_ED_0x52    , op_ED_0x53    ,	/* 0x50 */
 NULL          , NULL          , op_ED_0x56    , op_ED_0x57    ,	/* 0x54 */
 op_ED_0x58    , op_ED_0x59    , op_ED_0x5a    , op_ED_0x5b    ,	/* 0x58 */
 zop_ED_0x5C   , NULL          , op_ED_0x5e    , op_ED_0x5f    ,	/* 0x5C */
 op_ED_0x60    , op_ED_0x61    , op_ED_0x62    , op_ED_0x63    ,	/* 0x60 */
 zop_ED_0x64   , NULL          , NULL          , op_ED_0x67    ,	/* 0x64 */
 op_ED_0x68    , op_ED_0x69    , op_ED_0x6a    , op_ED_0x6b    ,	/* 0x68 */
 zop_ED_0x6C   , NULL          , NULL          , op_ED_0x6f    ,	/* 0x6C */
 op_ED_0x70    , NULL          , op_ED_0x72    , op_ED_0x73    ,	/* 0x70 */
 zop_ED_0x74   , NULL          , zop_ED_0x76   , NULL          ,	/* 0x74 */
 op_ED_0x78    , op_ED_0x79    , op_ED_0x7a    , op_ED_0x7b    ,	/* 0x78 */
 zop_ED_0x7C   , NULL          , NULL          , NULL          ,	/* 0x7C */
 NULL          , NULL          , NULL          , zop_ED_0x83   ,	/* 0x80 */
 NULL          , NULL          , NULL          , NULL          ,	/* 0x84 */
 NULL          , NULL          , NULL          , zop_ED_0x8B   ,	/* 0x88 */
 NULL          , NULL          , NULL          , NULL          ,	/* 0x8C */
 NULL          , NULL          , NULL          , zop_ED_0x93   ,	/* 0x90 */
 NULL          , NULL          , NULL          , NULL          ,	/* 0x94 */
 NULL          , NULL          , NULL          , zop_ED_0x9B   ,	/* 0x98 */
 NULL          , NULL          , NULL          , NULL          ,	/* 0x9C */
 op_ED_0xa0    , op_ED_0xa1    , op_ED_0xa2    , op_ED_0xa3    ,	/* 0xA0 */
 NULL          , NULL          , NULL          , NULL          ,	/* 0xA4 */
 op_ED_0xa8    , op_ED_0xa9    , op_ED_0xaa    , op_ED_0xab    ,	/* 0xA8 */
 NULL          , NULL          , NULL          , NULL          ,	/* 0xAC */
 op_ED_0xb0    , op_ED_0xb1    , op_ED_0xb2    , op_ED_0xb3    ,	/* 0xB0 */
 NULL          , NULL          , NULL          , NULL          ,	/* 0xB4 */
 op_ED_0xb8    , op_ED_0xb9    , op_ED_0xba    , op_ED_0xbb    ,	/* 0xB8 */
 NULL          , NULL          , NULL          , NULL          ,	/* 0xBC */
 NULL          , NULL          , NULL          , NULL          ,	/* 0xC0 */
 NULL          , NULL          , NULL          , NULL          ,	/* 0xC4 */
 NULL          , NULL          , NULL          , NULL          ,	/* 0xC8 */
 NULL          , NULL          , NULL          , NULL          ,	/* 0xCC */
 NULL          , NULL          , NULL          , NULL          ,	/* 0xD0 */
 NULL          , NULL          , NULL          , NULL          ,	/* 0xD4 */
 NULL          , NULL          , NULL          , NULL          ,	/* 0xD8 */
 NULL          , NULL          , NULL          , NULL          ,	/* 0xDC */
 NULL          , NULL          , NULL          , NULL          ,	/* 0xE0 */
 NULL          , NULL          , NULL          , NULL          ,	/* 0xE4 */
 NULL          , NULL          , NULL          , NULL          ,	/* 0xE8 */
 NULL          , NULL          , NULL          , NULL          ,	/* 0xEC */
 NULL          , NULL          , NULL          , NULL          ,	/* 0xF0 */
 NULL          , NULL          , NULL          , NULL          ,	/* 0xF4 */
 NULL          , NULL          , NULL          , NULL          ,	/* 0xF8 */
 NULL          , NULL          , NULL          , NULL			/* 0xFC */
};

#endif

