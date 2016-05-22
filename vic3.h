/* Test-case for a very simple, inaccurate, work-in-progress Commodore 65 emulator.
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

#ifndef __LGB_VIC3_H_INCLUDED
#define __LGB_VIC3_H_INCLUDED

extern int vic_new_mode;
extern int scanline;
extern Uint8 vic3_registers[];
extern int clock_divider7_hack;

extern void  vic3_init ( void );
extern void  vic3_write_reg ( int addr, Uint8 data );
extern Uint8 vic3_read_reg ( int addr );
extern void  vic3_write_palette_reg ( int num, Uint8 data );
extern void  vic3_render_screen ( void );
extern void  vic3_check_raster_interrupt ( void );


#endif
