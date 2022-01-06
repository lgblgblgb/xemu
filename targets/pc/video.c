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


#include "xemu/emutools.h"
#include "video.h"

uint32_t sdlpal[16];

static uint8_t segA000[0x10000];
static uint8_t segB000[0x10000];

static int current_x, current_y, current_colour;

#define TEXT_SCREEN_SOURCE (segB000 + 0x8000)

#define CHARACTER_SET_DEFINER_8X16 static const Uint8 vgafont[]
#include "xemu/vgafonts.c"

// Used by memory.c in memory decoders:

uint8_t read_A0000 ( const uint16_t addr16 )
{
	return segA000[addr16];
}

uint8_t read_B0000 ( const uint16_t addr16 )
{
	return segB000[addr16];
}

void write_A0000 ( const uint16_t addr16, const uint8_t data )
{
	segA000[addr16] = data;
}

void write_B0000 ( const uint16_t addr16, const uint8_t data )
{
	segB000[addr16] = data;
}


void video_reset ( void )
{
	video_clear();
}


void video_render_text_screen ( void )
{
	int tail;
	Uint32 *pix = xemu_start_pixel_buffer_access(&tail);
	Uint32 *pix_cursor = pix + current_x * 8 + current_y * 8 * 80 * 16;
	for (int y = 0; y < 25; y++) {
		for (int raster = 0; raster < 16; raster++) {
			uint8_t *vp = TEXT_SCREEN_SOURCE + y * 160;
			for (int x = 0; x < 80; x++, vp += 2) {
				for (uint8_t fbyte = vgafont[(vp[0] << 4) + raster], mask = 0x80, bg = vp[1] >> 4, fg = vp[1] & 0xF; mask; mask >>= 1)
					*pix++ = sdlpal[(fbyte & mask) ? fg : bg];
			}
		}
	}
	static uint32_t cursor_phase = 0;
	if (cursor_phase++ & 32)
		for (int y = 0; y < 16; y++) {
			for (int x = 0; x < 8; x++)
				*pix_cursor++ = 0xFFFFFFFFU;
			pix_cursor += 8 * 79;
		}
	xemu_update_screen();
}


void video_clear ( void )
{
	for (int i = 0; i < 80 * 25 * 2;)
		TEXT_SCREEN_SOURCE[i++] = 0x20, TEXT_SCREEN_SOURCE[i++] = 7;
	current_x = 0;
	current_y = 0;
	current_colour = 7;
}


static void video_scroll ( void )
{
	memmove(TEXT_SCREEN_SOURCE, TEXT_SCREEN_SOURCE + 160, 80 * 24 * 2);
	for (uint8_t *vp = TEXT_SCREEN_SOURCE + 24 * 160, *ve = TEXT_SCREEN_SOURCE + 25 * 160; vp < ve; vp += 2)
		vp[0] = 0x20, vp[1] = current_colour;
}


void video_write_char ( const uint8_t c )
{
	if (c == 8) {
		if (current_x > 0)
			current_x--;
	} else if (c == '\r') {
		current_x = 0;
	} else if (c == '\n') {
		current_y++;
	} else {
		uint8_t *vp = TEXT_SCREEN_SOURCE + current_y * 160 + current_x * 2;
		vp[0] = c;
		vp[1] = current_colour;
		if (current_x >= 79) {
			current_x = 0;
			current_y++;
		} else
			current_x++;
	}
	if (current_y >= 25) {
		current_y = 24;
		video_scroll();
	}
}


void video_write_string ( const char *s )
{
	while (*s)
		video_write_char(*s++);
}
