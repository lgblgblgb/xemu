/* A work-in-progess MEGA65 (Commodore 65 clone origins) emulator
   Part of the Xemu project, please visit: https://github.com/lgblgblgb/xemu
   Copyright (C)2016-2020 LGB (Gábor Lénárt) <lgblgblgb@gmail.com>

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

#ifndef XEMU_MEGA65_MEMCONTENT_H_INCLUDED
#define XEMU_MEGA65_MEMCONTENT_H_INCLUDED

#define MEMINITDATA_KICKSTART_SIZE	0x4000
#define MEMINITDATA_CHRWOM_SIZE		0x1000
#define MEMINITDATA_CRAMUTILS_SIZE	0x8000
#define MEMINITDATA_BANNER_SIZE		21248

extern Uint8 meminitdata_kickstart[MEMINITDATA_KICKSTART_SIZE];
extern Uint8 meminitdata_chrwom[MEMINITDATA_CHRWOM_SIZE];
extern Uint8 meminitdata_cramutils[MEMINITDATA_CRAMUTILS_SIZE];
extern Uint8 meminitdata_banner[MEMINITDATA_BANNER_SIZE];
extern Uint8 meminitdata_freezer[];

extern const int meminitdata_freezer_size;
#define MEMINITDATA_FREEZER_SIZE	meminitdata_freezer_size

#endif
