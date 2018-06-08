/* Z8001 CPU emulator
 * Part of the Xemu project, please visit: https://github.com/lgblgblgb/xemu
 * Copyright (C)2018 LGB (Gábor Lénárt) <lgblgblgb@gmail.com>

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

#ifndef __XEMU_C900_Z8K1_H_INCLUDED
#define __XEMU_C900_Z8K1_H_INCLUDED

extern void z8k1_init  ( void );
extern void z8k1_reset ( void );
extern int  z8k1_step  ( int cycles_limit );

// Need to be defined by the user:
// (surely for C900 emulation, these must be fed into the Z8010 MMU emulation then ...)

extern Uint8  z8k1_read_byte_cb		( int seg, Uint16 ofs );
extern Uint16 z8k1_read_word_cb		( int seg, Uint16 ofs );
extern void   z8k1_write_byte_cb	( int seg, Uint16 ofs, Uint8  data );
extern void   z8k1_write_word_cb	( int seg, Uint16 ofs, Uint16 data );

extern Uint8  z8k1_in_byte_cb		( int special, Uint16 addr );
extern Uint16 z8k1_in_word_cb		( int special, Uint16 addr );
extern void   z8k1_out_byte_cb		( int special, Uint16 addr, Uint8  data );
extern void   z8k1_out_word_cb		( int special, Uint16 addr, Uint16 data );

#endif
