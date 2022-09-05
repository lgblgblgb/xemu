/* Part of the Xemu project, please visit: https://github.com/lgblgblgb/xemu
   Copyright (C)2017-2022 LGB (Gábor Lénárt) <lgblgblgb@gmail.com>

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

#ifndef XEMU_HAS_SOCKET_API
#error "Need HAVE_XEMU_SOCKET_API for HAVE_XEMU_UMON to be enabled at the target!"
#endif

#define XUMON_DEFAULT_PORT	9000

struct xumon_com_st {
	int	size;
	Uint8	*data;
	int	seq;
	const void *ptr;
};

extern int xumon_running;

extern int xumon_init ( const int port );
extern int xumon_stop ( void );

extern int xumon_get_request ( struct xumon_com_st *res );
extern int xumon_set_answer  ( struct xumon_com_st *res );

#endif
#endif
