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

#ifndef __XEMU_COMMON_CBMHOSTFS_H_INCLUDED
#define __XEMU_COMMON_CBMHOSTFS_H_INCLUDED

extern void  hostfs_init       ( const char *basedir, const char *subdir );
extern void  hostfs_close_all  ( void );
extern void  hostfs_flush_all  ( void );
extern Uint8 hostfs_read_reg0  ( void );
extern Uint8 hostfs_read_reg1  ( void );
extern void  hostfs_write_reg0 ( Uint8 data );
extern void  hostfs_write_reg1 ( Uint8 data );

#endif
