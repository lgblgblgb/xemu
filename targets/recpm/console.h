/* Re-CP/M: CP/M-like own implementation + Z80 emulator
   Part of the Xemu project, please visit: https://github.com/lgblgblgb/xemu
   Copyright (C)2016-2019 LGB (Gábor Lénárt) <lgblgblgb@gmail.com>

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

#ifndef __XEMU_RECPM_CONSOLE_H_INCLUDED
#define __XEMU_RECPM_CONSOLE_H_INCLUDED

#define	FONT_HEIGHT		16

#define SCREEN_FORMAT		SDL_PIXELFORMAT_ARGB8888
#define USE_LOCKED_TEXTURE	1
#define RENDER_SCALE_QUALITY	2

#define conprintf(...) do {	\
	char _buf_for_msg_[4096];	\
	CHECK_SNPRINTF(snprintf(_buf_for_msg_, sizeof _buf_for_msg_, __VA_ARGS__), sizeof _buf_for_msg_);	\
	conputs(_buf_for_msg_);	\
} while (0)

extern void console_output ( Uint8 data );
extern void conputs ( const char *s );
extern int  console_status ( void );
extern int  console_input ( void );
extern void console_cursor_blink ( int delay );
extern void console_iteration ( void );
extern int  console_init ( int width, int height, int zoom_percent, int *map_to_ram, int baud_emu );

extern void recpm_shutdown_callback ( void );

#endif
