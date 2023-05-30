/* Commodore LCD emulator (rewrite of my world's first working Commodore LCD emulator)
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


#ifndef XEMU_CLCD_INJECT_H_INCLUDED
#define XEMU_CLCD_INJECT_H_INCLUDED

struct prg_inject_st {
	int phase;
	Uint8 data[55294];
	int size;
};

extern struct prg_inject_st prg_inject;

extern int  prg_load_prepare_inject   ( const char *file_name, int new_address );
extern int  prg_load_do_inject        ( const int address );
extern int  prg_save_prepare_store    ( const int address );
extern void prg_inject_periodic_check ( void );

#endif
