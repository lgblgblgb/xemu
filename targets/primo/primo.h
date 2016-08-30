/* Test-case for a very simple and inaccurate Primo (a Hungarian U880 - Z80 compatible - based
   8 bit computer) emulator using SDL2 library.
   Copyright (C)2016 LGB (Gábor Lénárt) <lgblgblgb@gmail.com>

   This Primo emulator is HIGHTLY inaccurate and unusable.

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

#ifndef __XEMU_PRIMO_H_INCLUDED
#define __XEMU_PRIMO_H_INCLUDED

#define ROM_NAME		"primo-b64.rom"
#define SCREEN_FORMAT		SDL_PIXELFORMAT_ARGB8888
#define USE_LOCKED_TEXTURE	1
#define RENDER_SCALE_QUALITY	1
#define SCREEN_WIDTH		256
#define SCREEN_HEIGHT		192
#define CPU_CLOCK		2500000

#endif
