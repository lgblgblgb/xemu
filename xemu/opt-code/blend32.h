/* A work-in-progess MEGA65 (Commodore 65 clone origins) emulator
   Part of the Xemu project, please visit: https://github.com/lgblgblgb/xemu
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

#ifndef XEMU_COMMON_ARCH_OPT_BLEND32_H_INCLUDED
#define XEMU_COMMON_ARCH_OPT_BLEND32_H_INCLUDED

#if defined(__SSE2__)
#include <emmintrin.h>

static XEMU_INLINE Uint32 blend32 ( const Uint32 a, const Uint32 b, const Uint8 s )
{
	__m128i va = _mm_cvtsi32_si128(a); // move a to XMM reg
	__m128i vb = _mm_cvtsi32_si128(b);
	__m128i zero = _mm_setzero_si128();
	__m128i a_unpacked = _mm_unpacklo_epi8(va, zero); // [00 AA 00 RR 00 GG 00 BB]
	__m128i b_unpacked = _mm_unpacklo_epi8(vb, zero);
	// scale factors: s and (255 - s)
	__m128i scale_a = _mm_set1_epi16(s);
	__m128i scale_b = _mm_set1_epi16(255 - s);
	// Multiply: (a * s) and (b * (255 - s))
	__m128i a_scaled = _mm_mullo_epi16(a_unpacked, scale_a);	// a_scaled = a * s
	__m128i b_scaled = _mm_mullo_epi16(b_unpacked, scale_b);	// b_scaled = b * (255 - s)
	// Add +127 for rounding
	__m128i sum = _mm_add_epi16(_mm_add_epi16(a_scaled, b_scaled), _mm_set1_epi16(127));
	// Divide by 255 (approximation using multiply + shift)
	__m128i result = _mm_mulhi_epu16(sum, _mm_set1_epi16(257)); // (x + 127) / 255 ~= (x * 257) >> 16
	// Pack back to 8-bit
	result = _mm_packus_epi16(result, zero);
	return _mm_cvtsi128_si32(result);
}

#elif defined(__ARM_NEON) || defined(__ARM_NEON__)
#include <arm_neon.h>

static XEMU_INLINE Uint32 blend32 ( const Uint32 a, const Uint32 b, const Uint8 s )
{
	// Load A and B into NEON vector registers
	uint8x8_t va = vreinterpret_u8_u32(vdup_n_u32(a));
	uint8x8_t vb = vreinterpret_u8_u32(vdup_n_u32(b));
	// Duplicate scalar S into a vector
	uint8x8_t vs = vdup_n_u8(s);
	uint8x8_t is = vdup_n_u8(255 - s);
	// Multiply and accumulate: (a*s + b*(255-s))
	uint16x8_t mul_a = vmull_u8(va, vs);
	uint16x8_t mul_b = vmull_u8(vb, is);
	uint16x8_t sum = vaddq_u16(mul_a, mul_b);
	// Add rounding bias (127) and divide by 255
	sum = vaddq_u16(sum, vdupq_n_u16(127));
	uint8x8_t result = vshrn_n_u16(sum, 8); // Divide by 256 (approximate 255)
	// Return result as Uint32
	return vget_lane_u32(vreinterpret_u32_u8(result), 0);
}

#else

static XEMU_INLINE Uint32 blend32 ( const Uint32 a, const Uint32 b, const Uint8 s )
{
	// Used by the FCM alpha-blend renderer on SDL2 colour representation (RGBA
	// per bytes within 32 bit unsigned integers). Technically I wouldn't need
	// to "blend" the alpha channel, however I cannot be sure which byte is alpha
	// (platform dependent, byte order ...). But since alpha channel is always $FF
	// for "A" and "B" too, blending those wouldn't hurt. And btw, do not confuse
	// the "alpha" in RGBA and the nature of alpha-blending, independent things.
#if 1
	return
	  (( a        & 0xFF) * s + ( b        & 0xFF) * (255U - s)) / 255U         |
	(((((a >>  8) & 0xFF) * s + ((b >>  8) & 0xFF) * (255U - s)) / 255U) <<  8) |
	(((((a >> 16) & 0xFF) * s + ((b >> 16) & 0xFF) * (255U - s)) / 255U) << 16) |
	(((( a >> 24        ) * s + ( b >> 24        ) * (255U - s)) / 255U) << 24);
#else
	// 15.3% vs 15.6% (slightly faster) on my PC
	return
	( (( (a        & 0xFF) * s + ( b        & 0xFF) * (255 - s)) * 257) >> 16       ) +
	((((((a >>  8) & 0xFF) * s + ((b >>  8) & 0xFF) * (255 - s)) * 257) >> 16) <<  8) +
	((((((a >> 16) & 0xFF) * s + ((b >> 16) & 0xFF) * (255 - s)) * 257) >> 16) << 16) +
	((((((a >> 24) & 0xFF) * s + ((b >> 24) & 0xFF) * (255 - s)) * 257) >> 16) << 24) ;
#endif
}

#endif

#endif
