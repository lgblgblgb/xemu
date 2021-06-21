/* Test-case for a very simple and inaccurate Commodore VIC-20 emulator using SDL2 library.
   Part of the Xemu project, please visit: https://github.com/lgblgblgb/xemu
   Copyright (C)2016-2021 LGB (Gábor Lénárt) <lgblgblgb@gmail.com>

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

#ifndef XEMU_CVIC20_COMMODORE_VIC20_H_INCLUDED
#define XEMU_CVIC20_COMMODORE_VIC20_H_INCLUDED

#define SCREEN_DEFAULT_ZOOM     4
#define SCREEN_FORMAT           SDL_PIXELFORMAT_ARGB8888

#define FULL_FRAME_USECS	39971

#define REAL_CPU_SPEED		1108404

#define USE_LOCKED_TEXTURE	1
#define RENDER_SCALE_QUALITY	0

#define EMU_ROM_NAME		"#vic20-emulator-tool.rom"
#define EMU_ROM_VERSION		0
#define CHR_ROM_NAME		"#vic20-chargen.rom"
#define KERNAL_ROM_NAME		"#vic20-kernal.rom"
#define BASIC_ROM_NAME		"#vic20-basic.rom"

#endif
