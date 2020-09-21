/* Part of the Xemu project, please visit: https://github.com/lgblgblgb/xemu
   Copyright (C)2016-2020 LGB (Gábor Lénárt) <lgblgblgb@gmail.com>

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

#ifdef	XEMU_XEMU_ARCH_SYS_H_INCLUDED
#	error "xemu/arch-sys.h cannot be included multiple times (and it's included by C compiler command line)."
#endif
#define	XEMU_XEMU_ARCH_SYS_H_INCLUDED

#ifndef	_ISOC11_SOURCE
#	define	_ISOC11_SOURCE
#endif
// We need this otherwise stupid things happen like M_E is not defined by math.h, grrr.
#ifndef _DEFAULT_SOURCE
#	define	_DEFAULT_SOURCE
#endif
//#ifdef __STRICT_ANSI__
//#	undef __STRICT_ANSI__
//#endif

// Generic stuff to signal we're inside XEMU build
// Useful for multi-purpose sources, can be also compiled out-of-source-tree, and stuff like that ...
#define	XEMU_BUILD

#ifdef	__EMSCRIPTEN__
#	define	XEMU_ARCH_HTML
#	define	XEMU_ARCH_NAME	"html"
#	ifndef	DISABLE_DEBUG
#		define	DISABLE_DEBUG
#	endif
//#	define	XEMU_OLD_TIMING
#elif	defined(_WIN64)
#	define	XEMU_ARCH_WIN64
#	define	XEMU_ARCH_WIN
#	define	XEMU_ARCH_NAME	"win64"
#	define	XEMU_SLEEP_IS_USLEEP
#elif	defined(_WIN32)
#	define	XEMU_ARCH_WIN32
#	define	XEMU_ARCH_WIN
#	define	XEMU_ARCH_NAME	"win32"
#	define	XEMU_SLEEP_IS_USLEEP
#elif	defined(__APPLE__)
	// Actually, MacOS / OSX is kinda UNIX, but for some minor reasons we handle it differently here
#	include	<TargetConditionals.h>
#	ifndef	TARGET_OS_MAC
#		error	"Unknown Apple platform (TARGET_OS_MAC is not defined by TargetConditionals.h)"
#	endif
#	define	XEMU_ARCH_OSX
#	define	XEMU_ARCH_MAC
#	define	XEMU_ARCH_UNIX
#	define	XEMU_ARCH_NAME	"osx"
#	define	XEMU_SLEEP_IS_NANOSLEEP
#	define	_XOPEN_SOURCE	100
#elif	defined(__unix__) || defined(__unix) || defined(__linux__) || defined(__linux)
#	define	XEMU_ARCH_UNIX
#	if	defined(__linux__) || defined(__linux)
#		define	XEMU_ARCH_LINUX
#		define	XEMU_ARCH_NAME	"linux"
#	else
#		define	XEMU_ARCH_NAME	"unix"
#	endif
#	define	XEMU_SLEEP_IS_NANOSLEEP
#else
#	error	"Unknown target OS architecture."
#endif

#if defined(XEMU_ARCH_UNIX) && !defined(_XOPEN_SOURCE)
#	define	_XOPEN_SOURCE	700
#endif

#if defined(XEMU_ARCH_WIN) && !defined(_USE_MATH_DEFINES)
	// It seems, some (?) versions of Windows requires _USE_MATH_DEFINES to be defined to define some math constants by math.h
#	define	_USE_MATH_DEFINES
#endif

// It seems Mingw on windows defaults to 32 bit off_t which causes problems, even on win64
// In theory this _FILE_OFFSET_BITS should work for UNIX as well (though maybe that's default there since ages?)
// Mingw "should" support this since 2011 or so ... Thus in nutshell: use this trick to enable large file support
// in general, regardless of the OS, UNIX-like or Windows. Hopefully it will work.
#ifdef	_FILE_OFFSET_BITS
#	undef	_FILE_OFFSET_BITS
#endif
#define	_FILE_OFFSET_BITS	64
