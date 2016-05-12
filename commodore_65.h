/* Test-case for a very simple and inaccurate and not working Commodore 65 emulator using SDL2 library.
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

#ifndef __LGB_COMMODORE_65_H_INCLUDED
#define __LGB_COMMODORE_65_H_INCLUDED

#define SCREEN_FORMAT           SDL_PIXELFORMAT_ARGB8888
#define USE_LOCKED_TEXTURE	1
#define RENDER_SCALE_QUALITY	2
#define SCREEN_WIDTH		640
#define SCREEN_HEIGHT		400

#define CPU_CLOCK		3546875
#define FULL_FRAME_CPU_CYCLES	141875

#endif
