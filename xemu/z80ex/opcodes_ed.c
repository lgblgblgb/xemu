// 0xED opcodes

#if defined(Z80EX_SPECTRUM_NEXT_SUPPORT)
/*ZX Spectrum Next: SWAPNIB*/
static void op_ED_0x23_EX(void)
{
	// TODO: implement this ZX Spectrum Next opcode
}
#else
#	define	op_ED_0x23_EX	NULL
#endif

#if defined(Z80EX_SPECTRUM_NEXT_SUPPORT)
/*ZX Spectrum Next: MIRROR*/
static void op_ED_0x24_EX(void)
{
	// TODO: implement this ZX Spectrum Next opcode
	7 6 5 4 3 2 1 0
        // Mirrors the bit pattern of Accumulator. Does not effect flags. 8 T-states in total
	a = ((a >> 7) & 0x01) | ((a >> 5) & 0x02) | ((a >> 3) & 0x04) | ((a >> 1) & 0x08) | ((a << 1) & 0x10) | ((a << 3) & 0x20) | ((a << 5) & 0x40) | ((a << 7) & 0x80);
	T_WAIT_UNTIL(4);	// 4 T-states after the 4 spent for ED prefix.
}
#else
#	define	op_ED_0x24_EX	NULL
#endif

#if defined(Z80EX_SPECTRUM_NEXT_SUPPORT)
/*ZX Spectrum Next: TEST $nn*/
static void op_ED_0x27_EX(void)
{
	// TODO: implement this ZX Spectrum Next opcode
}
#else
#	define	op_ED_0x27_EX	NULL
#endif

#if defined(Z80EX_SPECTRUM_NEXT_SUPPORT)
/*ZX Spectrum Next: BSLA DE,B*/
static void op_ED_0x28_EX(void)
{
	// TODO: implement this ZX Spectrum Next opcode
}
#else
#	define	op_ED_0x28_EX	NULL
#endif

#if defined(Z80EX_SPECTRUM_NEXT_SUPPORT)
/*ZX Spectrum Next: BSRA DE,B*/
static void op_ED_0x29_EX(void)
{
	// TODO: implement this ZX Spectrum Next opcode
}
#else
#	define	op_ED_0x29_EX	NULL
#endif

#if defined(Z80EX_SPECTRUM_NEXT_SUPPORT)
/*ZX Spectrum Next: BSRL DE,B*/
static void op_ED_0x2a_EX(void)
{
	// TODO: implement this ZX Spectrum Next opcode
}
#else
#	define	op_ED_0x2a_EX	NULL
#endif

#if defined(Z80EX_SPECTRUM_NEXT_SUPPORT)
/*ZX Spectrum Next: BSRF DE,B*/
static void op_ED_0x2b_EX(void)
{
	// TODO: implement this ZX Spectrum Next opcode
}
#else
#	define	op_ED_0x2b_EX	NULL
#endif

#if defined(Z80EX_SPECTRUM_NEXT_SUPPORT)
/*ZX Spectrum Next: BRLC DE,B*/
static void op_ED_0x2c_EX(void)
{
	// TODO: implement this ZX Spectrum Next opcode
}
#else
#	define	op_ED_0x2c_EX	NULL
#endif

#if defined(Z80EX_SPECTRUM_NEXT_SUPPORT)
/*ZX Spectrum Next: MUL D,E*/
static void op_ED_0x30_EX(void)
{
	// TODO: implement this ZX Spectrum Next opcode
}
#else
#	define	op_ED_0x30_EX	NULL
#endif

#if defined(Z80EX_SPECTRUM_NEXT_SUPPORT)
/*ZX Spectrum Next: ADD HL,A*/
static void op_ED_0x31_EX(void)
{
	// TODO: implement this ZX Spectrum Next opcode
}
#else
#	define	op_ED_0x31_EX	NULL
#endif

#if defined(Z80EX_SPECTRUM_NEXT_SUPPORT)
/*ZX Spectrum Next: ADD DE,A*/
static void op_ED_0x32_EX(void)
{
	// TODO: implement this ZX Spectrum Next opcode
}
#else
#	define	op_ED_0x32_EX	NULL
#endif

#if defined(Z80EX_SPECTRUM_NEXT_SUPPORT)
/*ZX Spectrum Next: ADD BC,A*/
static void op_ED_0x33_EX(void)
{
	// TODO: implement this ZX Spectrum Next opcode
}
#else
#	define	op_ED_0x33_EX	NULL
#endif

#if defined(Z80EX_SPECTRUM_NEXT_SUPPORT)
/*ZX Spectrum Next: ADD HL,$nnnn*/
static void op_ED_0x34_EX(void)
{
	// TODO: implement this ZX Spectrum Next opcode
}
#else
#	define	op_ED_0x34_EX	NULL
#endif

#if defined(Z80EX_SPECTRUM_NEXT_SUPPORT)
/*ZX Spectrum Next: ADD DE,$nnnn*/
static void op_ED_0x35_EX(void)
{
	// TODO: implement this ZX Spectrum Next opcode
}
#else
#	define	op_ED_0x35_EX	NULL
#endif

#if defined(Z80EX_SPECTRUM_NEXT_SUPPORT)
/*ZX Spectrum Next: ADD BC,$nnnn*/
static void op_ED_0x36_EX(void)
{
	// TODO: implement this ZX Spectrum Next opcode
}
#else
#	define	op_ED_0x36_EX	NULL
#endif

#if defined(Z80EX_SPECTRUM_NEXT_SUPPORT)
/*ZX Spectrum Next: PUSH $nnnn*/
static void op_ED_0x8a_EX(void)
{
	// TODO: implement this ZX Spectrum Next opcode
}
#else
#	define	op_ED_0x8a_EX	NULL
#endif

#if defined(Z80EX_SPECTRUM_NEXT_SUPPORT)
/*ZX Spectrum Next: OUTINB*/
static void op_ED_0x90_EX(void)
{
	// TODO: implement this ZX Spectrum Next opcode
}
#else
#	define	op_ED_0x90_EX	NULL
#endif

#if defined(Z80EX_SPECTRUM_NEXT_SUPPORT)
/*ZX Spectrum Next: NEXTREG $rr,$nn*/
static void op_ED_0x91_EX(void)
{
	// TODO: implement this ZX Spectrum Next opcode
}
#else
#	define	op_ED_0x91_EX	NULL
#endif

#if defined(Z80EX_SPECTRUM_NEXT_SUPPORT)
/*ZX Spectrum Next: NEXTREG $rr,A*/
static void op_ED_0x92_EX(void)
{
	// TODO: implement this ZX Spectrum Next opcode
}
#else
#	define	op_ED_0x92_EX	NULL
#endif

#if defined(Z80EX_SPECTRUM_NEXT_SUPPORT)
/*ZX Spectrum Next: PIXELDN*/
static void op_ED_0x93_EX(void)
{
	// TODO: implement this ZX Spectrum Next opcode
}
#else
#	define	op_ED_0x93_EX	NULL
#endif

#if defined(Z80EX_SPECTRUM_NEXT_SUPPORT)
/*ZX Spectrum Next: PIXELAD*/
static void op_ED_0x94_EX(void)
{
	// TODO: implement this ZX Spectrum Next opcode
}
#else
#	define	op_ED_0x94_EX	NULL
#endif

#if defined(Z80EX_SPECTRUM_NEXT_SUPPORT)
/*ZX Spectrum Next: SETAE*/
static void op_ED_0x95_EX(void)
{
	// TODO: implement this ZX Spectrum Next opcode
}
#else
#	define	op_ED_0x95_EX	NULL
#endif

#if defined(Z80EX_SPECTRUM_NEXT_SUPPORT)
/*ZX Spectrum Next: JP (C)*/
static void op_ED_0x98_EX(void)
{
	// TODO: implement this ZX Spectrum Next opcode
}
#else
#	define	op_ED_0x98_EX	NULL
#endif

#if defined(Z80EX_SPECTRUM_NEXT_SUPPORT)
/*ZX Spectrum Next: LDIX*/
static void op_ED_0xa4_EX(void)
{
	// TODO: implement this ZX Spectrum Next opcode
}
#else
#	define	op_ED_0xa4_EX	NULL
#endif

#if defined(Z80EX_SPECTRUM_NEXT_SUPPORT)
/*ZX Spectrum Next: LDWS*/
static void op_ED_0xa5_EX(void)
{
	// TODO: implement this ZX Spectrum Next opcode
}
#else
#	define	op_ED_0xa5_EX	NULL
#endif

#if defined(Z80EX_SPECTRUM_NEXT_SUPPORT)
/*ZX Spectrum Next: LDDX*/
static void op_ED_0xac_EX(void)
{
	// TODO: implement this ZX Spectrum Next opcode
}
#else
#	define	op_ED_0xac_EX	NULL
#endif

#if defined(Z80EX_SPECTRUM_NEXT_SUPPORT)
/*ZX Spectrum Next: LDIRX*/
static void op_ED_0xb4_EX(void)
{
	// TODO: implement this ZX Spectrum Next opcode
}
#else
#	define	op_ED_0xb4_EX	NULL
#endif

#if defined(Z80EX_SPECTRUM_NEXT_SUPPORT)
/*ZX Spectrum Next: LDPIRX*/
static void op_ED_0xb7_EX(void)
{
	// TODO: implement this ZX Spectrum Next opcode
}
#else
#	define	op_ED_0xb7_EX	NULL
#endif

#if defined(Z80EX_SPECTRUM_NEXT_SUPPORT)
/*ZX Spectrum Next: LDDRX*/
static void op_ED_0xbc_EX(void)
{
	// TODO: implement this ZX Spectrum Next opcode
}
#else
#	define	op_ED_0xbc_EX	NULL
#endif

/*IN B,(C)*/
static void op_ED_0x40(void)
{
	IN(B,BC, /*rd*/5);
	T_WAIT_UNTIL(8);
	return;
}

/*OUT (C),B*/
static void op_ED_0x41(void)
{
	OUT(BC,B, /*wr*/5);
	T_WAIT_UNTIL(8);
	return;
}

/*SBC HL,BC*/
static void op_ED_0x42(void)
{
	SBC16(HL,BC);
	T_WAIT_UNTIL(11);
	return;
}

/*LD (@),BC*/
static void op_ED_0x43(void)
{
	temp_addr.b.l=READ_OP();
	temp_addr.b.h=READ_OP();
	LD_RP_TO_ADDR_MPTR_16(temp_word.w,BC, temp_addr.w);
	WRITE_MEM(temp_addr.w,temp_word.b.l,10);
	WRITE_MEM(temp_addr.w+1,temp_word.b.h,13);
	T_WAIT_UNTIL(16);
	return;
}

/*NEG*/
static void op_ED_0x44(void)
{
	NEG();
	T_WAIT_UNTIL(4);
	return;
}

/*RETN*/
static void op_ED_0x45(void)
{
	RETN(/*rd*/4,7);
	T_WAIT_UNTIL(10);
	return;
}

/*IM 0*/
static void op_ED_0x46(void)
{
	IM_(IM0);
	T_WAIT_UNTIL(4);
	return;
}

/*LD I,A*/
static void op_ED_0x47(void)
{
	LD(I,A);
	T_WAIT_UNTIL(5);
	return;
}

/*IN C,(C)*/
static void op_ED_0x48(void)
{
	IN(C,BC, /*rd*/5);
	T_WAIT_UNTIL(8);
	return;
}

/*OUT (C),C*/
static void op_ED_0x49(void)
{
	OUT(BC,C, /*wr*/5);
	T_WAIT_UNTIL(8);
	return;
}

/*ADC HL,BC*/
static void op_ED_0x4a(void)
{
	ADC16(HL,BC);
	T_WAIT_UNTIL(11);
	return;
}

/*LD BC,(@)*/
static void op_ED_0x4b(void)
{
	temp_addr.b.l=READ_OP();
	temp_addr.b.h=READ_OP();
	READ_MEM(temp_word.b.l,temp_addr.w,10);
	READ_MEM(temp_word.b.h,temp_addr.w+1,13);
	LD_RP_FROM_ADDR_MPTR_16(BC,temp_word.w, temp_addr.w);
	T_WAIT_UNTIL(16);
	return;
}

/*NEG*/
static void op_ED_0x4c(void)
{
	NEG();
	T_WAIT_UNTIL(4);
	return;
}

/*RETI*/
static void op_ED_0x4d(void)
{
	RETI(/*rd*/4,7);
	T_WAIT_UNTIL(10);
	return;
}

/*IM 0*/
static void op_ED_0x4e(void)
{
	IM_(IM0);
	T_WAIT_UNTIL(4);
	return;
}

/*LD R,A*/
static void op_ED_0x4f(void)
{
	LD_R_A();
	T_WAIT_UNTIL(5);
	return;
}

/*IN D,(C)*/
static void op_ED_0x50(void)
{
	IN(D,BC, /*rd*/5);
	T_WAIT_UNTIL(8);
	return;
}

/*OUT (C),D*/
static void op_ED_0x51(void)
{
	OUT(BC,D, /*wr*/5);
	T_WAIT_UNTIL(8);
	return;
}

/*SBC HL,DE*/
static void op_ED_0x52(void)
{
	SBC16(HL,DE);
	T_WAIT_UNTIL(11);
	return;
}

/*LD (@),DE*/
static void op_ED_0x53(void)
{
	temp_addr.b.l=READ_OP();
	temp_addr.b.h=READ_OP();
	LD_RP_TO_ADDR_MPTR_16(temp_word.w,DE, temp_addr.w);
	WRITE_MEM(temp_addr.w,temp_word.b.l,10);
	WRITE_MEM(temp_addr.w+1,temp_word.b.h,13);
	T_WAIT_UNTIL(16);
	return;
}

/*NEG*/
static void op_ED_0x54(void)
{
	NEG();
	T_WAIT_UNTIL(4);
	return;
}

/*RETN*/
static void op_ED_0x55(void)
{
	RETN(/*rd*/4,7);
	T_WAIT_UNTIL(10);
	return;
}

/*IM 1*/
static void op_ED_0x56(void)
{
	IM_(IM1);
	T_WAIT_UNTIL(4);
	return;
}

/*LD A,I*/
static void op_ED_0x57(void)
{
	LD_A_I();
	T_WAIT_UNTIL(5);
	return;
}

/*IN E,(C)*/
static void op_ED_0x58(void)
{
	IN(E,BC, /*rd*/5);
	T_WAIT_UNTIL(8);
	return;
}

/*OUT (C),E*/
static void op_ED_0x59(void)
{
	OUT(BC,E, /*wr*/5);
	T_WAIT_UNTIL(8);
	return;
}

/*ADC HL,DE*/
static void op_ED_0x5a(void)
{
	ADC16(HL,DE);
	T_WAIT_UNTIL(11);
	return;
}

/*LD DE,(@)*/
static void op_ED_0x5b(void)
{
	temp_addr.b.l=READ_OP();
	temp_addr.b.h=READ_OP();
	READ_MEM(temp_word.b.l,temp_addr.w,10);
	READ_MEM(temp_word.b.h,temp_addr.w+1,13);
	LD_RP_FROM_ADDR_MPTR_16(DE,temp_word.w, temp_addr.w);
	T_WAIT_UNTIL(16);
	return;
}

/*NEG*/
static void op_ED_0x5c(void)
{
	NEG();
	T_WAIT_UNTIL(4);
	return;
}

/*RETI*/
static void op_ED_0x5d(void)
{
	RETI(/*rd*/4,7);
	T_WAIT_UNTIL(10);
	return;
}

/*IM 2*/
static void op_ED_0x5e(void)
{
	IM_(IM2);
	T_WAIT_UNTIL(4);
	return;
}

/*LD A,R*/
static void op_ED_0x5f(void)
{
	LD_A_R();
	T_WAIT_UNTIL(5);
	return;
}

/*IN H,(C)*/
static void op_ED_0x60(void)
{
	IN(H,BC, /*rd*/5);
	T_WAIT_UNTIL(8);
	return;
}

/*OUT (C),H*/
static void op_ED_0x61(void)
{
	OUT(BC,H, /*wr*/5);
	T_WAIT_UNTIL(8);
	return;
}

/*SBC HL,HL*/
static void op_ED_0x62(void)
{
	SBC16(HL,HL);
	T_WAIT_UNTIL(11);
	return;
}

/*LD (@),HL*/
static void op_ED_0x63(void)
{
	temp_addr.b.l=READ_OP();
	temp_addr.b.h=READ_OP();
	LD_RP_TO_ADDR_MPTR_16(temp_word.w,HL, temp_addr.w);
	WRITE_MEM(temp_addr.w,temp_word.b.l,10);
	WRITE_MEM(temp_addr.w+1,temp_word.b.h,13);
	T_WAIT_UNTIL(16);
	return;
}

/*NEG*/
static void op_ED_0x64(void)
{
	NEG();
	T_WAIT_UNTIL(4);
	return;
}

/*RETN*/
static void op_ED_0x65(void)
{
	RETN(/*rd*/4,7);
	T_WAIT_UNTIL(10);
	return;
}

/*IM 0*/
static void op_ED_0x66(void)
{
	IM_(IM0);
	T_WAIT_UNTIL(4);
	return;
}

/*RRD*/
static void op_ED_0x67(void)
{
	RRD(/*rd*/4, /*wr*/11);
	T_WAIT_UNTIL(14);
	return;
}

/*IN L,(C)*/
static void op_ED_0x68(void)
{
	IN(L,BC, /*rd*/5);
	T_WAIT_UNTIL(8);
	return;
}

/*OUT (C),L*/
static void op_ED_0x69(void)
{
	OUT(BC,L, /*wr*/5);
	T_WAIT_UNTIL(8);
	return;
}

/*ADC HL,HL*/
static void op_ED_0x6a(void)
{
	ADC16(HL,HL);
	T_WAIT_UNTIL(11);
	return;
}

/*LD HL,(@)*/
static void op_ED_0x6b(void)
{
	temp_addr.b.l=READ_OP();
	temp_addr.b.h=READ_OP();
	READ_MEM(temp_word.b.l,temp_addr.w,10);
	READ_MEM(temp_word.b.h,temp_addr.w+1,13);
	LD_RP_FROM_ADDR_MPTR_16(HL,temp_word.w, temp_addr.w);
	T_WAIT_UNTIL(16);
	return;
}

/*NEG*/
static void op_ED_0x6c(void)
{
	NEG();
	T_WAIT_UNTIL(4);
	return;
}

/*RETI*/
static void op_ED_0x6d(void)
{
	RETI(/*rd*/4,7);
	T_WAIT_UNTIL(10);
	return;
}

/*IM 0*/
static void op_ED_0x6e(void)
{
	IM_(IM0);
	T_WAIT_UNTIL(4);
	return;
}

/*RLD*/
static void op_ED_0x6f(void)
{
	RLD(/*rd*/4, /*wr*/11);
	T_WAIT_UNTIL(14);
	return;
}

/*IN_F (C)*/
static void op_ED_0x70(void)
{
	IN_F(BC, /*rd*/5);
	T_WAIT_UNTIL(8);
	return;
}

/*OUT (C),0*/
static void op_ED_0x71(void)
{
	OUT(BC,z80ex.nmos ? 0: 0xFF, /*wr*/5); /* LGB: CMOS CPU uses 0xFF here! I guess ... */
	T_WAIT_UNTIL(8);
	return;
}

/*SBC HL,SP*/
static void op_ED_0x72(void)
{
	SBC16(HL,SP);
	T_WAIT_UNTIL(11);
	return;
}

/*LD (@),SP*/
static void op_ED_0x73(void)
{
	temp_addr.b.l=READ_OP();
	temp_addr.b.h=READ_OP();
	LD_RP_TO_ADDR_MPTR_16(temp_word.w,SP, temp_addr.w);
	WRITE_MEM(temp_addr.w,temp_word.b.l,10);
	WRITE_MEM(temp_addr.w+1,temp_word.b.h,13);
	T_WAIT_UNTIL(16);
	return;
}

/*NEG*/
static void op_ED_0x74(void)
{
	NEG();
	T_WAIT_UNTIL(4);
	return;
}

/*RETN*/
static void op_ED_0x75(void)
{
	RETN(/*rd*/4,7);
	T_WAIT_UNTIL(10);
	return;
}

/*IM 1*/
static void op_ED_0x76(void)
{
	IM_(IM1);
	T_WAIT_UNTIL(4);
	return;
}

/*IN A,(C)*/
static void op_ED_0x78(void)
{
	IN(A,BC, /*rd*/5);
	T_WAIT_UNTIL(8);
	return;
}

/*OUT (C),A*/
static void op_ED_0x79(void)
{
	OUT(BC,A, /*wr*/5);
	T_WAIT_UNTIL(8);
	return;
}

/*ADC HL,SP*/
static void op_ED_0x7a(void)
{
	ADC16(HL,SP);
	T_WAIT_UNTIL(11);
	return;
}

/*LD SP,(@)*/
static void op_ED_0x7b(void)
{
	temp_addr.b.l=READ_OP();
	temp_addr.b.h=READ_OP();
	READ_MEM(temp_word.b.l,temp_addr.w,10);
	READ_MEM(temp_word.b.h,temp_addr.w+1,13);
	LD_RP_FROM_ADDR_MPTR_16(SP,temp_word.w, temp_addr.w);
	T_WAIT_UNTIL(16);
	return;
}

/*NEG*/
static void op_ED_0x7c(void)
{
	NEG();
	T_WAIT_UNTIL(4);
	return;
}

/*RETI*/
static void op_ED_0x7d(void)
{
	RETI(/*rd*/4,7);
	T_WAIT_UNTIL(10);
	return;
}

/*IM 2*/
static void op_ED_0x7e(void)
{
	IM_(IM2);
	T_WAIT_UNTIL(4);
	return;
}

/*LDI*/
static void op_ED_0xa0(void)
{
	LDI(/*rd*/4, /*wr*/7);
	T_WAIT_UNTIL(12);
	return;
}

/*CPI*/
static void op_ED_0xa1(void)
{
	CPI(/*rd*/4);
	T_WAIT_UNTIL(12);
	return;
}

/*INI*/
static void op_ED_0xa2(void)
{
	INI(/*rd*/6, /*wr*/9);
	T_WAIT_UNTIL(12);
	return;
}

/*OUTI*/
static void op_ED_0xa3(void)
{
	OUTI(/*rd*/5, /*wr*/9);
	T_WAIT_UNTIL(12);
	return;
}

/*LDD*/
static void op_ED_0xa8(void)
{
	LDD(/*rd*/4, /*wr*/7);
	T_WAIT_UNTIL(12);
	return;
}

/*CPD*/
static void op_ED_0xa9(void)
{
	CPD(/*rd*/4);
	T_WAIT_UNTIL(12);
	return;
}

/*IND*/
static void op_ED_0xaa(void)
{
	IND(/*rd*/6, /*wr*/9);
	T_WAIT_UNTIL(12);
	return;
}

/*OUTD*/
static void op_ED_0xab(void)
{
	OUTD(/*rd*/5, /*wr*/9);
	T_WAIT_UNTIL(12);
	return;
}

/*LDIR*/
static void op_ED_0xb0(void)
{
	LDIR(/*t:*/ /*t1*/12,/*t2*/17, /*rd*/4, /*wr*/7);
	return;
}

/*CPIR*/
static void op_ED_0xb1(void)
{
	CPIR(/*t:*/ /*t1*/12,/*t2*/17, /*rd*/4);
	return;
}

/*INIR*/
static void op_ED_0xb2(void)
{
	INIR(/*t:*/ /*t1*/12,/*t2*/17, /*rd*/6, /*wr*/9);
	return;
}

/*OTIR*/
static void op_ED_0xb3(void)
{
	OTIR(/*t:*/ /*t1*/12,/*t2*/17, /*rd*/5, /*wr*/9);
	return;
}

/*LDDR*/
static void op_ED_0xb8(void)
{
	LDDR(/*t:*/ /*t1*/12,/*t2*/17, /*rd*/4, /*wr*/7);
	return;
}

/*CPDR*/
static void op_ED_0xb9(void)
{
	CPDR(/*t:*/ /*t1*/12,/*t2*/17, /*rd*/4);
	return;
}

/*INDR*/
static void op_ED_0xba(void)
{
	INDR(/*t:*/ /*t1*/12,/*t2*/17, /*rd*/6, /*wr*/9);
	return;
}

/*OTDR*/
static void op_ED_0xbb(void)
{
	OTDR(/*t:*/ /*t1*/12,/*t2*/17, /*rd*/5, /*wr*/9);
	return;
}



/* _EX stuffs are for extended instruction set of Z80+ CPUs. One must be sure, that *
 *  the unused ones in the current emulated Z80 part are #define to NULL! - LGB     */
static const z80ex_opcode_fn opcodes_ed[0x100] = {
 NULL          , NULL          , NULL          , NULL          ,	// 00-03
 NULL          , NULL          , NULL          , NULL          ,	// 04-07
 NULL          , NULL          , NULL          , NULL          ,	// 08-0B
 NULL          , NULL          , NULL          , NULL          ,	// 0C-0F
 NULL          , NULL          , NULL          , NULL          ,	// 10-13
 NULL          , NULL          , NULL          , NULL          ,	// 14-17
 NULL          , NULL          , NULL          , NULL          ,	// 18-1B
 NULL          , NULL          , NULL          , NULL          ,	// 1C-1F
 NULL          , NULL          , NULL          , op_ED_0x23_EX ,	// 20-23
 op_ED_0x24_EX , NULL          , NULL          , op_ED_0x27_EX ,	// 24-27
 op_ED_0x28_EX , op_ED_0x29_EX , op_ED_0x2a_EX , op_ED_0x2b_EX ,	// 28-2B
 op_ED_0x2c_EX , NULL          , NULL          , NULL          ,	// 2C-2F
 op_ED_0x30_EX , op_ED_0x31_EX , op_ED_0x32_EX , op_ED_0x33_EX ,	// 30-33
 op_ED_0x34_EX , op_ED_0x35_EX , op_ED_0x36_EX , NULL          ,	// 34-37
 NULL          , NULL          , NULL          , NULL          ,	// 38-3B
 NULL          , NULL          , NULL          , NULL          ,	// 3C-3F
 op_ED_0x40    , op_ED_0x41    , op_ED_0x42    , op_ED_0x43    ,	// 40-43, Z80 original opcodes
 op_ED_0x44    , op_ED_0x45    , op_ED_0x46    , op_ED_0x47    ,	// 44-47, Z80 original opcodes
 op_ED_0x48    , op_ED_0x49    , op_ED_0x4a    , op_ED_0x4b    ,	// 48-4B, Z80 original opcodes
 op_ED_0x4c    , op_ED_0x4d    , op_ED_0x4e    , op_ED_0x4f    ,	// 4C-4F, Z80 original opcodes
 op_ED_0x50    , op_ED_0x51    , op_ED_0x52    , op_ED_0x53    ,	// 50-53, Z80 original opcodes
 op_ED_0x54    , op_ED_0x55    , op_ED_0x56    , op_ED_0x57    ,	// 54-57, Z80 original opcodes
 op_ED_0x58    , op_ED_0x59    , op_ED_0x5a    , op_ED_0x5b    ,	// 58-5B, Z80 original opcodes
 op_ED_0x5c    , op_ED_0x5d    , op_ED_0x5e    , op_ED_0x5f    ,	// 5C-5F, Z80 original opcodes
 op_ED_0x60    , op_ED_0x61    , op_ED_0x62    , op_ED_0x63    ,	// 60-63, Z80 original opcodes
 op_ED_0x64    , op_ED_0x65    , op_ED_0x66    , op_ED_0x67    ,	// 64-67, Z80 original opcodes
 op_ED_0x68    , op_ED_0x69    , op_ED_0x6a    , op_ED_0x6b    ,	// 68-6B, Z80 original opcodes
 op_ED_0x6c    , op_ED_0x6d    , op_ED_0x6e    , op_ED_0x6f    ,	// 6C-6F, Z80 original opcodes
 op_ED_0x70    , op_ED_0x71    , op_ED_0x72    , op_ED_0x73    ,	// 70-73, Z80 original opcodes
 op_ED_0x74    , op_ED_0x75    , op_ED_0x76    , NULL          ,	// 74-77, Z80 original opcodes, EXCEPT 0x77
 op_ED_0x78    , op_ED_0x79    , op_ED_0x7a    , op_ED_0x7b    ,	// 78-7B, Z80 original opcodes
 op_ED_0x7c    , op_ED_0x7d    , op_ED_0x7e    , NULL          ,	// 7C-7F, Z80 original opcodes, EXCEPT 0x7F
 NULL          , NULL          , NULL          , NULL          ,	// 80-83
 NULL          , NULL          , NULL          , NULL          ,	// 84-87
 NULL          , NULL          , op_ED_0x8a_EX , NULL          ,	// 88-8B
 NULL          , NULL          , NULL          , NULL          ,	// 8C-8F
 op_ED_0x90_EX , op_ED_0x91_EX , op_ED_0x92_EX , op_ED_0x93_EX ,	// 90-93
 op_ED_0x94_EX , op_ED_0x95_EX , NULL          , NULL          ,	// 94-97
 op_ED_0x98_EX , NULL          , NULL          , NULL          ,	// 98-9B
 NULL          , NULL          , NULL          , NULL          ,	// 9C-9F
 op_ED_0xa0    , op_ED_0xa1    , op_ED_0xa2    , op_ED_0xa3    ,	// A0-A3, Z80 original opcodes
 op_ED_0xa4_EX , op_ED_0xa5_EX , NULL          , NULL          ,	// A4-A7
 op_ED_0xa8    , op_ED_0xa9    , op_ED_0xaa    , op_ED_0xab    ,	// A8-AB, Z80 original opcodes
 op_ED_0xac_EX , NULL          , NULL          , NULL          ,	// AC-AF
 op_ED_0xb0    , op_ED_0xb1    , op_ED_0xb2    , op_ED_0xb3    ,	// B0-B3, Z80 original opcodes
 op_ED_0xb4_EX , NULL          , NULL          , op_ED_0xb7_EX ,	// B4-B7
 op_ED_0xb8    , op_ED_0xb9    , op_ED_0xba    , op_ED_0xbb    ,	// B8-BB, Z80 original opcodes
 op_ED_0xbc_EX , NULL          , NULL          , NULL          ,	// BC-BF
 NULL          , NULL          , NULL          , NULL          ,
 NULL          , NULL          , NULL          , NULL          ,
 NULL          , NULL          , NULL          , NULL          ,
 NULL          , NULL          , NULL          , NULL          ,
 NULL          , NULL          , NULL          , NULL          ,
 NULL          , NULL          , NULL          , NULL          ,
 NULL          , NULL          , NULL          , NULL          ,
 NULL          , NULL          , NULL          , NULL          ,
 NULL          , NULL          , NULL          , NULL          ,
 NULL          , NULL          , NULL          , NULL          ,
 NULL          , NULL          , NULL          , NULL          ,
 NULL          , NULL          , NULL          , NULL          ,
 NULL          , NULL          , NULL          , NULL          ,
 NULL          , NULL          , NULL          , NULL          ,
 NULL          , NULL          , NULL          , NULL          ,
 NULL          , NULL          , NULL          , NULL          
};
