/* Preliminary Commander X16 emulation ...
   Copyright (C)2019 LGB (Gábor Lénárt) <lgblgblgb@gmail.com>

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

#ifndef __XEMU_LOCAL_COMMANDER_X16_H_INCLUDED
#define __XEMU_LOCAL_COMMANDER_X16_H_INCLUDED

#define SCREEN_FORMAT           SDL_PIXELFORMAT_ARGB8888

#define FULL_FRAME_USECS	39971

#define REAL_CPU_SPEED		1108404

#define USE_LOCKED_TEXTURE	1
#define RENDER_SCALE_QUALITY	2

#define ROM_NAME		"#cx16-system.rom"

extern Uint64 all_virt_cycles;

extern int dump_stuff ( const char *fn, void *mem, int size );


#endif
