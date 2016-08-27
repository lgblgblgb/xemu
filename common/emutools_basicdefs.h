/* Commodore LCD emulator using SDL2 library. Also includes:
   Test-case for a very simple and inaccurate Commodore VIC-20 emulator.
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

#ifndef __LGB_EMUTOOLS_BASICDEFS_H_INCLUDED
#define __LGB_EMUTOOLS_BASICDEFS_H_INCLUDED

#include <stdio.h>
#include <SDL_types.h>

#ifdef __GNUC__
#define likely(x)       __builtin_expect(!!(x), 1)
#define unlikely(x)     __builtin_expect(!!(x), 0)
#else
#define likely(x)       (x)
#define unlikely(x)     (x)
#endif

/* Note: O_BINARY is a must for Windows for opening binary files, odd enough, I know ...
         So we always use O_BINARY in the code, and defining O_BINARY as zero for non-Windows systems, so it won't hurt at all.
	 Surely, SDL has some kind of file abstraction layer, but I seem to get used to some "native" code as well :-) */
#ifndef _WIN32
#	define O_BINARY		0
#	define DIRSEP_STR	"/"
#	define DIRSEP_CHR	'/'
#	define NL		"\n"
#else
#	define DIRSEP_STR	"\\"
#	define DIRSEP_CHR	'\\'
#	define NL		"\r\n"
#endif

extern FILE *debug_fp;

#define DEBUG(...) do { \
	if (unlikely(debug_fp))	\
		fprintf(debug_fp, __VA_ARGS__);	\
} while (0)


#ifndef __BIGGEST_ALIGNMENT__
#define __BIGGEST_ALIGNMENT__	16
#endif
#define VARALIGN __attribute__ ((aligned (__BIGGEST_ALIGNMENT__)))

#endif
