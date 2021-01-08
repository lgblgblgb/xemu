/*
 * Z80Ex, ZILoG Z80 CPU emulator.
 *
 * by Pigmaker57 aka boo_boo [pigmaker57@kahoh57.info]
 * modified by Gabor Lenart LGB [lgblgblgb@gmail.com] for Xep128 project
 * used by Gabor Lenart LGB [lgblgblgb@gmail.com] in Xemu project
 * my modifications (C)2015,2016,2017
 *
 * contains some code from the FUSE project (http://fuse-emulator.sourceforge.net)
 * Released under GNU GPL v2
 *
 */

#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include "xemu/z80ex/z80ex.h"
#include "xemu/z80ex/macros.h"

#include "xemu/emutools_basicdefs.h"

#define temp_byte z80ex.tmpbyte
#define temp_byte_s z80ex.tmpbyte_s
#define temp_addr z80ex.tmpaddr
#define temp_word z80ex.tmpword

typedef void (*z80ex_opcode_fn) (void);

#ifdef Z80EX_Z180_BY_DEFAULT
#define Z180_LIKELY XEMU_LIKELY
#else
#define Z180_LIKELY XEMU_UNLIKELY
#endif

#include "xemu/z80ex/ptables.c"
#include "xemu/z80ex/opcodes_base.c"
#include "xemu/z80ex/opcodes_dd.c"
#include "xemu/z80ex/opcodes_fd.c"
#include "xemu/z80ex/opcodes_cb.c"
#include "xemu/z80ex/opcodes_ed.c"
#include "xemu/z80ex/opcodes_ddcb.c"
#include "xemu/z80ex/opcodes_fdcb.c"
#include "xemu/z80ex/z180ex.c"

/* do one opcode (instruction or prefix) */
int z80ex_step(void)
{
	Z80EX_BYTE opcode, d;
	z80ex_opcode_fn ofn = NULL;

	z80ex.doing_opcode = 1;
	z80ex.noint_once = 0;
	z80ex.reset_PV_on_int = 0;
	z80ex.tstate = 0;
	z80ex.op_tstate = 0;

	opcode = READ_OP_M1(); 		/* fetch opcode */
	if (z80ex.int_vector_req)
	{
		TSTATES(2); 		/* interrupt eats two extra wait-states */
	}
	R++;				/* R increased by one on every first M1 cycle */

	T_WAIT_UNTIL(4);		/* M1 cycle eats min 4 t-states */

	if (!z80ex.prefix)
		opcodes_base[opcode]();
	else {
		if ((z80ex.prefix | 0x20) == 0xFD && ((opcode | 0x20) == 0xFD || opcode == 0xED)) {
			z80ex.prefix = opcode;
			z80ex.noint_once = 1; /* interrupts are not accepted immediately after prefix */
		} else {
			switch (z80ex.prefix) {
				case 0xDD:
				case 0xFD:
					if (opcode == 0xCB) {	/* FD/DD prefixed CB opcodes */
						d = READ_OP(); /* displacement */
						temp_byte_s = (d & 0x80)? -(((~d) & 0x7f)+1): d;
						opcode = READ_OP();
#ifdef Z80EX_Z180_SUPPORT
						if (Z180_LIKELY(z80ex.z180)) {
							if (XEMU_UNLIKELY((opcode & 7) != 6 || opcode == 0x36))
								ofn = trapping(z80ex.prefix, 0xCB, opcode, ITC_B3);
							else
								ofn = (z80ex.prefix == 0xDD) ? opcodes_ddcb[opcode] : opcodes_fdcb[opcode];
						} else
#endif
							ofn = (z80ex.prefix == 0xDD) ? opcodes_ddcb[opcode] : opcodes_fdcb[opcode];
					} else { /* FD/DD prefixed base opcodes */
#ifdef Z80EX_Z180_SUPPORT
						if (XEMU_UNLIKELY(z80ex.z180 && opcodes_ddfd_bad_for_z180[opcode])) {
							ofn = trapping(z80ex.prefix, 0x00, opcode, ITC_B2);
						} else {
#endif
							ofn = (z80ex.prefix == 0xDD) ? opcodes_dd[opcode] : opcodes_fd[opcode];
							if (XEMU_UNLIKELY(ofn == NULL)) ofn = opcodes_base[opcode]; /* 'mirrored' instructions NOTE: this should NOT happen with Z180! */
#ifdef Z80EX_Z180_SUPPORT
						}
#endif
					}
					break;

				case 0xED: /* ED opcodes */
#ifdef Z80EX_ED_TRAPPING_SUPPORT
					if (XEMU_UNLIKELY(opcode > 0xBB)) {
						/* check if ED-trap emu func accepted the opcode as its own "faked" */
						if (z80ex_ed_cb(opcode)) {
							ofn = opcodes_base[0x00];
							break;
						}
					}
#endif
#ifdef Z80EX_Z180_SUPPORT
					if (Z180_LIKELY(z80ex.z180))
						ofn = opcodes_ed_z180[opcode];
					else
#endif
						ofn = opcodes_ed[opcode];
					if (ofn == NULL) {
#ifdef Z80EX_Z180_SUPPORT
						if (XEMU_UNLIKELY(z80ex.z180))
							ofn = trapping(0x00, 0xED, opcode, ITC_B2);
						else
#endif
							ofn = opcodes_base[0x00];
					}
					break;

				case 0xCB: /* CB opcodes */
#ifdef Z80EX_Z180_SUPPORT
					if (XEMU_UNLIKELY(z80ex.z180 && (opcode & 0xF8) == 0x30))
						ofn = trapping(0x00, 0xCB, opcode, ITC_B2);
					else
#endif
						ofn = opcodes_cb[opcode];
					break;

				default:
					/* this must'nt happen! */
					assert(0);
					break;
			}

			ofn();

			z80ex.prefix = 0;
		}
	}

	z80ex.doing_opcode = 0;
	return z80ex.tstate;
}


void z80ex_reset()
{
	PC = 0x0000; IFF1 = IFF2 = 0; IM = IM0;
	AF= SP = BC = DE = HL = IX = IY = AF_ = BC_ = DE_ = HL_ = 0xffff;
	I = R = R7 = 0;
	z80ex.noint_once = 0; z80ex.reset_PV_on_int = 0; z80ex.halted = 0;
	z80ex.int_vector_req = 0;
	z80ex.doing_opcode = 0;
	z80ex.tstate = z80ex.op_tstate = 0;
	z80ex.prefix = 0;
#ifdef Z80EX_Z180_SUPPORT
	z80ex.internal_int_disable = 0;
#endif
}




/**/
void z80ex_init ( void )
{
	memset(&z80ex, 0x00, sizeof(Z80EX_CONTEXT));

	z80ex_reset();

	z80ex.nmos = 1;
#ifdef Z80EX_HAVE_TSTATE_CB_VAR
	z80ex.tstate_cb = 0;
#endif
#ifdef Z80EX_Z180_SUPPORT
	z80ex.internal_int_disable = 0;
	z80ex.z180 = 0;
#endif
}

/*non-maskable interrupt*/
int z80ex_nmi(void)
{
	if (z80ex.doing_opcode || z80ex.noint_once || z80ex.prefix) return 0;

	if (z80ex.halted) {
		/*so we met an interrupt... stop waiting*/
		PC++;
		z80ex.halted = 0;
	}

	z80ex.doing_opcode = 1;

	R++; /* accepting interrupt increases R by one */
	/*IFF2=IFF1;*/ /* contrary to zilog z80 docs, IFF2 is not modified on NMI. proved by Slava Tretiak aka restorer */
	IFF1 = 0;

	TSTATES(5);

	z80ex_mwrite_cb(--SP, z80ex.pc.b.h); /* PUSH PC -- high byte */
	TSTATES(3);

	z80ex_mwrite_cb(--SP, z80ex.pc.b.l); /* PUSH PC -- low byte */
	TSTATES(3);

	PC = 0x0066;
	MEMPTR = PC; /* FIXME: is that really so? */

	z80ex.doing_opcode = 0;

	return 11; /* NMI always takes 11 t-states */
}

/*maskable interrupt*/
int z80ex_int(void)
{
	Z80EX_WORD inttemp;
	Z80EX_BYTE iv;
	unsigned long tt;

	/* If the INT line is low and IFF1 is set, and there's no opcode executing just now,
	a maskable interrupt is accepted, whether or not the
	last INT routine has finished */
	if (
		!IFF1 || z80ex.noint_once || z80ex.doing_opcode || z80ex.prefix
#ifdef Z80EX_Z180_SUPPORT
		|| z80ex.internal_int_disable
#endif
	) return 0;

	z80ex.tstate = 0;
	z80ex.op_tstate = 0;

	if (z80ex.halted) {
		/* so we met an interrupt... stop waiting */
		PC++;
		z80ex.halted = 0;
	}

	/* When an INT is accepted, both IFF1 and IFF2 are cleared, preventing another interrupt from
	occurring which would end up as an infinite loop */
	IFF1 = IFF2 = 0;

	/* original (NMOS) zilog z80 bug: */
	/* If a LD A,I or LD A,R (which copy IFF2 to the P/V flag) is interrupted, then the P/V flag is reset, even if interrupts were enabled beforehand. */
	/* (this bug was fixed in CMOS version of z80) */
	if (z80ex.reset_PV_on_int) {
		F = (F & ~FLAG_P);
	}
	z80ex.reset_PV_on_int = 0;

	z80ex.int_vector_req = 1;
	z80ex.doing_opcode = 1;

	switch (IM) {
		case IM0:
			/* note: there's no need to do R++ and WAITs here, it'll be handled by z80ex_step */
			tt = z80ex_step();
			while(z80ex.prefix) { /* this is not the end? */
				tt+=z80ex_step();
			}
			z80ex.tstate = tt;
			break;

		case IM1:
			R++;
			TSTATES(2); /* two extra wait-states */
			/* An RST 38h is executed, no matter what value is put on the bus or what
			value the I register has. 13 t-states (2 extra + 11 for RST). */
			opcodes_base[0xff](); /* RST 38 */
			break;

		case IM2:
			R++;
			/* takes 19 clock periods to complete (seven to fetch the
			lower eight bits from the interrupting device, six to save the program
			counter, and six to obtain the jump address) */
			iv=READ_OP();
			T_WAIT_UNTIL(7);
			inttemp = (0x100 * I) + iv;

			PUSH(PC, 7, 10);

			READ_MEM(PCL, inttemp++, 13); READ_MEM(PCH, inttemp, 16);
			MEMPTR = PC;
			T_WAIT_UNTIL(19);

			break;
	}

	z80ex.doing_opcode = 0;
	z80ex.int_vector_req = 0;

	return z80ex.tstate;
}

void z80ex_w_states(unsigned w_states)
{
	TSTATES(w_states);
}

void z80ex_next_t_state(void)
{
#ifdef Z80EX_TSTATE_CALLBACK
	if (IS_TSTATE_CB) z80ex_tstate_cb();
#endif
	z80ex.tstate++;
	z80ex.op_tstate++;
}

/*int z80ex_get_noint_once(Z80EX_CONTEXT *cpu)
{
	return z80ex.noint_once;
}*/

/*returns 1 if maskable interrupts are possible in current z80 state*/
int z80ex_int_possible(void)
{
        return ((!IFF1 || z80ex.noint_once || z80ex.doing_opcode || z80ex.prefix) ? 0 : 1);
}

/*returns 1 if non-maskable interrupts are possible in current z80 state*/
int z80ex_nmi_possible(void)
{
        return ((z80ex.noint_once || z80ex.doing_opcode || z80ex.prefix) ? 0 : 1);
}

