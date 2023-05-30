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

#ifndef XEMU_CLCD_CONFIGDB_H_INCLUDED
#define XEMU_CLCD_CONFIGDB_H_INCLUDED

extern void configdb_define_emulator_options ( void );

struct configdb_st {
	int fullscreen_requested;
	int syscon;
	int zoom;
	int sdlrenderquality;
	int ram_size_kbytes;
	int clock_mhz;
	char *rom102_fn;
	char *rom103_fn;
	char *rom104_fn;
	char *rom105_fn;
	char *romchr_fn;
	char *prg_inject_fn;
	char *gui_selection;
	char *dumpmem;
};

extern struct configdb_st configdb;

#endif
