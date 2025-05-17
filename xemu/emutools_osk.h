/* Xemu - emulation (running on Linux/Unix/Windows/OSX, utilizing SDL2) of some
   8 bit machines, including the Commodore LCD and Commodore 65 and MEGA65 as well.
   Copyright (C)2016-2025 LGB (Gábor Lénárt) <lgblgblgb@gmail.com>

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

#if	defined(XEMU_OSK_SUPPORT) && !defined(XEMU_COMMON_EMUTOOLS_OSK_H_INCLUDED)
#define	XEMU_COMMON_EMUTOOLS_OSK_H_INCLUDED

#define OSK_DESC_NEW_LINE	1
#define OSK_DESC_MOD_KEY	2
#define OSK_DESC_MODABLE	4
#define OSK_DESC_TOGGLE		8

struct osk_desc_st {
	const Uint32 flags;
	const int gap_before;
	const int key_len;
	const char *key_str;
	const int key_id;
};

extern bool osk_in_use;

extern bool osk_init ( const struct osk_desc_st *desc );
extern bool osk_render ( void );


#endif
