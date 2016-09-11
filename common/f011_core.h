/* Test-case for a very simple, inaccurate, work-in-progress Commodore 65 / Mega-65 emulator,
   within the Xemu project. F011 FDC core implementation.
   Copyright (C)2016 LGB (Gábor Lénárt) <lgblgblgb@gmail.com>

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

#ifndef __XEMU_F011_CORE_H_INCLUDED
#define __XEMU_F011_CORE_H_INCLUDED

extern void  fdc_write_reg ( int addr, Uint8 data );
extern Uint8 fdc_read_reg  ( int addr );
extern void  fdc_init      ( void );
extern void  fdc_set_disk  ( int in_have_disk, int in_have_write );
extern int   fdc_cb_rd_sec ( Uint8 *buffer, int offset );
extern int   fdc_cb_wr_sec ( Uint8 *buffer, int offset );

#endif