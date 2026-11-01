/* A work-in-progess MEGA65 (Commodore 65 clone origins) emulator
   Part of the Xemu project, please visit: https://github.com/lgblgblgb/xemu
   Copyright (C)2016-2026 LGB (Gábor Lénárt) <lgblgblgb@gmail.com>

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
#include "math65.h"
#include "io_mapper.h"

bool math65_div_result_is_valid = false;
bool math65_mul_result_is_valid = false;

//#define NEW_CODE


static XEMU_INLINE void do_mult_update ( const Uint32 input_a, const Uint32 input_b )
{
	xemu_u64le_to_u8p(D7XX + 0x78, (Uint64)input_a * (Uint64)input_b);
	DEBUGPRINT("*** update multiplicator unit" NL);
	math65_mul_result_is_valid = true;
}


void math65_update_hw_mul_result ( void )
{
	do_mult_update(xemu_u8p_to_u32le(D7XX + 0x70), xemu_u8p_to_u32le(D7XX + 0x74));
}


#ifdef NEW_CODE

#error "New code is not yet ready. Comment this #error out if you are serious"

#define UINT160_UINT32_LENGTH 5
typedef Uint32 Uint160[UINT160_UINT32_LENGTH];


static inline void mul32 ( Uint32 *lo, Uint32 *hi, Uint32 a, Uint32 b )
{
	const Uint64 result = (Uint64)a * (Uint64)b;
	*lo = (Uint32)result;
	*hi = (Uint32)(result >> 32);
}


static void mul160 ( Uint160 result, Uint160 a, Uint160 b )
{
	memset(result, 0, sizeof(Uint160));
	for (int i = 0; i < UINT160_UINT32_LENGTH; i++) {
		Uint64 carry = 0;
		for (int j = 0; j < UINT160_UINT32_LENGTH - i; j++) {
			Uint32 lo, hi;
			mul32(&lo, &hi, a[i], b[j]);
			const Uint64 sum = (Uint64)result[i + j] + (Uint64)lo + carry;
			result[i + j] = (Uint32)sum;
			carry = (Uint64)hi + (sum >> 32);
		}
	}
}


static inline void do_fpga_divide ( Uint32 n, Uint32 d )
{
	const int leading_zeroes = __builtin_clz(d);	// gcc/clang built-in
	// TODO!
}


void math65_update_hw_div_result ( void )
{
	const Uint32 input_a = xemu_u8p_to_u32le(D7XX + 0x70);
	const Uint32 input_b = xemu_u8p_to_u32le(D7XX + 0x74);
	// Multiplication is light-weight (compared to the iteration based exact emulation of mega65-core algorithm for divide), so do it here anyway:
	if (!math65_mul_result_is_valid)
		do_mult_update(input_a, input_b);
	// --- Do the quotient, divide ---
	if (input_b) {	// ... only if it's not dividing by zero!
		do_fpga_divide(input_a, input_b);
	}
	DEBUGPRINT("*** update divider unit" NL);
	math65_div_result_is_valid = true;
}


#else


void math65_update_hw_div_result ( void )
{
	const Uint32 input_a = xemu_u8p_to_u32le(D7XX + 0x70);
	const Uint32 input_b = xemu_u8p_to_u32le(D7XX + 0x74);
	// Multiplication is light-weight, so do it here anyway:
	if (!math65_mul_result_is_valid)
		do_mult_update(input_a, input_b);
	// --- Do the quotient, divide ---
	// ... but we really don't want to divide by zero, so let's test this
	if (XEMU_LIKELY(input_b)) {
		// input_b is non-zero, it's OK to divide
		xemu_u64le_to_u8p(D7XX + 0x68, (Uint64)((Uint64)input_a << 32) / (Uint64)input_b);
	} else {
		// Previously VHDL set all bytes to $FF in case of division by zero.
		// However it seems it has changed, and now it's set to $00.
		// https://github.com/MEGA65/mega65-core/commit/ed1794e5f49dc09ed415d7fd4fb788ddee1c0e6c
		memset(D7XX + 0x68, 0x00, 8);
	}
	DEBUGPRINT("*** update divider unit" NL);
	math65_div_result_is_valid = true;
}


#endif
