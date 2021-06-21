/* Part of the Xemu project, please visit: https://github.com/lgblgblgb/xemu
   Copyright (C)2016-2021 LGB (Gábor Lénárt) <lgblgblgb@gmail.com>

   This is an odd emulator, emulating a Commodore 64 like machine only for the
   level needed for a special version of GEOS to be able to run on it.

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

#ifndef XEMU_CGEOS_GEOS_H_INCLUDED
#define XEMU_CGEOS_GEOS_H_INCLUDED


extern Uint8 memory[];
extern Uint8 colour_sram[];

extern int  geos_load_kernal ( const char *kernal_image_name );
extern void geos_cpu_trap ( Uint8 opcode );
extern void inject_screencoded_message ( int addr, const char *s );

#endif
