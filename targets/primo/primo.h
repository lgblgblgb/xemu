/* Test-case for a very simple Primo (a Hungarian U880 - Z80
   compatible clone CPU - based 8 bit computer) emulator.
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

#ifndef XEMU_PRIMO_PRIMO_H_INCLUDED
#define XEMU_PRIMO_PRIMO_H_INCLUDED

#define SCREEN_FORMAT		SDL_PIXELFORMAT_ARGB8888
#define USE_LOCKED_TEXTURE	1
#define RENDER_SCALE_QUALITY	1
#define SCREEN_WIDTH		300
#define SCREEN_HEIGHT		230
#define DEFAULT_CPU_CLOCK	2500000

#endif
