/* Xemu - Somewhat lame emulation (running on Linux/Unix/Windows/OSX, utilizing
   SDL2) of some 8 bit machines, including the Commodore LCD and Commodore 65
   and MEGA65 as well.
   Copyright (C)2016-2020 LGB (Gábor Lénárt) <lgblgblgb@gmail.com>

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

#ifndef XEMU_COMMON_EMUTOOLS_BASICDEFS_H_INCLUDED
#define XEMU_COMMON_EMUTOOLS_BASICDEFS_H_INCLUDED

#define COPYRIGHT_YEARS "2016-2020"

#include <stdio.h>
#include <limits.h>
#include <stdlib.h>

#define USE_REGPARM

// In case of RELEASE build, we don't support debugging (ie write detailed log file),
// since it introduces some overhead in form of many if's at critical places, even
// if it's not used.
#ifdef XEMU_RELEASE_BUILD
#define DISABLE_DEBUG
#endif

#ifndef XEMU_DISABLE_SDL
#ifndef XEMU_HAS_SDL2
#error  "We require SDL2, but XEMU_HAS_SDL2 was not defined: SDL2 cannot be detected?"
#endif
#include <SDL_types.h>
#include <SDL_endian.h>
#else
#ifdef XEMU_HAS_SDL2
#error "This build does not want SDL2, but XEMU_HAS_SDL2 was specified?"
#endif
#include <stdint.h>
typedef int8_t Sint8;
typedef uint8_t Uint8;
typedef int16_t Sint16;
typedef uint16_t Uint16;
typedef int32_t Sint32;
typedef uint32_t Uint32;
typedef int64_t Sint64;
typedef uint64_t Uint64;
/* this part is taken from SDL2 header ... */
#define SDL_LIL_ENDIAN  1234
#define SDL_BIG_ENDIAN  4321
#ifdef __linux__
#include <endian.h>
#define SDL_BYTEORDER  __BYTE_ORDER
#else /* __linux__ */
#if defined(__hppa__) || \
    defined(__m68k__) || defined(mc68000) || defined(_M_M68K) || \
    (defined(__MIPS__) && defined(__MISPEB__)) || \
    defined(__ppc__) || defined(__POWERPC__) || defined(_M_PPC) || \
    defined(__sparc__)
#define SDL_BYTEORDER   SDL_BIG_ENDIAN
#else
#define SDL_BYTEORDER   SDL_LIL_ENDIAN
#endif
#endif /* __linux__ */
#endif

#if UINTPTR_MAX == 0xffffffffU
#	define ARCH_32BIT
#	define ARCH_BITS 32
#	define ARCH_BITS_AS_TEXT "32"
#else
#	define ARCH_64BIT
#	define ARCH_BITS 64
#	define ARCH_BITS_AS_TEXT "64"
#endif

#if defined(__EMSCRIPTEN__)
#	define CC_TYPE "clang-emscripten"
#elif defined(__clang__)
#	define CC_TYPE "clang"
#elif defined(__MINGW64__)
#	define CC_TYPE "gcc-mingw64"
#elif defined(__MINGW32__)
#	define CC_TYPE "gcc-mingw32"
#elif defined(__GNUC__)
#	define CC_TYPE "gcc-compatible"
#else
#	define CC_TYPE "UNKNOWN-COMPILER"
#	warning "Unrecognizable C compiler"
#endif

#define XEMU_UNREACHABLE_FATAL_ERROR()	do { fprintf(stderr, "*** Unreachable code point hit in function %s\n", __func__); exit(1); } while(0)

#ifdef __GNUC__
#	define XEMU_LIKELY(__x__)	__builtin_expect(!!(__x__), 1)
#	define XEMU_UNLIKELY(__x__)	__builtin_expect(!!(__x__), 0)
#	ifdef DO_NOT_FORCE_UNREACHABLE
#		define XEMU_UNREACHABLE()	XEMU_UNREACHABLE_FATAL_ERROR()
#	else
#		define XEMU_UNREACHABLE()	__builtin_unreachable()
#	endif
#	ifdef DO_NOT_FORCE_INLINE
#		define XEMU_INLINE		inline
#	else
#		define XEMU_INLINE		__attribute__ ((__always_inline__)) inline
#	endif
#else
#	define XEMU_LIKELY(__x__)	(__x__)
#	define XEMU_UNLIKELY(__x__)	(__x__)
#	define XEMU_INLINE		inline
#	define XEMU_UNREACHABLE()	XEMU_UNREACHABLE_FATAL_ERROR()
#endif

#if defined(USE_REGPARM) && defined(__GNUC__) && !defined(__EMSCRIPTEN__)
#define REGPARM(__n__)	__attribute__ ((__regparm__ (__n__)))
#else
#define REGPARM(__n__)
#endif

/* Note: O_BINARY is a must for Windows for opening binary files, odd enough, I know ...
         So we always use O_BINARY in the code, and defining O_BINARY as zero for non-Windows systems, so it won't hurt at all.
	 Surely, SDL has some kind of file abstraction layer, but I seem to get used to some "native" code as well :-) */
#ifndef XEMU_ARCH_WIN
#	define O_BINARY		0
#	define DIRSEP_STR	"/"
#	define DIRSEP_CHR	'/'
#	define NL		"\n"
#	define PRINTF_LLD	"%lld"
#	define PRINTF_LLU	"%llu"
#	define MKDIR(__n)	mkdir((__n), 0777)
#else
#	define DIRSEP_STR	"\\"
#	define DIRSEP_CHR	'\\'
#	define NL		"\r\n"
#	define PRINTF_LLD	"%I64d"
#	define PRINTF_LLU	"%I64u"
#	define MKDIR(__n)	mkdir(__n)
#endif

extern FILE *debug_fp;
extern int chatty_xemu;

#ifdef DISABLE_DEBUG
#define DEBUG(...)
#define DEBUGPRINT(...) printf(__VA_ARGS__)
#else
#define DEBUG(...) do { 		\
	if (XEMU_UNLIKELY(debug_fp))	\
		fprintf(debug_fp, __VA_ARGS__);	\
} while (0)
#define DEBUGPRINT(...) do {		\
	if (chatty_xemu)		\
		printf(__VA_ARGS__);	\
	DEBUG(__VA_ARGS__);		\
} while (0)
#endif

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
	static volatile RegPair r;
	volatile RegPair *w = &r;
	w->_raw[0] = 0x01;
	w->_raw[1] = 0x23;
	w->_raw[2] = 0x45;
	w->_raw[3] = 0x67;
	return (r.b.l != ENDIAN_CHECKER_BYTE_L || r.b.h != ENDIAN_CHECKER_BYTE_H || r.w.w != ENDIAN_CHECKER_WORD || r.d != ENDIAN_CHECKER_DWORD);
}

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#define XEMUEXIT(n)	do { emscripten_cancel_main_loop(); emscripten_force_exit(n); exit(n); } while (0)
#else
#include <stdlib.h>
#define XEMUEXIT(n)	exit(n)
#endif

#define BOOLEAN_VALUE(n)	(!!(n))

extern const char *XEMU_BUILDINFO_ON, *XEMU_BUILDINFO_AT, *XEMU_BUILDINFO_GIT, *XEMU_BUILDINFO_CC, *XEMU_BUILDINFO_TARGET, *XEMU_BUILDINFO_CDATE;
extern const char emulators_disclaimer[];
extern void xemu_dump_version ( FILE *fp, const char *slogan );

#endif
