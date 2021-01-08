/* Commodore LCD emulator.
   Copyright (C)2016,2018-2021 LGB (Gábor Lénárt) <lgblgblgb@gmail.com>
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

#ifndef XEMU_COMMODORE_LCD_H_INCLUDED
#define XEMU_COMMODORE_LCD_H_INCLUDED

#define SCREEN_WIDTH            480
#define SCREEN_HEIGHT           128
#define SCREEN_DEFAULT_ZOOM     2
#define SCREEN_FORMAT           SDL_PIXELFORMAT_ARGB8888

#define USE_LOCKED_TEXTURE	1
#define RENDER_SCALE_QUALITY	0

#define ROM_HACK_COLD_START
//#define ROM_HACK_NEW_ROM_SEARCHING

#endif
