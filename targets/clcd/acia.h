/* Commodore LCD emulator.
   Copyright (C)2016-2023 LGB (Gábor Lénárt) <lgblgblgb@gmail.com>
   Part of the Xemu project: https://github.com/lgblgblgb/xemu

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

#ifndef XEMU_ACIA_H_INCLUDED
#define XEMU_ACIA_H_INCLUDED

typedef void(*acia_interrupt_setter_t)(const int actve);

extern void  acia_reset     ( void );
extern void  acia_init      ( acia_interrupt_setter_t int_setter );
extern void  acia_write_reg ( const int reg, const Uint8 data );
extern Uint8 acia_read_reg  ( const int reg );

#endif
