/* A work-in-progess MEGA65 (Commodore-65 clone origins) emulator
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
#include "matrix_mode.h"

#include "xemu/cpu65.h"
#include "hypervisor.h"
#include "vic4.h"


int in_the_matrix = 0;


// TODO: many inventions here eventlually should be moved into some common place as
// probably other emulators ("generic OSD console API"), and OSD GUI want to use them as well!

// to get charset
#include "rom.h"

#define MATRIX(...) do { \
	char _buf_for_msg_[4096]; \
	CHECK_SNPRINTF(snprintf(_buf_for_msg_, sizeof _buf_for_msg_, __VA_ARGS__), sizeof _buf_for_msg_); \
	matrix_write_string(_buf_for_msg_); \
} while(0)



static const Uint8 console_colours[] = {
	0x00, 0x00, 0x00, 0x80,		// 0: for background shade of the console
	0x00, 0xFF, 0x00, 0xFF,		// 1: normal green colour of the console text
	0xFF, 0xFF, 0x00, 0xFF,		// 2: alternative yellow colour of the console text
	0x00, 0x00, 0x00, 0x00		// 3: totally transparent stuff
};
static Uint32 colour_mappings[16];
static Uint8 current_colour;

static int backend_xsize, backend_ysize;
static Uint32 *backend_pixels;
static int chrscreen_xsize, chrscreen_ysize;
static Uint8 *vmem = NULL;
static int current_x = 0, current_y = 0;
static int need_update = 0;
static int reserve_top_lines = 0;	// reserve this amount of top lines when scrolling



static void matrix_update ( void )
{
	if (!need_update)
		return;
	Uint8 *vp = vmem;
	need_update &= ~1;
	int updated = 0;
	for (int y = 0; y < chrscreen_ysize; y++)
		for (int x = 0; x < chrscreen_xsize; x++, vp += 2)
			if (need_update || (vp[1] & 0x80)) {
				updated++;
				const Uint8 *font = &vga_font_8x8[vp[0] << 3];
				vp[1] &= 0x7F;
				Uint32 *pix = backend_pixels + (y * 8) * backend_xsize + (x * 8);
				for (int line = 0; line < 8; line++, font++, pix += backend_xsize - 8)
					for (Uint8 bp = 0, data = *font; bp < 8; bp++, data <<= 1)
						*pix++ = colour_mappings[(vp[1] >> ((data & 0x80) ? 0 : 4)) & 0xF];
			}
	need_update = 0;
	if (updated) {
		osd_update();
		DEBUGPRINT("MATRIX: updated %d characters in the texture" NL, updated);
	}
}


static void write_char_raw ( const int x, const int y, const Uint8 ch, const Uint8 col )
{
	if (XEMU_UNLIKELY(x < 0 || y < 0 || x >= chrscreen_xsize || y >= chrscreen_ysize))
		return;
	Uint8 *v = vmem + (chrscreen_xsize * y + x) * 2;
	if (XEMU_LIKELY(v[0] != ch))
		v[0] = ch, v[1] = col | 0x80;
	else if (XEMU_UNLIKELY((v[1] ^ col) & 0x7F))
		v[1] = col | 0x80;
	need_update |= 1;
}


static void matrix_clear ( void )
{
	for (Uint8 *vp = vmem, *ve = vmem + chrscreen_xsize * chrscreen_ysize * 2; vp < ve; vp += 2)
		vp[0] = 0x20, vp[1] = current_colour;
	need_update |= 2;
}


static void matrix_scroll ( void )
{
	memmove(
		vmem + 2 * chrscreen_xsize *  reserve_top_lines,
		vmem + 2 * chrscreen_xsize * (reserve_top_lines + 1),
		chrscreen_xsize * (chrscreen_ysize - 1 - reserve_top_lines) * 2
	);
	for (Uint8 x = 0, *vp = vmem + (chrscreen_ysize - 1) * chrscreen_xsize * 2; x < chrscreen_xsize; x++, vp += 2)
		vp[0] = 0x20, vp[1] = current_colour;
	need_update |= 2;
}


static void matrix_write_char ( const Uint8 c )
{
	if (c == '\n') {
		current_x = 0;
		current_y++;
	} else {
		write_char_raw(current_x, current_y, c, current_colour);
		current_x++;
		if (current_x >= chrscreen_xsize) {
			current_x = 0;
			current_y++;
		}
	}
	if (current_y >= chrscreen_ysize) {
		current_y = chrscreen_ysize - 1;
		matrix_scroll();
	}
}


static inline void matrix_write_string ( const char *s )
{
	while (*s)
		matrix_write_char(*s++);
}


static void matrix_updater_callback ( void )
{
	if (!in_the_matrix)
		return;		// should not happen, but ...
	current_x = 0;
	current_y = 1;
	static const Uint8 io_mode_xlat[4] = {2, 3, 0, 4};
	MATRIX("PC:%04X A:%02X X:%02X Y:%02X Z:%02X SP:%04X B:%02X I/O=%d HYPER=%d",
		cpu65.pc, cpu65.a, cpu65.x, cpu65.y, cpu65.z,
		cpu65.sphi + cpu65.s, cpu65.bphi >> 8, io_mode_xlat[vic_iomode], !!in_hypervisor
	);
	matrix_update();
}


void matrix_mode_toggle ( int status )
{
	status = !!status;
	if (status == !!in_the_matrix)
		return;
	in_the_matrix = status;
	if (in_the_matrix) {
		osd_hijack(matrix_updater_callback, &backend_xsize, &backend_ysize, &backend_pixels);
		static int init_done = 0;
		if (!init_done) {
			init_done = 1;
			for (int i = 0; i < sizeof(console_colours) / 4; i++)
				colour_mappings[i] = SDL_MapRGBA(sdl_pix_fmt, console_colours[i * 4], console_colours[i * 4 + 1], console_colours[i * 4 + 2], console_colours[i * 4 + 3]);
			chrscreen_xsize = backend_xsize / 8;
			chrscreen_ysize = backend_ysize / 8;
			vmem = xemu_malloc(chrscreen_xsize * chrscreen_ysize * 2);
			current_colour = 1;
			matrix_clear();
			current_colour = 2;
			matrix_write_string("Stub-matrix mode only, for now ... press right-ALT + TAB to exit");
			current_colour = 1;
		}
		need_update = 2;
		matrix_update();
		DEBUGPRINT("MATRIX: ON (%dx%d pixels, %dx%d character resolution)" NL,
			backend_xsize, backend_ysize, chrscreen_xsize, chrscreen_ysize
		);
	} else {
		osd_hijack(NULL, NULL, NULL, NULL);
		DEBUGPRINT("MATRIX: OFF" NL);
	}
}
