/* Test-case for a very simple and inaccurate Commodore VIC-20 emulator using SDL2 library.
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

#ifndef __LGB_COMMODORE_VIC20_H_INCLUDED
#define __LGB_COMMODORE_VIC20_H_INCLUDED

// CPU clock of PAL models (actually 1108404.5Hz ...)
//#define CPU_CLOCK               1108404
//#define CPU_CYCLES_PER_TV_FRAME 44336

//#define SCREEN_WIDTH            176
//#define SCREEN_HEIGHT           184
#define SCREEN_DEFAULT_ZOOM     4
#define SCREEN_FORMAT           SDL_PIXELFORMAT_ARGB8888

/* Defines the X ("dotpos") and Y ("scanline") parameters of the visible (emulated) screen area */
#define SCREEN_FIRST_VISIBLE_SCANLINE	 28
#define SCREEN_LAST_VISIBLE_SCANLINE	311
#define SCREEN_FIRST_VISIBLE_DOTPOS	 12
#define SCREEN_LAST_VISIBLE_DOTPOS	235

/* Size of texture the emulator will display in pixels, including border, etc, too! */
#define SCREEN_HEIGHT 		(SCREEN_LAST_VISIBLE_SCANLINE - SCREEN_FIRST_VISIBLE_SCANLINE + 1)
#define SCREEN_WIDTH		(SCREEN_LAST_VISIBLE_DOTPOS   - SCREEN_FIRST_VISIBLE_DOTPOS   + 1)

/* Usual (X/Y) origin of the screen (left bottom corner) if VIC-I X/Y origin registers are zero */
// maybe SCANLINE ORIGIN is 38 ... I guess :)
#define SCREEN_ORIGIN_SCANLINE		38
#define SCREEN_ORIGIN_DOTPOS		12



#define USE_LOCKED_TEXTURE	1
#define RENDER_SCALE_QUALITY	1

#endif
