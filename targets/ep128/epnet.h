/* Xep128: Minimalistic Enterprise-128 emulator with focus on "exotic" hardware
   Copyright (C)2015,2016,2020 LGB (Gábor Lénárt) <lgblgblgb@gmail.com>
   http://xep128.lgb.hu/

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

#ifndef XEMU_EP128_EPNET_H_INCLUDED
#define XEMU_EP128_EPNET_H_INCLUDED
#ifdef CONFIG_EPNET_SUPPORT

extern Uint8 epnet_read_cpu_port  ( unsigned int port );
extern void  epnet_write_cpu_port ( unsigned int port, Uint8 data );
extern void  epnet_init ( void (*cb)(int) );
extern void  epnet_uninit ( void );

#endif
#endif
