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

#define SCREEN_DEFAULT_ZOOM     4
#define SCREEN_FORMAT           SDL_PIXELFORMAT_ARGB8888

/* Defines the X ("dotpos") and Y ("scanline") parameters of the visible (emulated) screen area */
#define SCREEN_FIRST_VISIBLE_SCANLINE	 28
#define SCREEN_LAST_VISIBLE_SCANLINE	311
#define SCREEN_FIRST_VISIBLE_DOTPOS	 38
#define SCREEN_LAST_VISIBLE_DOTPOS	261

/* Size of texture the emulator will display in pixels, including border, etc, too! */
#define SCREEN_HEIGHT 		(SCREEN_LAST_VISIBLE_SCANLINE - SCREEN_FIRST_VISIBLE_SCANLINE + 1)
#define SCREEN_WIDTH		(SCREEN_LAST_VISIBLE_DOTPOS   - SCREEN_FIRST_VISIBLE_DOTPOS   + 1)

#if ((SCREEN_WIDTH) & 1) != 0
#error "Screen width must be evaluted to an even number!"
#endif

/* Usual (X/Y) origin of the screen (left bottom corner) if VIC-I X/Y origin registers are zero */
// maybe SCANLINE ORIGIN is 38 ... I guess :)
//#define SCREEN_ORIGIN_SCANLINE		38
//#define SCREEN_ORIGIN_DOTPOS		12
#define SCREEN_ORIGIN_SCANLINE          0
#define SCREEN_ORIGIN_DOTPOS            12


#if ((SCREEN_ORIGIN_DOTPOS) & 1) != 0
#error "SCREEN_ORIGIN_DOTPOS must be an even number!"
#endif


#define USE_LOCKED_TEXTURE	1
#define RENDER_SCALE_QUALITY	2

#endif
