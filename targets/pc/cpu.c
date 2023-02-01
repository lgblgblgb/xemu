/*
  Fake86: A portable, open-source 8086 PC emulator.
  Copyright (C)2010-2013 Mike Chambers
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

/* cpu.c: functions to emulate the 8086/V20 CPU in software. the heart of Fake86. */

#include "xemu/emutools.h"
#include "cpu.h"

#include "memory.h"




#define modregrm() do { \
	addrbyte = getmem8(x86_segregs[regcs], ip); \
	StepIP(1); \
	mode = addrbyte >> 6; \
	reg = (addrbyte >> 3) & 7; \
	rm = addrbyte & 7; \
	switch (mode) { \
		case 0: \
			if (rm == 6) { \
				disp16 = getmem16(x86_segregs[regcs], ip); \
				StepIP(2); \
			} \
			if (((rm == 2) || (rm == 3)) && !segoverride) { \
				useseg = x86_segregs[regss]; \
			} \
			break; \
		case 1: \
			disp16 = signext(getmem8(x86_segregs[regcs], ip)); \
			StepIP(1); \
			if (((rm == 2) || (rm == 3) || (rm == 6)) && !segoverride) { \
				useseg = x86_segregs[regss]; \
			} \
			break; \
		case 2: \
			disp16 = getmem16(x86_segregs[regcs], ip); \
			StepIP(2); \
			if (((rm == 2) || (rm == 3) || (rm == 6)) && !segoverride) { \
				useseg = x86_segregs[regss]; \
			} \
			break; \
		default: \
			disp8 = 0; \
			disp16 = 0; \
			break; \
	} \
} while(0)

#define StepIP(x)			ip += x
#define getmem8(x, y)			read86(segbase(x) + (y))
#define getmem16(x, y)			readw86(segbase(x) + (y))
#define putmem8(x, y, z)		write86(segbase(x) + (y), z)
#define putmem16(x, y, z)		writew86(segbase(x) + (y), z)
#define signext(value)			(int16_t)(int8_t)(value)
#define signext32(value)		(int32_t)(int16_t)(value)
#define getreg16(regid)			x86_regs.wordregs[regid]
#define getreg8(regid)			x86_regs.byteregs[byteregtable[regid]]
#define putreg16(regid, writeval)	x86_regs.wordregs[regid] = writeval
#define putreg8(regid, writeval)	x86_regs.byteregs[byteregtable[regid]] = writeval
#define getsegreg(regid)		x86_segregs[regid]
#define putsegreg(regid, writeval)	x86_segregs[regid] = writeval
#define segbase(x)			((uint32_t) (x) << 4)


static const uint8_t byteregtable[8] = { regal, regcl, regdl, regbl, regah, regch, regdh, regbh };

static const uint8_t parity[0x100] = {
	1, 0, 0, 1, 0, 1, 1, 0, 0, 1, 1, 0, 1, 0, 0, 1, 0, 1, 1, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0, 1, 1, 0,
	0, 1, 1, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0, 1, 1, 0, 1, 0, 0, 1, 0, 1, 1, 0, 0, 1, 1, 0, 1, 0, 0, 1,
	0, 1, 1, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0, 1, 1, 0, 1, 0, 0, 1, 0, 1, 1, 0, 0, 1, 1, 0, 1, 0, 0, 1,
	1, 0, 0, 1, 0, 1, 1, 0, 0, 1, 1, 0, 1, 0, 0, 1, 0, 1, 1, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0, 1, 1, 0,
	0, 1, 1, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0, 1, 1, 0, 1, 0, 0, 1, 0, 1, 1, 0, 0, 1, 1, 0, 1, 0, 0, 1,
	1, 0, 0, 1, 0, 1, 1, 0, 0, 1, 1, 0, 1, 0, 0, 1, 0, 1, 1, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0, 1, 1, 0,
	1, 0, 0, 1, 0, 1, 1, 0, 0, 1, 1, 0, 1, 0, 0, 1, 0, 1, 1, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0, 1, 1, 0,
	0, 1, 1, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0, 1, 1, 0, 1, 0, 0, 1, 0, 1, 1, 0, 0, 1, 1, 0, 1, 0, 0, 1
};

static uint8_t	opcode, segoverride, reptype, hltstate = 0;
static uint16_t savecs, saveip, useseg, oldsp;
static uint8_t	oldcf, mode, reg, rm;
static uint16_t oper1, oper2, res16, disp16, temp16, dummy;
static uint8_t	oper1b, oper2b, res8, disp8, addrbyte;
static uint32_t temp1, temp2, temp3, ea;
static uint64_t totalexec;

#ifdef USE_PREFETCH_QUEUE
static uint8_t prefetch[6];
static uint32_t prefetch_base = 0;
#endif


union x86_bytewordregs_union x86_regs;
uint16_t x86_segregs[4];
uint8_t	 x86_cf, x86_pf, x86_af, x86_zf, x86_sf, x86_tf, x86_ifl, x86_df, x86_of;
uint16_t x86_ip;

#define ip	x86_ip
#define cf	x86_cf
#define pf	x86_pf
#define af	x86_af
#define zf	x86_zf
#define sf	x86_sf
#define tf	x86_tf
#define ifl	x86_ifl
#define df	x86_df
#define of	x86_of


static void intcall86 ( const uint8_t intnum ) ;


static inline void flag_szp8 ( const uint8_t value )
{
	zf = !value;
	sf = !!(value & 0x80);
	pf = parity[value];
}

static inline void flag_szp16 ( const uint16_t value )
{
	zf = !value;
	sf = !!(value & 0x8000);
	pf = parity[value & 0xFF];
}

static inline void flag_log8 ( const uint8_t value )
{
	flag_szp8(value);
	cf = 0;
	of = 0;		// bitwise logic ops always clear carry and overflow
}

static inline void flag_log16 ( const uint16_t value )
{
	flag_szp16(value);
	cf = 0;
	of = 0;		// bitwise logic ops always clear carry and overflow
}

static inline void flag_adc8 ( const uint8_t v1, const uint8_t v2, const uint8_t v3 )
{
	// v1 = destination operand, v2 = source operand, v3 = carry flag
	const uint16_t dst = (uint16_t)v1 + (uint16_t)v2 + (uint16_t)v3;
	flag_szp8((uint8_t)dst);
	of = !!((dst ^ v1) & (dst ^ v2) & 0x80);
	cf = !!(dst & 0xFF00);
	af = !!((v1 ^ v2 ^ dst) & 0x10);
}

static inline void flag_add8 ( const uint8_t v1, const uint8_t v2)
{
	flag_adc8(v1, v2, 0);
}

static inline void flag_adc16 ( const uint16_t v1, const uint16_t v2, const uint16_t v3 )
{

	const uint32_t dst = (uint32_t)v1 + (uint32_t)v2 + (uint32_t)v3;
	flag_szp16((uint16_t)dst);
	of = !!(((dst ^ v1) & (dst ^ v2)) & 0x8000);
	cf = !!(dst & 0xFFFF0000U);
	af = !!((v1 ^ v2 ^ dst) & 0x10);
}

static inline void flag_add16 ( const uint16_t v1, const uint16_t v2 )
{
	flag_adc16(v1, v2, 0);
}

static inline void flag_sbb8 ( const uint8_t v1, const uint8_t v2_in, const uint8_t v3_in )
{
	// v1 = destination operand, v2 = source operand, v3 = carry flag
	const uint8_t v2p3 = v2_in + v3_in;
	const uint16_t dst = (uint16_t)v1 - (uint16_t)v2p3;
	flag_szp8((uint8_t)dst);
	of = !!((dst ^ v1) & (v1 ^ v2p3) & 0x80);
	cf = !!(dst & 0xFF00);
	af = !!((v1 ^ v2p3 ^ dst) & 0x10);
}

static inline void flag_sub8 ( const uint8_t v1, const uint8_t v2 )
{
	flag_sbb8(v1, v2, 0);
}

static inline void flag_sbb16 ( const uint16_t v1, const uint16_t v2_in, const uint16_t v3_in )
{
	// v1 = destination operand, v2 = source operand, v3 = carry flag
	const uint16_t v2p3 = v2_in + v3_in;
	const uint32_t dst = (uint32_t)v1 - (uint32_t)v2p3;
	flag_szp16((uint16_t)dst);
	of = !!((dst ^ v1) & (v1 ^ v2p3) & 0x8000);
	cf = !!(dst & 0xFFFF0000U);
	af = !!((v1 ^ v2p3 ^ dst) & 0x10);
}

static inline void flag_sub16 ( const uint16_t v1, const uint16_t v2 )
{
	flag_sbb16(v1, v2, 0);
}

static inline void op_adc8 ( void )
{
	res8 = oper1b + oper2b + cf;
	flag_adc8(oper1b, oper2b, cf);
}

static inline void op_adc16 ( void )
{
	res16 = oper1 + oper2 + cf;
	flag_adc16(oper1, oper2, cf);
}

static inline void op_add8 ( void )
{
	res8 = oper1b + oper2b;
	flag_add8(oper1b, oper2b);
}

static inline void op_add16 ( void )
{
	res16 = oper1 + oper2;
	flag_add16(oper1, oper2);
}

static inline void op_and8 ( void )
{
	res8 = oper1b & oper2b;
	flag_log8(res8);
}

static inline void op_and16 ( void )
{
	res16 = oper1 & oper2;
	flag_log16(res16);
}

static inline void op_or8 ( void )
{
	res8 = oper1b | oper2b;
	flag_log8(res8);
}

static inline void op_or16 ( void )
{
	res16 = oper1 | oper2;
	flag_log16(res16);
}

static inline void op_xor8 ( void )
{
	res8 = oper1b ^ oper2b;
	flag_log8(res8);
}

static inline void op_xor16 ( void )
{
	res16 = oper1 ^ oper2;
	flag_log16(res16);
}

static inline void op_sub8 ( void )
{
	res8 = oper1b - oper2b;
	flag_sub8(oper1b, oper2b);
}

static inline void op_sub16 ( void )
{
	res16 = oper1 - oper2;
	flag_sub16(oper1, oper2);
}

static inline void op_sbb8 ( void )
{
	res8 = oper1b - (oper2b + cf);
	flag_sbb8(oper1b, oper2b, cf);
}

static inline void op_sbb16 ( void )
{
	res16 = oper1 - (oper2 + cf);
	flag_sbb16(oper1, oper2, cf);
}

static void getea ( const uint8_t rmval )
{
	uint32_t tempea = 0;
	switch (mode) {
		case 0:
			switch (rmval) {
				case 0:
					tempea = X86_BX + X86_SI;
					break;
				case 1:
					tempea = X86_BX + X86_DI;
					break;
				case 2:
					tempea = X86_BP + X86_SI;
					break;
				case 3:
					tempea = X86_BP + X86_DI;
					break;
				case 4:
					tempea = X86_SI;
					break;
				case 5:
					tempea = X86_DI;
					break;
				case 6:
					tempea = disp16;
					break;
				case 7:
					tempea = X86_BX;
					break;
			}
			break;

		case 1:
		case 2:
			switch (rmval) {
				case 0:
					tempea = X86_BX + X86_SI + disp16;
					break;
				case 1:
					tempea = X86_BX + X86_DI + disp16;
					break;
				case 2:
					tempea = X86_BP + X86_SI + disp16;
					break;
				case 3:
					tempea = X86_BP + X86_DI + disp16;
					break;
				case 4:
					tempea = X86_SI + disp16;
					break;
				case 5:
					tempea = X86_DI + disp16;
					break;
				case 6:
					tempea = X86_BP + disp16;
					break;
				case 7:
					tempea = X86_BX + disp16;
					break;
			}
			break;
	}
	ea = (tempea & 0xFFFF) + (useseg << 4);
}

static void push ( const uint16_t pushval )
{
	X86_SP = X86_SP - 2;
	putmem16(X86_SS, X86_SP, pushval);
}

static uint16_t pop ( void )
{
	uint16_t tempval = getmem16(X86_SS, X86_SP);
	X86_SP = X86_SP + 2;
	return tempval;
}

void iret86 ( void )
{
	ip = pop();
	X86_CS = pop();
	decodeflagsword(pop());
}

void reset86 ( void )
{
	X86_CS = 0xFFFF;
	ip = 0x0000;
	hltstate = 0;
	DEBUGPRINT("CPU: reset, CS:IP = %04X:%04X" NL, X86_CS, X86_IP);
}

static inline uint16_t readrm16 ( const uint8_t rmval )
{
	if (mode < 3) {
		getea(rmval);
		return read86(ea) | ((uint16_t)read86(ea + 1) << 8);
	} else
		return getreg16(rmval);
}

static inline uint8_t readrm8 ( const uint8_t rmval )
{
	if (mode < 3) {
		getea(rmval);
		return read86(ea);
	} else
		return getreg8(rmval);
}

static inline void writerm16 ( const uint8_t rmval, const uint16_t value )
{
	if (mode < 3) {
		getea(rmval);
		write86(ea, value & 0xFF);
		write86(ea + 1, value >> 8);
	} else
		putreg16(rmval, value);
}

static inline void writerm8 ( const uint8_t rmval, const uint8_t value )
{
	if (mode < 3) {
		getea(rmval);
		write86(ea, value);
	} else
		putreg8(rmval, value);
}

static uint8_t op_grp2_8 ( uint8_t cnt )
{
	uint16_t s = oper1b;
	uint16_t	shift;
	uint16_t oldcf = cf;
	uint16_t	msb;
#ifdef CPU_LIMIT_SHIFT_COUNT
	cnt &= 0x1F;
#endif
	switch (reg) {
		case 0: /* ROL r/m8 */
			for (shift = 1; shift <= cnt; shift++) {
				if (s & 0x80) {
							cf = 1;
						}
					else {
							cf = 0;
						}

					s = s << 1;
					s = s | cf;
			}

			if (cnt == 1) {
					//of = cf ^ ( (s >> 7) & 1);
				if ((s & 0x80) && cf) of = 1; else of = 0;
				} else of = 0;
			break;

		case 1: /* ROR r/m8 */
			for (shift = 1; shift <= cnt; shift++) {
					cf = s & 1;
					s = (s >> 1) | (cf << 7);
				}

			if (cnt == 1) {
					of = (s >> 7) ^ ( (s >> 6) & 1);
				}
			break;

		case 2: /* RCL r/m8 */
			for (shift = 1; shift <= cnt; shift++) {
					oldcf = cf;
					if (s & 0x80) {
							cf = 1;
						}
					else {
							cf = 0;
						}

					s = s << 1;
					s = s | oldcf;
				}

			if (cnt == 1) {
					of = cf ^ ( (s >> 7) & 1);
				}
			break;

		case 3: /* RCR r/m8 */
			for (shift = 1; shift <= cnt; shift++) {
					oldcf = cf;
					cf = s & 1;
					s = (s >> 1) | (oldcf << 7);
				}

			if (cnt == 1) {
					of = (s >> 7) ^ ( (s >> 6) & 1);
				}
			break;

		case 4:
		case 6: /* SHL r/m8 */
			for (shift = 1; shift <= cnt; shift++) {
					if (s & 0x80) {
							cf = 1;
						}
					else {
							cf = 0;
						}

					s = (s << 1) & 0xFF;
				}

			if ( (cnt == 1) && (cf == (s >> 7) ) ) {
					of = 0;
				}
			else {
					of = 1;
				}

			flag_szp8 ( (uint8_t) s);
			break;

		case 5: /* SHR r/m8 */
			if ( (cnt == 1) && (s & 0x80) ) {
					of = 1;
				}
			else {
					of = 0;
				}

			for (shift = 1; shift <= cnt; shift++) {
					cf = s & 1;
					s = s >> 1;
				}

			flag_szp8 ( (uint8_t) s);
			break;

		case 7: /* SAR r/m8 */
			for (shift = 1; shift <= cnt; shift++) {
					msb = s & 0x80;
					cf = s & 1;
					s = (s >> 1) | msb;
				}

			of = 0;
			flag_szp8 ( (uint8_t) s);
			break;
	}
	return s & 0xFF;
}

static uint16_t op_grp2_16 (uint8_t cnt) {

	uint32_t	s;
	uint32_t	shift;
	uint32_t	oldcf;
	uint32_t	msb;

	s = oper1;
	oldcf = cf;
#ifdef CPU_LIMIT_SHIFT_COUNT
	cnt &= 0x1F;
#endif
	switch (reg) {
			case 0: /* ROL r/m8 */
				for (shift = 1; shift <= cnt; shift++) {
						if (s & 0x8000) {
								cf = 1;
							}
						else {
								cf = 0;
							}

						s = s << 1;
						s = s | cf;
					}

				if (cnt == 1) {
						of = cf ^ ( (s >> 15) & 1);
					}
				break;

			case 1: /* ROR r/m8 */
				for (shift = 1; shift <= cnt; shift++) {
						cf = s & 1;
						s = (s >> 1) | (cf << 15);
					}

				if (cnt == 1) {
						of = (s >> 15) ^ ( (s >> 14) & 1);
					}
				break;

			case 2: /* RCL r/m8 */
				for (shift = 1; shift <= cnt; shift++) {
						oldcf = cf;
						if (s & 0x8000) {
								cf = 1;
							}
						else {
								cf = 0;
							}

						s = s << 1;
						s = s | oldcf;
					}

				if (cnt == 1) {
						of = cf ^ ( (s >> 15) & 1);
					}
				break;

			case 3: /* RCR r/m8 */
				for (shift = 1; shift <= cnt; shift++) {
						oldcf = cf;
						cf = s & 1;
						s = (s >> 1) | (oldcf << 15);
					}

				if (cnt == 1) {
						of = (s >> 15) ^ ( (s >> 14) & 1);
					}
				break;

			case 4:
			case 6: /* SHL r/m8 */
				for (shift = 1; shift <= cnt; shift++) {
						if (s & 0x8000) {
								cf = 1;
							}
						else {
								cf = 0;
							}

						s = (s << 1) & 0xFFFF;
					}

				if ( (cnt == 1) && (cf == (s >> 15) ) ) {
						of = 0;
					}
				else {
						of = 1;
					}

				flag_szp16 ( (uint16_t) s);
				break;

			case 5: /* SHR r/m8 */
				if ( (cnt == 1) && (s & 0x8000) ) {
						of = 1;
					}
				else {
						of = 0;
					}

				for (shift = 1; shift <= cnt; shift++) {
						cf = s & 1;
						s = s >> 1;
					}

				flag_szp16 ( (uint16_t) s);
				break;

			case 7: /* SAR r/m8 */
				for (shift = 1; shift <= cnt; shift++) {
						msb = s & 0x8000;
						cf = s & 1;
						s = (s >> 1) | msb;
					}

				of = 0;
				flag_szp16 ( (uint16_t) s);
				break;
		}

	return (uint16_t) s & 0xFFFF;
}

static void op_div8 (uint16_t valdiv, uint8_t divisor) {
	if (divisor == 0) {
			intcall86 (0);
			return;
		}

	if ( (valdiv / (uint16_t) divisor) > 0xFF) {
			intcall86 (0);
			return;
		}

	X86_AH = valdiv % (uint16_t) divisor;
	X86_AL = valdiv / (uint16_t) divisor;
}

static void op_idiv8 (uint16_t valdiv, uint8_t divisor) {

	uint16_t	s1;
	uint16_t	s2;
	uint16_t	d1;
	uint16_t	d2;
	int	sign;

	if (divisor == 0) {
			intcall86 (0);
			return;
		}

	s1 = valdiv;
	s2 = divisor;
	sign = ( ( (s1 ^ s2) & 0x8000) != 0);
	s1 = (s1 < 0x8000) ? s1 : ( (~s1 + 1) & 0xffff);
	s2 = (s2 < 0x8000) ? s2 : ( (~s2 + 1) & 0xffff);
	d1 = s1 / s2;
	d2 = s1 % s2;
	if (d1 & 0xFF00) {
			intcall86 (0);
			return;
		}

	if (sign) {
			d1 = (~d1 + 1) & 0xff;
			d2 = (~d2 + 1) & 0xff;
		}

	X86_AH = (uint8_t) d2;
	X86_AL = (uint8_t) d1;
}

static void op_grp3_8 ( void ) {
	oper1 = signext (oper1b);
	oper2 = signext (oper2b);
	switch (reg) {
			case 0:
			case 1: /* TEST */
				flag_log8 (oper1b & getmem8 (X86_CS, ip) );
				StepIP (1);
				break;

			case 2: /* NOT */
				res8 = ~oper1b;
				break;

			case 3: /* NEG */
				res8 = (~oper1b) + 1;
				flag_sub8 (0, oper1b);
				if (res8 == 0) {
						cf = 0;
					}
				else {
						cf = 1;
					}
				break;

			case 4: /* MUL */
				temp1 = (uint32_t) oper1b * (uint32_t) X86_AL;
				X86_AX = temp1 & 0xFFFF;
				flag_szp8 ( (uint8_t) temp1);
				if (X86_AH) {
						cf = 1;
						of = 1;
					}
				else {
						cf = 0;
						of = 0;
					}
#ifdef CPU_CLEAR_ZF_ON_MUL
				zf = 0;
#endif
				break;

			case 5: /* IMUL */
				oper1 = signext (oper1b);
				temp1 = signext (X86_AL);
				temp2 = oper1;
				if ( (temp1 & 0x80) == 0x80) {
						temp1 = temp1 | 0xFFFFFF00;
					}

				if ( (temp2 & 0x80) == 0x80) {
						temp2 = temp2 | 0xFFFFFF00;
					}

				temp3 = (temp1 * temp2) & 0xFFFF;
				X86_AX = temp3 & 0xFFFF;
				if (X86_AH) {
						cf = 1;
						of = 1;
					}
				else {
						cf = 0;
						of = 0;
					}
#ifdef CPU_CLEAR_ZF_ON_MUL
				zf = 0;
#endif
				break;

			case 6: /* DIV */
				op_div8 (X86_AX, oper1b);
				break;

			case 7: /* IDIV */
				op_idiv8 (X86_AX, oper1b);
				break;
		}
}

static void op_div16 (uint32_t valdiv, uint16_t divisor) {
	if (divisor == 0) {
			intcall86 (0);
			return;
		}

	if ( (valdiv / (uint32_t) divisor) > 0xFFFF) {
			intcall86 (0);
			return;
		}

	X86_DX = valdiv % (uint32_t) divisor;
	X86_AX = valdiv / (uint32_t) divisor;
}

static void op_idiv16 (uint32_t valdiv, uint16_t divisor) {

	uint32_t	d1;
	uint32_t	d2;
	uint32_t	s1;
	uint32_t	s2;
	int	sign;

	if (divisor == 0) {
			intcall86 (0);
			return;
		}

	s1 = valdiv;
	s2 = divisor;
	s2 = (s2 & 0x8000) ? (s2 | 0xffff0000) : s2;
	sign = ( ( (s1 ^ s2) & 0x80000000) != 0);
	s1 = (s1 < 0x80000000) ? s1 : ( (~s1 + 1) & 0xffffffff);
	s2 = (s2 < 0x80000000) ? s2 : ( (~s2 + 1) & 0xffffffff);
	d1 = s1 / s2;
	d2 = s1 % s2;
	if (d1 & 0xFFFF0000) {
			intcall86 (0);
			return;
		}

	if (sign) {
			d1 = (~d1 + 1) & 0xffff;
			d2 = (~d2 + 1) & 0xffff;
		}

	X86_AX = d1;
	X86_DX = d2;
}

static void op_grp3_16 ( void ) {
	switch (reg) {
			case 0:
			case 1: /* TEST */
				flag_log16 (oper1 & getmem16 (X86_CS, ip) );
				StepIP (2);
				break;

			case 2: /* NOT */
				res16 = ~oper1;
				break;

			case 3: /* NEG */
				res16 = (~oper1) + 1;
				flag_sub16 (0, oper1);
				if (res16) {
						cf = 1;
					}
				else {
						cf = 0;
					}
				break;

			case 4: /* MUL */
				temp1 = (uint32_t) oper1 * (uint32_t) X86_AX;
				X86_AX = temp1 & 0xFFFF;
				X86_DX = temp1 >> 16;
				flag_szp16 ( (uint16_t) temp1);
				if (X86_DX) {
						cf = 1;
						of = 1;
					}
				else {
						cf = 0;
						of = 0;
					}
#ifdef CPU_CLEAR_ZF_ON_MUL
				zf = 0;
#endif
				break;

			case 5: /* IMUL */
				temp1 = X86_AX;
				temp2 = oper1;
				if (temp1 & 0x8000) {
						temp1 |= 0xFFFF0000;
					}

				if (temp2 & 0x8000) {
						temp2 |= 0xFFFF0000;
					}

				temp3 = temp1 * temp2;
				X86_AX = temp3 & 0xFFFF;	/* into register ax */
				X86_DX = temp3 >> 16;	/* into register dx */
				if (X86_DX) {
						cf = 1;
						of = 1;
					}
				else {
						cf = 0;
						of = 0;
					}
#ifdef CPU_CLEAR_ZF_ON_MUL
				zf = 0;
#endif
				break;

			case 6: /* DIV */
				op_div16 ( ( (uint32_t) X86_DX << 16) + X86_AX, oper1);
				break;

			case 7: /* DIV */
				op_idiv16 ( ( (uint32_t) X86_DX << 16) + X86_AX, oper1);
				break;
		}
}

static void op_grp5 ( void ) {
	switch (reg) {
			case 0: /* INC Ev */
				{
				oper2 = 1;
				const uint8_t tempcf = cf;
				op_add16();
				cf = tempcf;
				writerm16 (rm, res16);
				}
				break;

			case 1: /* DEC Ev */
				{
				oper2 = 1;
				const uint8_t tempcf = cf;
				op_sub16();
				cf = tempcf;
				writerm16 (rm, res16);
				}
				break;

			case 2: /* CALL Ev */
				push (ip);
				ip = oper1;
				break;

			case 3: /* CALL Mp */
				push (X86_CS);
				push (ip);
				getea (rm);
				ip = (uint16_t) read86 (ea) + (uint16_t) read86 (ea + 1) * 256;
				X86_CS = (uint16_t) read86 (ea + 2) + (uint16_t) read86 (ea + 3) * 256;
				break;

			case 4: /* JMP Ev */
				ip = oper1;
				break;

			case 5: /* JMP Mp */
				getea (rm);
				ip = (uint16_t) read86 (ea) + (uint16_t) read86 (ea + 1) * 256;
				X86_CS = (uint16_t) read86 (ea + 2) + (uint16_t) read86 (ea + 3) * 256;
				break;

			case 6: /* PUSH Ev */
				push (oper1);
				break;
		}
}



static void intcall86 (const uint8_t intnum)
{
	push(makeflagsword() );
	push(X86_CS);
	push(ip);
	X86_CS = getmem16(0, (uint16_t) intnum * 4 + 2);
	ip = getmem16(0, (uint16_t) intnum * 4);
	ifl = 0;
	tf = 0;
	//DEBUGPRINT("CPU: calling interrupt $%02X, vector table address %04X:%04X" NL, intnum, X86_CS, ip);
}




uint32_t exec86 (uint32_t execloops) {

	uint32_t loopcount = 0;
	uint8_t docontinue;
	static uint16_t firstip;
	static uint16_t trap_toggle = 0;

	for (loopcount = 0; loopcount < execloops; loopcount++) {

			//if ( (totalexec & TIMING_INTERVAL) == 0) timing();
			if (trap_toggle) {
				intcall86 (1);
			}

			if (tf) {
				trap_toggle = 1;
			} else {
				trap_toggle = 0;
			}
			if (hltstate)
				goto skipexecution;
			reptype = 0;
			segoverride = 0;
			useseg = X86_DS;
			docontinue = 0;
			firstip = ip;
			while (!docontinue) {
					X86_CS = X86_CS & 0xFFFF;
					ip = ip & 0xFFFF;
					savecs = X86_CS;
					saveip = ip;
#ifdef USE_PREFETCH_QUEUE
					ea = segbase(savecs) + (uint32_t)saveip;
					if ( (ea < prefetch_base) || (ea > (prefetch_base + 5)) ) {
							memcpy (&prefetch[0], &RAM[ea], 6);
							prefetch_base = ea;
						}
					opcode = prefetch[ea - prefetch_base];
#else
					opcode = getmem8 (X86_CS, ip);
#endif
					StepIP (1);

					switch (opcode) {
								/* segment prefix check */
							case 0x2E:	/* segment X86_CS */
								useseg = X86_CS;
								segoverride = 1;
								break;

							case 0x3E:	/* segment X86_DS */
								useseg = X86_DS;
								segoverride = 1;
								break;

							case 0x26:	/* segment X86_ES */
								useseg = X86_ES;
								segoverride = 1;
								break;

							case 0x36:	/* segment X86_SS */
								useseg = X86_SS;
								segoverride = 1;
								break;

								/* repetition prefix check */
							case 0xF3:	/* REP/REPE/REPZ */
								reptype = 1;
								break;

							case 0xF2:	/* REPNE/REPNZ */
								reptype = 2;
								break;

							default:
								docontinue = 1;
								break;
						}
				}

			totalexec++;

			switch (opcode) {
					case 0x0:	/* 00 ADD Eb Gb */
						modregrm();
						oper1b = readrm8 (rm);
						oper2b = getreg8 (reg);
						op_add8();
						writerm8 (rm, res8);
						break;

					case 0x1:	/* 01 ADD Ev Gv */
						modregrm();
						oper1 = readrm16 (rm);
						oper2 = getreg16 (reg);
						op_add16();
						writerm16 (rm, res16);
						break;

					case 0x2:	/* 02 ADD Gb Eb */
						modregrm();
						oper1b = getreg8 (reg);
						oper2b = readrm8 (rm);
						op_add8();
						putreg8 (reg, res8);
						break;

					case 0x3:	/* 03 ADD Gv Ev */
						modregrm();
						oper1 = getreg16 (reg);
						oper2 = readrm16 (rm);
						op_add16();
						putreg16 (reg, res16);
						break;

					case 0x4:	/* 04 ADD X86_AL Ib */
						oper1b = X86_AL;
						oper2b = getmem8 (X86_CS, ip);
						StepIP (1);
						op_add8();
						X86_AL = res8;
						break;

					case 0x5:	/* 05 ADD eAX Iv */
						oper1 = X86_AX;
						oper2 = getmem16 (X86_CS, ip);
						StepIP (2);
						op_add16();
						X86_AX = res16;
						break;

					case 0x6:	/* 06 PUSH X86_ES */
						push (X86_ES);
						break;

					case 0x7:	/* 07 POP X86_ES */
						X86_ES = pop();
						break;

					case 0x8:	/* 08 OR Eb Gb */
						modregrm();
						oper1b = readrm8 (rm);
						oper2b = getreg8 (reg);
						op_or8();
						writerm8 (rm, res8);
						break;

					case 0x9:	/* 09 OR Ev Gv */
						modregrm();
						oper1 = readrm16 (rm);
						oper2 = getreg16 (reg);
						op_or16();
						writerm16 (rm, res16);
						break;

					case 0xA:	/* 0A OR Gb Eb */
						modregrm();
						oper1b = getreg8 (reg);
						oper2b = readrm8 (rm);
						op_or8();
						putreg8 (reg, res8);
						break;

					case 0xB:	/* 0B OR Gv Ev */
						modregrm();
						oper1 = getreg16 (reg);
						oper2 = readrm16 (rm);
						op_or16();
						if ( (oper1 == 0xF802) && (oper2 == 0xF802) ) {
								sf = 0;	/* cheap hack to make Wolf 3D think we're a 286 so it plays */
							}

						putreg16 (reg, res16);
						break;

					case 0xC:	/* 0C OR X86_AL Ib */
						oper1b = X86_AL;
						oper2b = getmem8 (X86_CS, ip);
						StepIP (1);
						op_or8();
						X86_AL = res8;
						break;

					case 0xD:	/* 0D OR eAX Iv */
						oper1 = X86_AX;
						oper2 = getmem16 (X86_CS, ip);
						StepIP (2);
						op_or16();
						X86_AX = res16;
						break;

					case 0xE:	/* 0E PUSH X86_CS */
						push (X86_CS);
						break;

#ifdef CPU_ALLOW_POP_CS //only the 8086/8088 does this.
					case 0xF: //0F POP CS
						X86_CS = pop();
						break;
#endif

					case 0x10:	/* 10 ADC Eb Gb */
						modregrm();
						oper1b = readrm8 (rm);
						oper2b = getreg8 (reg);
						op_adc8();
						writerm8 (rm, res8);
						break;

					case 0x11:	/* 11 ADC Ev Gv */
						modregrm();
						oper1 = readrm16 (rm);
						oper2 = getreg16 (reg);
						op_adc16();
						writerm16 (rm, res16);
						break;

					case 0x12:	/* 12 ADC Gb Eb */
						modregrm();
						oper1b = getreg8 (reg);
						oper2b = readrm8 (rm);
						op_adc8();
						putreg8 (reg, res8);
						break;

					case 0x13:	/* 13 ADC Gv Ev */
						modregrm();
						oper1 = getreg16 (reg);
						oper2 = readrm16 (rm);
						op_adc16();
						putreg16 (reg, res16);
						break;

					case 0x14:	/* 14 ADC X86_AL Ib */
						oper1b = X86_AL;
						oper2b = getmem8 (X86_CS, ip);
						StepIP (1);
						op_adc8();
						X86_AL = res8;
						break;

					case 0x15:	/* 15 ADC eAX Iv */
						oper1 = X86_AX;
						oper2 = getmem16 (X86_CS, ip);
						StepIP (2);
						op_adc16();
						X86_AX = res16;
						break;

					case 0x16:	/* 16 PUSH X86_SS */
						push (X86_SS);
						break;

					case 0x17:	/* 17 POP X86_SS */
						X86_SS = pop();
						break;

					case 0x18:	/* 18 SBB Eb Gb */
						modregrm();
						oper1b = readrm8 (rm);
						oper2b = getreg8 (reg);
						op_sbb8();
						writerm8 (rm, res8);
						break;

					case 0x19:	/* 19 SBB Ev Gv */
						modregrm();
						oper1 = readrm16 (rm);
						oper2 = getreg16 (reg);
						op_sbb16();
						writerm16 (rm, res16);
						break;

					case 0x1A:	/* 1A SBB Gb Eb */
						modregrm();
						oper1b = getreg8 (reg);
						oper2b = readrm8 (rm);
						op_sbb8();
						putreg8 (reg, res8);
						break;

					case 0x1B:	/* 1B SBB Gv Ev */
						modregrm();
						oper1 = getreg16 (reg);
						oper2 = readrm16 (rm);
						op_sbb16();
						putreg16 (reg, res16);
						break;

					case 0x1C:	/* 1C SBB X86_AL Ib */
						oper1b = X86_AL;
						oper2b = getmem8 (X86_CS, ip);
						StepIP (1);
						op_sbb8();
						X86_AL = res8;
						break;

					case 0x1D:	/* 1D SBB eAX Iv */
						oper1 = X86_AX;
						oper2 = getmem16 (X86_CS, ip);
						StepIP (2);
						op_sbb16();
						X86_AX = res16;
						break;

					case 0x1E:	/* 1E PUSH X86_DS */
						push (X86_DS);
						break;

					case 0x1F:	/* 1F POP X86_DS */
						X86_DS = pop();
						break;

					case 0x20:	/* 20 AND Eb Gb */
						modregrm();
						oper1b = readrm8 (rm);
						oper2b = getreg8 (reg);
						op_and8();
						writerm8 (rm, res8);
						break;

					case 0x21:	/* 21 AND Ev Gv */
						modregrm();
						oper1 = readrm16 (rm);
						oper2 = getreg16 (reg);
						op_and16();
						writerm16 (rm, res16);
						break;

					case 0x22:	/* 22 AND Gb Eb */
						modregrm();
						oper1b = getreg8 (reg);
						oper2b = readrm8 (rm);
						op_and8();
						putreg8 (reg, res8);
						break;

					case 0x23:	/* 23 AND Gv Ev */
						modregrm();
						oper1 = getreg16 (reg);
						oper2 = readrm16 (rm);
						op_and16();
						putreg16 (reg, res16);
						break;

					case 0x24:	/* 24 AND X86_AL Ib */
						oper1b = X86_AL;
						oper2b = getmem8 (X86_CS, ip);
						StepIP (1);
						op_and8();
						X86_AL = res8;
						break;

					case 0x25:	/* 25 AND eAX Iv */
						oper1 = X86_AX;
						oper2 = getmem16 (X86_CS, ip);
						StepIP (2);
						op_and16();
						X86_AX = res16;
						break;

					case 0x27:	/* 27 DAA */
						if ( ( (X86_AL & 0xF) > 9) || (af == 1) ) {
								oper1 = X86_AL + 6;
								X86_AL = oper1 & 255;
								if (oper1 & 0xFF00) {
										cf = 1;
									}
								else {
										cf = 0;
									}

								af = 1;
							}
						else {
								//af = 0;
							}

						if ( (X86_AL  > 0x9F) || (cf == 1) ) {
								X86_AL = X86_AL + 0x60;
								cf = 1;
							}
						else {
								//cf = 0;
							}

						X86_AL = X86_AL & 255;
						flag_szp8 (X86_AL);
						break;

					case 0x28:	/* 28 SUB Eb Gb */
						modregrm();
						oper1b = readrm8 (rm);
						oper2b = getreg8 (reg);
						op_sub8();
						writerm8 (rm, res8);
						break;

					case 0x29:	/* 29 SUB Ev Gv */
						modregrm();
						oper1 = readrm16 (rm);
						oper2 = getreg16 (reg);
						op_sub16();
						writerm16 (rm, res16);
						break;

					case 0x2A:	/* 2A SUB Gb Eb */
						modregrm();
						oper1b = getreg8 (reg);
						oper2b = readrm8 (rm);
						op_sub8();
						putreg8 (reg, res8);
						break;

					case 0x2B:	/* 2B SUB Gv Ev */
						modregrm();
						oper1 = getreg16 (reg);
						oper2 = readrm16 (rm);
						op_sub16();
						putreg16 (reg, res16);
						break;

					case 0x2C:	/* 2C SUB X86_AL Ib */
						oper1b = X86_AL;
						oper2b = getmem8 (X86_CS, ip);
						StepIP (1);
						op_sub8();
						X86_AL = res8;
						break;

					case 0x2D:	/* 2D SUB eAX Iv */
						oper1 = X86_AX;
						oper2 = getmem16 (X86_CS, ip);
						StepIP (2);
						op_sub16();
						X86_AX = res16;
						break;

					case 0x2F:	/* 2F DAS */
						if ( ( (X86_AL & 15) > 9) || (af == 1) ) {
								oper1 = X86_AL - 6;
								X86_AL = oper1 & 255;
								if (oper1 & 0xFF00) {
										cf = 1;
									}
								else {
										cf = 0;
									}

								af = 1;
							}
						else {
								af = 0;
							}

						if ( ( (X86_AL & 0xF0) > 0x90) || (cf == 1) ) {
								X86_AL = X86_AL - 0x60;
								cf = 1;
							}
						else {
								cf = 0;
							}

						flag_szp8 (X86_AL);
						break;

					case 0x30:	/* 30 XOR Eb Gb */
						modregrm();
						oper1b = readrm8 (rm);
						oper2b = getreg8 (reg);
						op_xor8();
						writerm8 (rm, res8);
						break;

					case 0x31:	/* 31 XOR Ev Gv */
						modregrm();
						oper1 = readrm16 (rm);
						oper2 = getreg16 (reg);
						op_xor16();
						writerm16 (rm, res16);
						break;

					case 0x32:	/* 32 XOR Gb Eb */
						modregrm();
						oper1b = getreg8 (reg);
						oper2b = readrm8 (rm);
						op_xor8();
						putreg8 (reg, res8);
						break;

					case 0x33:	/* 33 XOR Gv Ev */
						modregrm();
						oper1 = getreg16 (reg);
						oper2 = readrm16 (rm);
						op_xor16();
						putreg16 (reg, res16);
						break;

					case 0x34:	/* 34 XOR X86_AL Ib */
						oper1b = X86_AL;
						oper2b = getmem8 (X86_CS, ip);
						StepIP (1);
						op_xor8();
						X86_AL = res8;
						break;

					case 0x35:	/* 35 XOR eAX Iv */
						oper1 = X86_AX;
						oper2 = getmem16 (X86_CS, ip);
						StepIP (2);
						op_xor16();
						X86_AX = res16;
						break;

					case 0x37:	/* 37 AAA ASCII */
						if ( ( (X86_AL & 0xF) > 9) || (af == 1) ) {
								X86_AL = X86_AL + 6;
								X86_AH = X86_AH + 1;
								af = 1;
								cf = 1;
							}
						else {
								af = 0;
								cf = 0;
							}

						X86_AL = X86_AL & 0xF;
						break;

					case 0x38:	/* 38 CMP Eb Gb */
						modregrm();
						oper1b = readrm8 (rm);
						oper2b = getreg8 (reg);
						flag_sub8 (oper1b, oper2b);
						break;

					case 0x39:	/* 39 CMP Ev Gv */
						modregrm();
						oper1 = readrm16 (rm);
						oper2 = getreg16 (reg);
						flag_sub16 (oper1, oper2);
						break;

					case 0x3A:	/* 3A CMP Gb Eb */
						modregrm();
						oper1b = getreg8 (reg);
						oper2b = readrm8 (rm);
						flag_sub8 (oper1b, oper2b);
						break;

					case 0x3B:	/* 3B CMP Gv Ev */
						modregrm();
						oper1 = getreg16 (reg);
						oper2 = readrm16 (rm);
						flag_sub16 (oper1, oper2);
						break;

					case 0x3C:	/* 3C CMP X86_AL Ib */
						oper1b = X86_AL;
						oper2b = getmem8 (X86_CS, ip);
						StepIP (1);
						flag_sub8 (oper1b, oper2b);
						break;

					case 0x3D:	/* 3D CMP eAX Iv */
						oper1 = X86_AX;
						oper2 = getmem16 (X86_CS, ip);
						StepIP (2);
						flag_sub16 (oper1, oper2);
						break;

					case 0x3F:	/* 3F AAS ASCII */
						if ( ( (X86_AL & 0xF) > 9) || (af == 1) ) {
								X86_AL = X86_AL - 6;
								X86_AH = X86_AH - 1;
								af = 1;
								cf = 1;
							}
						else {
								af = 0;
								cf = 0;
							}

						X86_AL = X86_AL & 0xF;
						break;

					case 0x40:	/* 40 INC eAX */
						oldcf = cf;
						oper1 = X86_AX;
						oper2 = 1;
						op_add16();
						cf = oldcf;
						X86_AX = res16;
						break;

					case 0x41:	/* 41 INC eCX */
						oldcf = cf;
						oper1 = X86_CX;
						oper2 = 1;
						op_add16();
						cf = oldcf;
						X86_CX = res16;
						break;

					case 0x42:	/* 42 INC eDX */
						oldcf = cf;
						oper1 = X86_DX;
						oper2 = 1;
						op_add16();
						cf = oldcf;
						X86_DX = res16;
						break;

					case 0x43:	/* 43 INC eBX */
						oldcf = cf;
						oper1 = X86_BX;
						oper2 = 1;
						op_add16();
						cf = oldcf;
						X86_BX = res16;
						break;

					case 0x44:	/* 44 INC eSP */
						oldcf = cf;
						oper1 = X86_SP;
						oper2 = 1;
						op_add16();
						cf = oldcf;
						X86_SP = res16;
						break;

					case 0x45:	/* 45 INC eBP */
						oldcf = cf;
						oper1 = X86_BP;
						oper2 = 1;
						op_add16();
						cf = oldcf;
						X86_BP = res16;
						break;

					case 0x46:	/* 46 INC eSI */
						oldcf = cf;
						oper1 = X86_SI;
						oper2 = 1;
						op_add16();
						cf = oldcf;
						X86_SI = res16;
						break;

					case 0x47:	/* 47 INC eDI */
						oldcf = cf;
						oper1 = X86_DI;
						oper2 = 1;
						op_add16();
						cf = oldcf;
						X86_DI = res16;
						break;

					case 0x48:	/* 48 DEC eAX */
						oldcf = cf;
						oper1 = X86_AX;
						oper2 = 1;
						op_sub16();
						cf = oldcf;
						X86_AX = res16;
						break;

					case 0x49:	/* 49 DEC eCX */
						oldcf = cf;
						oper1 = X86_CX;
						oper2 = 1;
						op_sub16();
						cf = oldcf;
						X86_CX = res16;
						break;

					case 0x4A:	/* 4A DEC eDX */
						oldcf = cf;
						oper1 = X86_DX;
						oper2 = 1;
						op_sub16();
						cf = oldcf;
						X86_DX = res16;
						break;

					case 0x4B:	/* 4B DEC eBX */
						oldcf = cf;
						oper1 = X86_BX;
						oper2 = 1;
						op_sub16();
						cf = oldcf;
						X86_BX = res16;
						break;

					case 0x4C:	/* 4C DEC eSP */
						oldcf = cf;
						oper1 = X86_SP;
						oper2 = 1;
						op_sub16();
						cf = oldcf;
						X86_SP = res16;
						break;

					case 0x4D:	/* 4D DEC eBP */
						oldcf = cf;
						oper1 = X86_BP;
						oper2 = 1;
						op_sub16();
						cf = oldcf;
						X86_BP = res16;
						break;

					case 0x4E:	/* 4E DEC eSI */
						oldcf = cf;
						oper1 = X86_SI;
						oper2 = 1;
						op_sub16();
						cf = oldcf;
						X86_SI = res16;
						break;

					case 0x4F:	/* 4F DEC eDI */
						oldcf = cf;
						oper1 = X86_DI;
						oper2 = 1;
						op_sub16();
						cf = oldcf;
						X86_DI = res16;
						break;

					case 0x50:	/* 50 PUSH eAX */
						push (X86_AX);
						break;

					case 0x51:	/* 51 PUSH eCX */
						push (X86_CX);
						break;

					case 0x52:	/* 52 PUSH eDX */
						push (X86_DX);
						break;

					case 0x53:	/* 53 PUSH eBX */
						push (X86_BX);
						break;

					case 0x54:	/* 54 PUSH eSP */
#ifdef USE_286_STYLE_PUSH_SP
						push (X86_SP);
#else
						push (X86_SP - 2);
#endif
						break;

					case 0x55:	/* 55 PUSH eBP */
						push (X86_BP);
						break;

					case 0x56:	/* 56 PUSH eSI */
						push (X86_SI);
						break;

					case 0x57:	/* 57 PUSH eDI */
						push (X86_DI);
						break;

					case 0x58:	/* 58 POP eAX */
						X86_AX = pop();
						break;

					case 0x59:	/* 59 POP eCX */
						X86_CX = pop();
						break;

					case 0x5A:	/* 5A POP eDX */
						X86_DX = pop();
						break;

					case 0x5B:	/* 5B POP eBX */
						X86_BX = pop();
						break;

					case 0x5C:	/* 5C POP eSP */
						X86_SP = pop();
						break;

					case 0x5D:	/* 5D POP eBP */
						X86_BP = pop();
						break;

					case 0x5E:	/* 5E POP eSI */
						X86_SI = pop();
						break;

					case 0x5F:	/* 5F POP eDI */
						X86_DI = pop();
						break;

#ifndef CPU_8086
					case 0x60:	/* 60 PUSHA (80186+) */
						oldsp = X86_SP;
						push (X86_AX);
						push (X86_CX);
						push (X86_DX);
						push (X86_BX);
						push (oldsp);
						push (X86_BP);
						push (X86_SI);
						push (X86_DI);
						break;

					case 0x61:	/* 61 POPA (80186+) */
						X86_DI = pop();
						X86_SI = pop();
						X86_BP = pop();
						dummy = pop();
						X86_BX = pop();
						X86_DX = pop();
						X86_CX = pop();
						X86_AX = pop();
						break;

					case 0x62: /* 62 BOUND Gv, Ev (80186+) */
						modregrm();
						getea (rm);
						if (signext32 (getreg16 (reg) ) < signext32 ( getmem16 (ea >> 4, ea & 15) ) ) {
								intcall86 (5); //bounds check exception
							}
						else {
								ea += 2;
								if (signext32 (getreg16 (reg) ) > signext32 ( getmem16 (ea >> 4, ea & 15) ) ) {
										intcall86(5); //bounds check exception
									}
							}
						break;

					case 0x68:	/* 68 PUSH Iv (80186+) */
						push (getmem16 (X86_CS, ip) );
						StepIP (2);
						break;

					case 0x69:	/* 69 IMUL Gv Ev Iv (80186+) */
						modregrm();
						temp1 = readrm16 (rm);
						temp2 = getmem16 (X86_CS, ip);
						StepIP (2);
						if ( (temp1 & 0x8000L) == 0x8000L) {
								temp1 = temp1 | 0xFFFF0000L;
							}

						if ( (temp2 & 0x8000L) == 0x8000L) {
								temp2 = temp2 | 0xFFFF0000L;
							}

						temp3 = temp1 * temp2;
						putreg16 (reg, temp3 & 0xFFFFL);
						if (temp3 & 0xFFFF0000L) {
								cf = 1;
								of = 1;
							}
						else {
								cf = 0;
								of = 0;
							}
						break;

					case 0x6A:	/* 6A PUSH Ib (80186+) */
						push (getmem8 (X86_CS, ip) );
						StepIP (1);
						break;

					case 0x6B:	/* 6B IMUL Gv Eb Ib (80186+) */
						modregrm();
						temp1 = readrm16 (rm);
						temp2 = signext (getmem8 (X86_CS, ip) );
						StepIP (1);
						if ( (temp1 & 0x8000L) == 0x8000L) {
								temp1 = temp1 | 0xFFFF0000L;
							}

						if ( (temp2 & 0x8000L) == 0x8000L) {
								temp2 = temp2 | 0xFFFF0000L;
							}

						temp3 = temp1 * temp2;
						putreg16 (reg, temp3 & 0xFFFFL);
						if (temp3 & 0xFFFF0000L) {
								cf = 1;
								of = 1;
							}
						else {
								cf = 0;
								of = 0;
							}
						break;

					case 0x6C:	/* 6E INSB */
						if (reptype && (X86_CX == 0) ) {
								break;
							}

						putmem8 (useseg, X86_SI, portin (X86_DX) );
						if (df) {
								X86_SI = X86_SI - 1;
								X86_DI = X86_DI - 1;
							}
						else {
								X86_SI = X86_SI + 1;
								X86_DI = X86_DI + 1;
							}

						if (reptype) {
								X86_CX = X86_CX - 1;
							}

						totalexec++;
						loopcount++;
						if (!reptype) {
								break;
							}

						ip = firstip;
						break;

					case 0x6D:	/* 6F INSW */
						if (reptype && (X86_CX == 0) ) {
								break;
							}

						putmem16 (useseg, X86_SI, portin16 (X86_DX) );
						if (df) {
								X86_SI = X86_SI - 2;
								X86_DI = X86_DI - 2;
							}
						else {
								X86_SI = X86_SI + 2;
								X86_DI = X86_DI + 2;
							}

						if (reptype) {
								X86_CX = X86_CX - 1;
							}

						totalexec++;
						loopcount++;
						if (!reptype) {
								break;
							}

						ip = firstip;
						break;

					case 0x6E:	/* 6E OUTSB */
						if (reptype && (X86_CX == 0) ) {
								break;
							}

						portout (X86_DX, getmem8 (useseg, X86_SI) );
						if (df) {
								X86_SI = X86_SI - 1;
								X86_DI = X86_DI - 1;
							}
						else {
								X86_SI = X86_SI + 1;
								X86_DI = X86_DI + 1;
							}

						if (reptype) {
								X86_CX = X86_CX - 1;
							}

						totalexec++;
						loopcount++;
						if (!reptype) {
								break;
							}

						ip = firstip;
						break;

					case 0x6F:	/* 6F OUTSW */
						if (reptype && (X86_CX == 0) ) {
								break;
							}

						portout16 (X86_DX, getmem16 (useseg, X86_SI) );
						if (df) {
								X86_SI = X86_SI - 2;
								X86_DI = X86_DI - 2;
							}
						else {
								X86_SI = X86_SI + 2;
								X86_DI = X86_DI + 2;
							}

						if (reptype) {
								X86_CX = X86_CX - 1;
							}

						totalexec++;
						loopcount++;
						if (!reptype) {
								break;
							}

						ip = firstip;
						break;
#endif

					case 0x70:	/* 70 JO Jb */
						temp16 = signext (getmem8 (X86_CS, ip) );
						StepIP (1);
						if (of) {
								ip = ip + temp16;
							}
						break;

					case 0x71:	/* 71 JNO Jb */
						temp16 = signext (getmem8 (X86_CS, ip) );
						StepIP (1);
						if (!of) {
								ip = ip + temp16;
							}
						break;

					case 0x72:	/* 72 JB Jb */
						temp16 = signext (getmem8 (X86_CS, ip) );
						StepIP (1);
						if (cf) {
								ip = ip + temp16;
							}
						break;

					case 0x73:	/* 73 JNB Jb */
						temp16 = signext (getmem8 (X86_CS, ip) );
						StepIP (1);
						if (!cf) {
								ip = ip + temp16;
							}
						break;

					case 0x74:	/* 74 JZ Jb */
						temp16 = signext (getmem8 (X86_CS, ip) );
						StepIP (1);
						if (zf) {
								ip = ip + temp16;
							}
						break;

					case 0x75:	/* 75 JNZ Jb */
						temp16 = signext (getmem8 (X86_CS, ip) );
						StepIP (1);
						if (!zf) {
								ip = ip + temp16;
							}
						break;

					case 0x76:	/* 76 JBE Jb */
						temp16 = signext (getmem8 (X86_CS, ip) );
						StepIP (1);
						if (cf || zf) {
								ip = ip + temp16;
							}
						break;

					case 0x77:	/* 77 JA Jb */
						temp16 = signext (getmem8 (X86_CS, ip) );
						StepIP (1);
						if (!cf && !zf) {
								ip = ip + temp16;
							}
						break;

					case 0x78:	/* 78 JS Jb */
						temp16 = signext (getmem8 (X86_CS, ip) );
						StepIP (1);
						if (sf) {
								ip = ip + temp16;
							}
						break;

					case 0x79:	/* 79 JNS Jb */
						temp16 = signext (getmem8 (X86_CS, ip) );
						StepIP (1);
						if (!sf) {
								ip = ip + temp16;
							}
						break;

					case 0x7A:	/* 7A JPE Jb */
						temp16 = signext (getmem8 (X86_CS, ip) );
						StepIP (1);
						if (pf) {
								ip = ip + temp16;
							}
						break;

					case 0x7B:	/* 7B JPO Jb */
						temp16 = signext (getmem8 (X86_CS, ip) );
						StepIP (1);
						if (!pf) {
								ip = ip + temp16;
							}
						break;

					case 0x7C:	/* 7C JL Jb */
						temp16 = signext (getmem8 (X86_CS, ip) );
						StepIP (1);
						if (sf != of) {
								ip = ip + temp16;
							}
						break;

					case 0x7D:	/* 7D JGE Jb */
						temp16 = signext (getmem8 (X86_CS, ip) );
						StepIP (1);
						if (sf == of) {
								ip = ip + temp16;
							}
						break;

					case 0x7E:	/* 7E JLE Jb */
						temp16 = signext (getmem8 (X86_CS, ip) );
						StepIP (1);
						if ( (sf != of) || zf) {
								ip = ip + temp16;
							}
						break;

					case 0x7F:	/* 7F JG Jb */
						temp16 = signext (getmem8 (X86_CS, ip) );
						StepIP (1);
						if (!zf && (sf == of) ) {
								ip = ip + temp16;
							}
						break;

					case 0x80:
					case 0x82:	/* 80/82 GRP1 Eb Ib */
						modregrm();
						oper1b = readrm8 (rm);
						oper2b = getmem8 (X86_CS, ip);
						StepIP (1);
						switch (reg) {
								case 0:
									op_add8();
									break;
								case 1:
									op_or8();
									break;
								case 2:
									op_adc8();
									break;
								case 3:
									op_sbb8();
									break;
								case 4:
									op_and8();
									break;
								case 5:
									op_sub8();
									break;
								case 6:
									op_xor8();
									break;
								case 7:
									flag_sub8 (oper1b, oper2b);
									break;
								default:
									break;	/* to avoid compiler warnings */
							}

						if (reg < 7) {
								writerm8 (rm, res8);
							}
						break;

					case 0x81:	/* 81 GRP1 Ev Iv */
					case 0x83:	/* 83 GRP1 Ev Ib */
						modregrm();
						oper1 = readrm16 (rm);
						if (opcode == 0x81) {
								oper2 = getmem16 (X86_CS, ip);
								StepIP (2);
							}
						else {
								oper2 = signext (getmem8 (X86_CS, ip) );
								StepIP (1);
							}

						switch (reg) {
								case 0:
									op_add16();
									break;
								case 1:
									op_or16();
									break;
								case 2:
									op_adc16();
									break;
								case 3:
									op_sbb16();
									break;
								case 4:
									op_and16();
									break;
								case 5:
									op_sub16();
									break;
								case 6:
									op_xor16();
									break;
								case 7:
									flag_sub16 (oper1, oper2);
									break;
								default:
									break;	/* to avoid compiler warnings */
							}

						if (reg < 7) {
								writerm16 (rm, res16);
							}
						break;

					case 0x84:	/* 84 TEST Gb Eb */
						modregrm();
						oper1b = getreg8 (reg);
						oper2b = readrm8 (rm);
						flag_log8 (oper1b & oper2b);
						break;

					case 0x85:	/* 85 TEST Gv Ev */
						modregrm();
						oper1 = getreg16 (reg);
						oper2 = readrm16 (rm);
						flag_log16 (oper1 & oper2);
						break;

					case 0x86:	/* 86 XCHG Gb Eb */
						modregrm();
						oper1b = getreg8 (reg);
						putreg8 (reg, readrm8 (rm) );
						writerm8 (rm, oper1b);
						break;

					case 0x87:	/* 87 XCHG Gv Ev */
						modregrm();
						oper1 = getreg16 (reg);
						putreg16 (reg, readrm16 (rm) );
						writerm16 (rm, oper1);
						break;

					case 0x88:	/* 88 MOV Eb Gb */
						modregrm();
						writerm8 (rm, getreg8 (reg) );
						break;

					case 0x89:	/* 89 MOV Ev Gv */
						modregrm();
						writerm16 (rm, getreg16 (reg) );
						break;

					case 0x8A:	/* 8A MOV Gb Eb */
						modregrm();
						putreg8 (reg, readrm8 (rm) );
						break;

					case 0x8B:	/* 8B MOV Gv Ev */
						modregrm();
						putreg16 (reg, readrm16 (rm) );
						break;

					case 0x8C:	/* 8C MOV Ew Sw */
						modregrm();
						writerm16 (rm, getsegreg (reg) );
						break;

					case 0x8D:	/* 8D LEA Gv M */
						modregrm();
						getea (rm);
						putreg16 (reg, ea - segbase (useseg) );
						break;

					case 0x8E:	/* 8E MOV Sw Ew */
						modregrm();
						putsegreg (reg, readrm16 (rm) );
						break;

					case 0x8F:	/* 8F POP Ev */
						modregrm();
						writerm16 (rm, pop() );
						break;

					case 0x90:	/* 90 NOP */
						break;

					case 0x91:	/* 91 XCHG eCX eAX */
						oper1 = X86_CX;
						X86_CX = X86_AX;
						X86_AX = oper1;
						break;

					case 0x92:	/* 92 XCHG eDX eAX */
						oper1 = X86_DX;
						X86_DX = X86_AX;
						X86_AX = oper1;
						break;

					case 0x93:	/* 93 XCHG eBX eAX */
						oper1 = X86_BX;
						X86_BX = X86_AX;
						X86_AX = oper1;
						break;

					case 0x94:	/* 94 XCHG eSP eAX */
						oper1 = X86_SP;
						X86_SP = X86_AX;
						X86_AX = oper1;
						break;

					case 0x95:	/* 95 XCHG eBP eAX */
						oper1 = X86_BP;
						X86_BP = X86_AX;
						X86_AX = oper1;
						break;

					case 0x96:	/* 96 XCHG eSI eAX */
						oper1 = X86_SI;
						X86_SI = X86_AX;
						X86_AX = oper1;
						break;

					case 0x97:	/* 97 XCHG eDI eAX */
						oper1 = X86_DI;
						X86_DI = X86_AX;
						X86_AX = oper1;
						break;

					case 0x98:	/* 98 CBW */
						if ( (X86_AL & 0x80) == 0x80) {
								X86_AH = 0xFF;
							}
						else {
								X86_AH = 0;
							}
						break;

					case 0x99:	/* 99 CWD */
						if ( (X86_AH & 0x80) == 0x80) {
								X86_DX = 0xFFFF;
							}
						else {
								X86_DX = 0;
							}
						break;

					case 0x9A:	/* 9A CALL Ap */
						oper1 = getmem16 (X86_CS, ip);
						StepIP (2);
						oper2 = getmem16 (X86_CS, ip);
						StepIP (2);
						push (X86_CS);
						push (ip);
						ip = oper1;
						X86_CS = oper2;
						break;

					case 0x9B:	/* 9B WAIT */
						break;

					case 0x9C:	/* 9C PUSHF */
#ifdef CPU_SET_HIGH_FLAGS
						push (makeflagsword() | 0xF800);
#else
						push (makeflagsword() | 0x0800);
#endif
						break;

					case 0x9D:	/* 9D POPF */
						temp16 = pop();
						decodeflagsword (temp16);
						break;

					case 0x9E:	/* 9E SAHF */
						decodeflagsword ( (makeflagsword() & 0xFF00) | X86_AH);
						break;

					case 0x9F:	/* 9F LAHF */
						X86_AH = makeflagsword() & 0xFF;
						break;

					case 0xA0:	/* A0 MOV X86_AL Ob */
						X86_AL = getmem8 (useseg, getmem16 (X86_CS, ip) );
						StepIP (2);
						break;

					case 0xA1:	/* A1 MOV eAX Ov */
						oper1 = getmem16 (useseg, getmem16 (X86_CS, ip) );
						StepIP (2);
						X86_AX = oper1;
						break;

					case 0xA2:	/* A2 MOV Ob X86_AL */
						putmem8 (useseg, getmem16 (X86_CS, ip), X86_AL);
						StepIP (2);
						break;

					case 0xA3:	/* A3 MOV Ov eAX */
						putmem16 (useseg, getmem16 (X86_CS, ip), X86_AX);
						StepIP (2);
						break;

					case 0xA4:	/* A4 MOVSB */
						if (reptype && (X86_CX == 0) ) {
								break;
							}

						putmem8 (X86_ES, X86_DI, getmem8 (useseg, X86_SI) );
						if (df) {
								X86_SI = X86_SI - 1;
								X86_DI = X86_DI - 1;
							}
						else {
								X86_SI = X86_SI + 1;
								X86_DI = X86_DI + 1;
							}

						if (reptype) {
								X86_CX = X86_CX - 1;
							}

						totalexec++;
						loopcount++;
						if (!reptype) {
								break;
							}

						ip = firstip;
						break;

					case 0xA5:	/* A5 MOVSW */
						if (reptype && (X86_CX == 0) ) {
								break;
							}

						putmem16 (X86_ES, X86_DI, getmem16 (useseg, X86_SI) );
						if (df) {
								X86_SI = X86_SI - 2;
								X86_DI = X86_DI - 2;
							}
						else {
								X86_SI = X86_SI + 2;
								X86_DI = X86_DI + 2;
							}

						if (reptype) {
								X86_CX = X86_CX - 1;
							}

						totalexec++;
						loopcount++;
						if (!reptype) {
								break;
							}

						ip = firstip;
						break;

					case 0xA6:	/* A6 CMPSB */
						if (reptype && (X86_CX == 0) ) {
								break;
							}

						oper1b = getmem8 (useseg, X86_SI);
						oper2b = getmem8 (X86_ES, X86_DI);
						if (df) {
								X86_SI = X86_SI - 1;
								X86_DI = X86_DI - 1;
							}
						else {
								X86_SI = X86_SI + 1;
								X86_DI = X86_DI + 1;
							}

						flag_sub8 (oper1b, oper2b);
						if (reptype) {
								X86_CX = X86_CX - 1;
							}

						if ( (reptype == 1) && !zf) {
								break;
							}
						else if ( (reptype == 2) && (zf == 1) ) {
								break;
							}

						totalexec++;
						loopcount++;
						if (!reptype) {
								break;
							}

						ip = firstip;
						break;

					case 0xA7:	/* A7 CMPSW */
						if (reptype && (X86_CX == 0) ) {
								break;
							}

						oper1 = getmem16 (useseg,X86_SI);
						oper2 = getmem16 (X86_ES, X86_DI);
						if (df) {
								X86_SI = X86_SI - 2;
								X86_DI = X86_DI - 2;
							}
						else {
								X86_SI = X86_SI + 2;
								X86_DI = X86_DI + 2;
							}

						flag_sub16 (oper1, oper2);
						if (reptype) {
								X86_CX = X86_CX - 1;
							}

						if ( (reptype == 1) && !zf) {
								break;
							}

						if ( (reptype == 2) && (zf == 1) ) {
								break;
							}

						totalexec++;
						loopcount++;
						if (!reptype) {
								break;
							}

						ip = firstip;
						break;

					case 0xA8:	/* A8 TEST X86_AL Ib */
						oper1b = X86_AL;
						oper2b = getmem8 (X86_CS, ip);
						StepIP (1);
						flag_log8 (oper1b & oper2b);
						break;

					case 0xA9:	/* A9 TEST eAX Iv */
						oper1 = X86_AX;
						oper2 = getmem16 (X86_CS, ip);
						StepIP (2);
						flag_log16 (oper1 & oper2);
						break;

					case 0xAA:	/* AA STOSB */
						if (reptype && (X86_CX == 0) ) {
								break;
							}

						putmem8 (X86_ES, X86_DI, X86_AL);
						if (df) {
								X86_DI = X86_DI - 1;
							}
						else {
								X86_DI = X86_DI + 1;
							}

						if (reptype) {
								X86_CX = X86_CX - 1;
							}

						totalexec++;
						loopcount++;
						if (!reptype) {
								break;
							}

						ip = firstip;
						break;

					case 0xAB:	/* AB STOSW */
						if (reptype && (X86_CX == 0) ) {
								break;
							}

						putmem16 (X86_ES, X86_DI, X86_AX);
						if (df) {
								X86_DI = X86_DI - 2;
							}
						else {
								X86_DI = X86_DI + 2;
							}

						if (reptype) {
								X86_CX = X86_CX - 1;
							}

						totalexec++;
						loopcount++;
						if (!reptype) {
								break;
							}

						ip = firstip;
						break;

					case 0xAC:	/* AC LODSB */
						if (reptype && (X86_CX == 0) ) {
								break;
							}

						X86_AL = getmem8 (useseg, X86_SI);
						if (df) {
								X86_SI = X86_SI - 1;
							}
						else {
								X86_SI = X86_SI + 1;
							}

						if (reptype) {
								X86_CX = X86_CX - 1;
							}

						totalexec++;
						loopcount++;
						if (!reptype) {
								break;
							}

						ip = firstip;
						break;

					case 0xAD:	/* AD LODSW */
						if (reptype && (X86_CX == 0) ) {
								break;
							}

						oper1 = getmem16 (useseg, X86_SI);
						X86_AX = oper1;
						if (df) {
								X86_SI = X86_SI - 2;
							}
						else {
								X86_SI = X86_SI + 2;
							}

						if (reptype) {
								X86_CX = X86_CX - 1;
							}

						totalexec++;
						loopcount++;
						if (!reptype) {
								break;
							}

						ip = firstip;
						break;

					case 0xAE:	/* AE SCASB */
						if (reptype && (X86_CX == 0) ) {
								break;
							}

						oper1b = X86_AL;
						oper2b = getmem8 (X86_ES, X86_DI);
						flag_sub8 (oper1b, oper2b);
						if (df) {
								X86_DI = X86_DI - 1;
							}
						else {
								X86_DI = X86_DI + 1;
							}

						if (reptype) {
								X86_CX = X86_CX - 1;
							}

						if ( (reptype == 1) && !zf) {
								break;
							}
						else if ( (reptype == 2) && (zf == 1) ) {
								break;
							}

						totalexec++;
						loopcount++;
						if (!reptype) {
								break;
							}

						ip = firstip;
						break;

					case 0xAF:	/* AF SCASW */
						if (reptype && (X86_CX == 0) ) {
								break;
							}

						oper1 = X86_AX;
						oper2 = getmem16 (X86_ES, X86_DI);
						flag_sub16 (oper1, oper2);
						if (df) {
								X86_DI = X86_DI - 2;
							}
						else {
								X86_DI = X86_DI + 2;
							}

						if (reptype) {
								X86_CX = X86_CX - 1;
							}

						if ( (reptype == 1) && !zf) {
								break;
							}
						else if ( (reptype == 2) & (zf == 1) ) {
								break;
							}

						totalexec++;
						loopcount++;
						if (!reptype) {
								break;
							}

						ip = firstip;
						break;

					case 0xB0:	/* B0 MOV X86_AL Ib */
						X86_AL = getmem8 (X86_CS, ip);
						StepIP (1);
						break;

					case 0xB1:	/* B1 MOV X86_CL Ib */
						X86_CL = getmem8 (X86_CS, ip);
						StepIP (1);
						break;

					case 0xB2:	/* B2 MOV X86_DL Ib */
						X86_DL = getmem8 (X86_CS, ip);
						StepIP (1);
						break;

					case 0xB3:	/* B3 MOV X86_BL Ib */
						X86_BL = getmem8 (X86_CS, ip);
						StepIP (1);
						break;

					case 0xB4:	/* B4 MOV X86_AH Ib */
						X86_AH = getmem8 (X86_CS, ip);
						StepIP (1);
						break;

					case 0xB5:	/* B5 MOV X86_CH Ib */
						X86_CH = getmem8 (X86_CS, ip);
						StepIP (1);
						break;

					case 0xB6:	/* B6 MOV X86_DH Ib */
						X86_DH = getmem8 (X86_CS, ip);
						StepIP (1);
						break;

					case 0xB7:	/* B7 MOV X86_BH Ib */
						X86_BH = getmem8 (X86_CS, ip);
						StepIP (1);
						break;

					case 0xB8:	/* B8 MOV eAX Iv */
						oper1 = getmem16 (X86_CS, ip);
						StepIP (2);
						X86_AX = oper1;
						break;

					case 0xB9:	/* B9 MOV eCX Iv */
						oper1 = getmem16 (X86_CS, ip);
						StepIP (2);
						X86_CX = oper1;
						break;

					case 0xBA:	/* BA MOV eDX Iv */
						oper1 = getmem16 (X86_CS, ip);
						StepIP (2);
						X86_DX = oper1;
						break;

					case 0xBB:	/* BB MOV eBX Iv */
						oper1 = getmem16 (X86_CS, ip);
						StepIP (2);
						X86_BX = oper1;
						break;

					case 0xBC:	/* BC MOV eSP Iv */
						X86_SP = getmem16 (X86_CS, ip);
						StepIP (2);
						break;

					case 0xBD:	/* BD MOV eBP Iv */
						X86_BP = getmem16 (X86_CS, ip);
						StepIP (2);
						break;

					case 0xBE:	/* BE MOV eSI Iv */
						X86_SI = getmem16 (X86_CS, ip);
						StepIP (2);
						break;

					case 0xBF:	/* BF MOV eDI Iv */
						X86_DI = getmem16 (X86_CS, ip);
						StepIP (2);
						break;

					case 0xC0:	/* C0 GRP2 byte imm8 (80186+) */
						modregrm();
						oper1b = readrm8 (rm);
						oper2b = getmem8 (X86_CS, ip);
						StepIP (1);
						writerm8 (rm, op_grp2_8 (oper2b) );
						break;

					case 0xC1:	/* C1 GRP2 word imm8 (80186+) */
						modregrm();
						oper1 = readrm16 (rm);
						oper2 = getmem8 (X86_CS, ip);
						StepIP (1);
						writerm16 (rm, op_grp2_16 ( (uint8_t) oper2) );
						break;

					case 0xC2:	/* C2 RET Iw */
						oper1 = getmem16 (X86_CS, ip);
						ip = pop();
						X86_SP = X86_SP + oper1;
						break;

					case 0xC3:	/* C3 RET */
						ip = pop();
						break;

					case 0xC4:	/* C4 LES Gv Mp */
						modregrm();
						getea (rm);
						putreg16 (reg, read86 (ea) + read86 (ea + 1) * 256);
						X86_ES = read86 (ea + 2) + read86 (ea + 3) * 256;
						break;

					case 0xC5:	/* C5 LDS Gv Mp */
						modregrm();
						getea (rm);
						putreg16 (reg, read86 (ea) + read86 (ea + 1) * 256);
						X86_DS = read86 (ea + 2) + read86 (ea + 3) * 256;
						break;

					case 0xC6:	/* C6 MOV Eb Ib */
						modregrm();
						writerm8 (rm, getmem8 (X86_CS, ip) );
						StepIP (1);
						break;

					case 0xC7:	/* C7 MOV Ev Iv */
						modregrm();
						writerm16 (rm, getmem16 (X86_CS, ip) );
						StepIP (2);
						break;

					case 0xC8:	/* C8 ENTER (80186+) */
						{
						const uint16_t stacksize = getmem16 (X86_CS, ip);
						StepIP(2);
						const uint8_t nestlev = getmem8(X86_CS, ip);
						StepIP(1);
						push(X86_BP);
						const uint16_t frametemp = X86_SP;
						if (nestlev) {
							for (uint16_t temp16 = 1; temp16 < nestlev; temp16++) {
								X86_BP = X86_BP - 2;
								push(X86_BP);
							}
							push(X86_SP);
						}
						X86_BP = frametemp;
						X86_SP = X86_BP - stacksize;
						}
						break;

					case 0xC9:	/* C9 LEAVE (80186+) */
						X86_SP = X86_BP;
						X86_BP = pop();
						break;

					case 0xCA:	/* CA RETF Iw */
						oper1 = getmem16 (X86_CS, ip);
						ip = pop();
						X86_CS = pop();
						X86_SP = X86_SP + oper1;
						break;

					case 0xCB:	/* CB RETF */
						ip = pop();;
						X86_CS = pop();
						break;

					case 0xCC:	/* CC INT 3 */
						intcall86 (3);
						break;

					case 0xCD:	/* CD INT Ib */
						oper1b = getmem8 (X86_CS, ip);
						StepIP (1);
						intcall86 (oper1b);
						break;

					case 0xCE:	/* CE INTO */
						if (into_opcode()) {
							if (of)
								intcall86(4);
						}
						break;

					case 0xCF:	/* CF IRET */
						iret86();
						break;

					case 0xD0:	/* D0 GRP2 Eb 1 */
						modregrm();
						oper1b = readrm8 (rm);
						writerm8 (rm, op_grp2_8 (1) );
						break;

					case 0xD1:	/* D1 GRP2 Ev 1 */
						modregrm();
						oper1 = readrm16 (rm);
						writerm16 (rm, op_grp2_16 (1) );
						break;

					case 0xD2:	/* D2 GRP2 Eb X86_CL */
						modregrm();
						oper1b = readrm8 (rm);
						writerm8 (rm, op_grp2_8 (X86_CL) );
						break;

					case 0xD3:	/* D3 GRP2 Ev X86_CL */
						modregrm();
						oper1 = readrm16 (rm);
						writerm16 (rm, op_grp2_16 (X86_CL) );
						break;

					case 0xD4:	/* D4 AAM I0 */
						oper1 = getmem8 (X86_CS, ip);
						StepIP (1);
						if (!oper1) {
								intcall86 (0);
								break;
							}	/* division by zero */

						X86_AH = (X86_AL / oper1) & 255;
						X86_AL = (X86_AL % oper1) & 255;
						flag_szp16 (X86_AX);
						break;

					case 0xD5:	/* D5 AAD I0 */
						oper1 = getmem8 (X86_CS, ip);
						StepIP (1);
						X86_AL = (X86_AH * oper1 + X86_AL) & 255;
						X86_AH = 0;
						flag_szp16 (X86_AH * oper1 + X86_AL);
						sf = 0;
						break;

					case 0xD6:	/* D6 XLAT on V20/V30, SALC on 8086/8088 */
#ifndef CPU_NO_SALC
						X86_AL = cf ? 0xFF : 0x00;
						break;
#endif

					case 0xD7:	/* D7 XLAT */
						X86_AL = read86(useseg * 16 + (X86_BX) + X86_AL);
						break;

					case 0xD8:
					case 0xD9:
					case 0xDA:
					case 0xDB:
					case 0xDC:
					case 0xDE:
					case 0xDD:
					case 0xDF:	/* escape to x87 FPU (unsupported) */
						modregrm();
						break;

					case 0xE0:	/* E0 LOOPNZ Jb */
						temp16 = signext (getmem8 (X86_CS, ip) );
						StepIP (1);
						X86_CX = X86_CX - 1;
						if ( (X86_CX) && !zf) {
								ip = ip + temp16;
							}
						break;

					case 0xE1:	/* E1 LOOPZ Jb */
						temp16 = signext (getmem8 (X86_CS, ip) );
						StepIP (1);
						X86_CX = X86_CX - 1;
						if (X86_CX && (zf == 1) ) {
								ip = ip + temp16;
							}
						break;

					case 0xE2:	/* E2 LOOP Jb */
						temp16 = signext (getmem8 (X86_CS, ip) );
						StepIP (1);
						X86_CX = X86_CX - 1;
						if (X86_CX) {
								ip = ip + temp16;
							}
						break;

					case 0xE3:	/* E3 JCXZ Jb */
						temp16 = signext (getmem8 (X86_CS, ip) );
						StepIP (1);
						if (!X86_CX) {
								ip = ip + temp16;
							}
						break;

					case 0xE4:	/* E4 IN X86_AL Ib */
						oper1b = getmem8 (X86_CS, ip);
						StepIP (1);
						X86_AL = (uint8_t) portin (oper1b);
						break;

					case 0xE5:	/* E5 IN eAX Ib */
						oper1b = getmem8 (X86_CS, ip);
						StepIP (1);
						X86_AX = portin16 (oper1b);
						break;

					case 0xE6:	/* E6 OUT Ib X86_AL */
						oper1b = getmem8 (X86_CS, ip);
						StepIP (1);
						portout (oper1b, X86_AL);
						break;

					case 0xE7:	/* E7 OUT Ib eAX */
						oper1b = getmem8 (X86_CS, ip);
						StepIP (1);
						portout16 (oper1b, X86_AX);
						break;

					case 0xE8:	/* E8 CALL Jv */
						oper1 = getmem16 (X86_CS, ip);
						StepIP (2);
						push (ip);
						ip = ip + oper1;
						break;

					case 0xE9:	/* E9 JMP Jv */
						oper1 = getmem16 (X86_CS, ip);
						StepIP (2);
						ip = ip + oper1;
						break;

					case 0xEA:	/* EA JMP Ap */
						oper1 = getmem16 (X86_CS, ip);
						StepIP (2);
						oper2 = getmem16 (X86_CS, ip);
						ip = oper1;
						X86_CS = oper2;
						break;

					case 0xEB:	/* EB JMP Jb */
						oper1 = signext (getmem8 (X86_CS, ip) );
						StepIP (1);
						ip = ip + oper1;
						break;

					case 0xEC:	/* EC IN X86_AL regdx */
						oper1 = X86_DX;
						X86_AL = (uint8_t) portin (oper1);
						break;

					case 0xED:	/* ED IN eAX regdx */
						oper1 = X86_DX;
						X86_AX = portin16 (oper1);
						break;

					case 0xEE:	/* EE OUT regdx X86_AL */
						oper1 = X86_DX;
						portout (oper1, X86_AL);
						break;

					case 0xEF:	/* EF OUT regdx eAX */
						oper1 = X86_DX;
						portout16 (oper1, X86_AX);
						break;

					case 0xF0:	/* F0 LOCK */
						break;

					case 0xF4:	/* F4 HLT */
						hltstate = 1;
						break;

					case 0xF5:	/* F5 CMC */
						if (!cf) {
								cf = 1;
							}
						else {
								cf = 0;
							}
						break;

					case 0xF6:	/* F6 GRP3a Eb */
						modregrm();
						oper1b = readrm8 (rm);
						op_grp3_8();
						if ( (reg > 1) && (reg < 4) ) {
								writerm8 (rm, res8);
							}
						break;

					case 0xF7:	/* F7 GRP3b Ev */
						modregrm();
						oper1 = readrm16 (rm);
						op_grp3_16();
						if ( (reg > 1) && (reg < 4) ) {
								writerm16 (rm, res16);
							}
						break;

					case 0xF8:	/* F8 CLC */
						cf = 0;
						break;

					case 0xF9:	/* F9 STC */
						cf = 1;
						break;

					case 0xFA:	/* FA CLI */
						ifl = 0;
						break;

					case 0xFB:	/* FB STI */
						ifl = 1;
						break;

					case 0xFC:	/* FC CLD */
						df = 0;
						break;

					case 0xFD:	/* FD STD */
						df = 1;
						break;

					case 0xFE:	/* FE GRP4 Eb */
						modregrm();
						oper1b = readrm8 (rm);
						oper2b = 1;
						if (!reg) {
							const uint8_t tempcf = cf;
							res8 = oper1b + oper2b;
							flag_add8 (oper1b, oper2b);
							cf = tempcf;
							writerm8 (rm, res8);
						} else {
							const uint8_t tempcf = cf;
							res8 = oper1b - oper2b;
							flag_sub8 (oper1b, oper2b);
							cf = tempcf;
							writerm8 (rm, res8);
						}
						break;

					case 0xFF:	/* FF GRP5 Ev */
						modregrm();
						oper1 = readrm16 (rm);
						op_grp5();
						break;

					default:
#ifdef CPU_ALLOW_ILLEGAL_OP_EXCEPTION
						intcall86 (6); /* trip invalid opcode exception (this occurs on the 80186+, 8086/8088 CPUs treat them as NOPs. */
						               /* technically they aren't exactly like NOPs in most cases, but for our pursoses, that's accurate enough. */
#endif
						DEBUGPRINT("CPU: Illegal opcode: %02X %02X /%X @ %04X:%04X" NL, getmem8(savecs, saveip), getmem8(savecs, saveip+1), (getmem8(savecs, saveip+2) >> 3) & 7, savecs, saveip);
						break;
				}

skipexecution:
#if 0
			if (!running) {
					DEBUGPRINT("CPU: returning at !running with %u" NL, loopcount);
					return loopcount;
				}
#endif
			continue;	// damn C does not allow a label at the end of block, so I needed to put a "continue" here ;)
		}
	//DEBUGPRINT("CPU: returning at the end with %u" NL, loopcount);
	return loopcount;
}
//#endif
