/* Xemu - Somewhat lame emulation (running on Linux/Unix/Windows/OSX, utilizing
   SDL2) of some 8 bit machines, including the Commodore LCD and Commodore 65
   and MEGA65 as well.
   Copyright (C)2017-2020 LGB (Gábor Lénárt) <lgblgblgb@gmail.com>

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

#ifndef __XEMU_COMMON_EMUTOOLS_UMON_H_INCLUDED
#define __XEMU_COMMON_EMUTOOLS_UMON_H_INCLUDED
#ifdef HAVE_XEMU_UMON
#ifndef HAVE_XEMU_SOCKET_API
#error "Need HAVE_XEMU_SOCKET_API for HAVE_XEMU_UMON to be enabled at the target!"
#endif

extern volatile int xumon_is_running;

extern int xumon_init ( int port, int threaded );

#endif
#endif
