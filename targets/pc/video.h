/* Test-case for a primitive PC emulator inside the Xemu project,
   currently using Fake86's x86 CPU emulation.
   Part of the Xemu project, please visit: https://github.com/lgblgblgb/xemu
   Copyright (C)2016-2022 LGB (Gábor Lénárt) <lgblgblgb@gmail.com>

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

#ifndef XEMU_PC_VIDEO_H_INCLUDED
#define XEMU_PC_VIDEO_H_INCLUDED

extern uint32_t sdlpal[16];

extern uint8_t read_A0000 ( const uint16_t addr16 );
extern uint8_t read_B0000 ( const uint16_t addr16 );
extern void write_A0000 ( const uint16_t addr16, const uint8_t data );
extern void write_B0000 ( const uint16_t addr16, const uint8_t data );

extern void video_render_text_screen ( void );
extern void video_reset ( void );
extern void video_clear ( void );
extern void video_write_char ( const uint8_t c );
extern void video_write_string ( const char *s );

#endif
