/* Xemu - Somewhat lame emulation (running on Linux/Unix/Windows/OSX, utilizing
   SDL2) of some 8 bit machines, including the Commodore LCD and Commodore 65
   and MEGA65 as well.
   Copyright (C)2018,2020 LGB (Gábor Lénárt) <lgblgblgb@gmail.com>

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

#ifndef __XEMU_COMMON_ETHERTAP_H_INCLUDED
#define __XEMU_COMMON_ETHERTAP_H_INCLUDED
#ifdef HAVE_ETHERTAP

#include "xemu/emutools_basicdefs.h"

extern int xemu_tuntap_close  ( void );
extern int xemu_tuntap_alloc  ( const char *dev_in, char *dev_out, int dev_out_size, unsigned int flags );
extern int xemu_tuntap_read   ( void *buffer, int min_size, int max_size );
extern int xemu_tuntap_write  ( void *buffer, int size );
extern int xemu_tuntap_select ( int flags, int timeout_usecs );


// for xemu_tuntap_alloc():

#define XEMU_TUNTAP_IS_TAP		1
#define XEMU_TUNTAP_IS_TUN		2
#define XEMU_TUNTAP_NO_PI		0x100
#define XEMU_TUNTAP_NONBLOCKING_IO	0x200

// for xemu_tuntap_select:

#define XEMU_TUNTAP_SELECT_R		1
#define XEMU_TUNTAP_SELECT_W		2

#endif
#endif
