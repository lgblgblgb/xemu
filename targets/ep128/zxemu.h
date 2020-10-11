/* Minimalistic Enterprise-128 emulator with focus on "exotic" hardware
   Part of the Xemu project, please visit: https://github.com/lgblgblgb/xemu
   Copyright (C)2015-2016,2020 LGB (Gábor Lénárt) <lgblgblgb@gmail.com>

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

#ifndef XEMU_EP128_ZXEMU_H_INCLUDED
#define XEMU_EP128_ZXEMU_H_INCLUDED

#define IO16_HI_BYTE(port16) (((ports[(((port16) >> 14) & 3) | 0xB0] & 3) << 6) | (((port16) >> 8) & 0x3F))

extern void  zxemu_write_ula ( Uint8 hiaddr, Uint8 data );
extern Uint8 zxemu_read_ula ( Uint8 hiaddr );
extern void  zxemu_attribute_memory_write ( Uint16 address, Uint8 data );
extern void  zxemu_switch ( Uint8 data );

extern int   zxemu_on, nmi_pending;

#endif
