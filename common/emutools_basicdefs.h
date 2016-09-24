/* Xemu - Somewhat lame emulation (running on Linux/Unix/Windows/OSX, utilizing
   SDL2) of some 8 bit machines, including the Commodore LCD and Commodore 65
   and some Mega-65 features as well.
   Copyright (C)2016 LGB (Gábor Lénárt) <lgblgblgb@gmail.com>

   The goal of emutools.c is to provide a relative simple solution
   for relative simple emulators using SDL2.

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

#ifndef __XEMU_COMMON_EMUTOOLS_BASICDEFS_H_INCLUDED
#define __XEMU_COMMON_EMUTOOLS_BASICDEFS_H_INCLUDED

#include <stdio.h>
#include <SDL_types.h>
#include <SDL_endian.h>

#define USE_REGPARM

#if UINTPTR_MAX == 0xffffffff
#	define ARCH_32BIT
#	define ARCH_BITS 32
#else
#	define ARCH_64BIT
#	define ARCH_BITS 64
#endif

#if defined(__clang__)
#	define CC_TYPE "clang"
#elif defined(__MINGW64__)
#	define CC_TYPE "mingw64"
#elif defined(__MINGW32__)
#	define CC_TYPE "mingw32"
#elif defined(__GNUC__)
#	define CC_TYPE "gcc"
#else
#	define CC_TYPE "Something"
#	warning "Unrecognizable C compiler"
#endif

#ifdef __GNUC__
#define likely(__x__)	__builtin_expect(!!(__x__), 1)
#define unlikely(__x__)	__builtin_expect(!!(__x__), 0)
#define INLINE		__attribute__ ((__always_inline__)) inline
#else
#define likely(__x__)	(__x__)
#define unlikely(__x__)	(__x__)
#define INLINE		inline
#endif

#if defined(USE_REGPARM) && defined(__GNUC__)
#define REGPARM(__n__)	__attribute__ ((__regparm__ (__n__)))
#else
#define REGPARM(n)
#endif

/* Note: O_BINARY is a must for Windows for opening binary files, odd enough, I know ...
         So we always use O_BINARY in the code, and defining O_BINARY as zero for non-Windows systems, so it won't hurt at all.
	 Surely, SDL has some kind of file abstraction layer, but I seem to get used to some "native" code as well :-) */
#ifndef _WIN32
#	define O_BINARY		0
#	define DIRSEP_STR	"/"
#	define DIRSEP_CHR	'/'
#	define NL		"\n"
#	define PRINTF_LLD	"%lld"
#	define PRINTF_LLU	"%llu"
#else
#	define DIRSEP_STR	"\\"
#	define DIRSEP_CHR	'\\'
#	define NL		"\r\n"
#	define PRINTF_LLD	"%I64d"
#	define PRINTF_LLU	"%I64u"
#endif

extern FILE *debug_fp;

#define DEBUG(...) do { \
	if (unlikely(debug_fp))	\
		fprintf(debug_fp, __VA_ARGS__);	\
} while (0)


#ifndef __BIGGEST_ALIGNMENT__
#define __BIGGEST_ALIGNMENT__	16
#endif
#define ALIGNED(n) __attribute__ ((aligned (n)))
#define MAXALIGNED ALIGNED(__BIGGEST_ALIGNMENT__)

/* ---- BYTE ORDER RELATED STUFFS ---- */

#ifndef SDL_BYTEORDER
#	error "SDL_BYTEORDER is not defined!"
#endif
#if SDL_BYTEORDER == SDL_LIL_ENDIAN
#	ifdef Z80EX_WORDS_BIG_ENDIAN
#		undef Z80EX_WORDS_BIG_ENDIAN
#	endif
#	define ENDIAN_NAME		"LE"
#	define ENDIAN_GOOD
#	define ENDIAN_CHECKER_BYTE_L	0x01
#	define ENDIAN_CHECKER_BYTE_H	0x23
#	define ENDIAN_CHECKER_WORD	0x2301
#	define ENDIAN_CHECKER_DWORD	0x67452301
typedef union {
	Uint8  _raw[4];	// 01 23 45 67
	struct { Uint8  l,h,_un1,_un2; } b;
	struct { Uint16 w,_unw; } w;
	Uint32 d;
} RegPair;
#elif SDL_BYTEORDER == SDL_BIG_ENDIAN
#	ifndef Z80EX_WORDS_BIG_ENDIAN
#		define Z80EX_WORDS_BIG_ENDIAN
#	endif
#	define ENDIAN_NAME		"BE"
#	define ENDIAN_UGLY
#	define ENDIAN_CHECKER_BYTE_L    0x67
#	define ENDIAN_CHECKER_BYTE_H    0x45
#	define ENDIAN_CHECKER_WORD      0x4567
#	define ENDIAN_CHECKER_DWORD     0x01234567
typedef union {
	Uint8  _raw[4];
	struct { Uint8 _un1,_un2,h,l; } b;
	struct { Uint16 _unw,w; } w;
	Uint32 d;
} RegPair;
#else
#	error "SDL_BYTEORDER is not SDL_LIL_ENDIAN neither SDL_BIG_ENDIAN"
#endif
static inline int xemu_byte_order_test ( void )
{
	RegPair r;
	r._raw[0] = 0x01;
	r._raw[1] = 0x23;
	r._raw[2] = 0x45;
	r._raw[3] = 0x67;
	return (r.b.l != ENDIAN_CHECKER_BYTE_L || r.b.h != ENDIAN_CHECKER_BYTE_H || r.w.w != ENDIAN_CHECKER_WORD || r.d != ENDIAN_CHECKER_DWORD);
}

#endif
