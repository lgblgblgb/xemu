/* Minimalistic Enterprise-128 emulator with focus on "exotic" hardware
   Part of the Xemu project, please visit: https://github.com/lgblgblgb/xemu
   Copyright (C)2015-2017,2020 LGB (Gábor Lénárt) <lgblgblgb@gmail.com>

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
#include "xemu/emutools_files.h"
#include "enterprise128.h"
#include "nick.h"
#include "cpu.h"
#include "dave.h"


/*
 * The basic idea to speed emulation up: use the actual RGB
 * Uint32 value of EP colours at the sources (eg: setting
 * border colour, reading LPB, setting BIAS register) and use
 * those values instead of conversion all the time.
 */


static Uint16 lpt_a, lpt_set, ld1, ld2;
static int slot, visible, scanlines, max_scanlines;
static Uint8 *vram;
static Uint32 *pixels, *pixels_init, *pixels_limit_up, *pixels_limit_bottom, *pixels_limit_vsync_shortest, *pixels_limit_vsync_long_force;
static int pixels_gap;
static Uint32 palette[16] VARALIGN;
static Uint32 full_palette[256] VARALIGN;
static Uint32 *palette_bias = palette + 8;
static Uint32 border;
static Uint8 nick_last_byte;
static int reload, vres;
static int all_rasters;
static int lpt_clk;
int vsync;
static int frameskip;
static int lm, rm;
static int vm, cm;
static Uint8 col4trans[256 * 4] VARALIGN, col16trans[256 * 2] VARALIGN;
static int chs, msbalt, lsbalt;
static Uint8 balt_mask, chm, chb, altind;
Uint32 raster_time = 1;


#define RASTER_FIRST_VISIBLE 25
#define RASTER_LAST_VISIBLE   312
#define RASTER_NO_VSYNC_BEFORE 300
#define RASTER_FORCE_VSYNC 326


static int nick_addressing_init ( Uint32 *pixels_buffer, int line_size )
{
	if (line_size < 736) {
		ERROR_WINDOW("NICK: SDL: FATAL ERROR: target SDL surface has width (or pitch?) smaller than 736 pixels [%d]!", line_size);
		return 1;
	}
	if (line_size & 3) {
		ERROR_WINDOW("NICK: SDL: FATAL ERROR: line size bytes not 4 bytes aligned!");
		return 1;
	}
	DEBUG("NICK: first visible scanline = %d, last visible scanline = %d, line pitch pixels = %d" NL, RASTER_FIRST_VISIBLE, RASTER_LAST_VISIBLE, 0);
	pixels = pixels_init = (pixels_buffer - RASTER_FIRST_VISIBLE * line_size);
	pixels_limit_up = pixels_buffer;
	pixels_limit_bottom = pixels_init + RASTER_LAST_VISIBLE * line_size;
	pixels_limit_vsync_shortest = pixels_init + RASTER_NO_VSYNC_BEFORE * line_size;
	pixels_limit_vsync_long_force = pixels_init + RASTER_FORCE_VSYNC * line_size;
	pixels_gap = line_size - SCREEN_WIDTH;
	return 0;
}


// Ugly hack to access Xemu's framework for only non-locked texture :-O
extern Uint32 *sdl_pixel_buffer;

void screenshot ( void )
{
	if (!xemu_screenshot_png(
		"@", "screenshot.png",
		1,
		2,
		NULL,	// Allow function to figure it out ;)
		SCREEN_WIDTH,
		SCREEN_HEIGHT
	))
		OSD(-1, -1, "Screenshot has been taken");
}


int nick_init ( void )
{
	Uint32 *buf = sdl_pixel_buffer;
	pixels = NULL; // no previous state of buffer before the next function
	if (nick_addressing_init(buf, SCREEN_WIDTH))
		return 1;
	for (int a = 0; a < 256; a++) {
		// RGB colours for the target screen
		int r, g, b;
		r = (((a << 2) & 4) | ((a >> 2) & 2) | ((a >> 6) & 1)) * 255 / 7;
		g = (((a << 1) & 4) | ((a >> 3) & 2) | ((a >> 7) & 1)) * 255 / 7;
		b = (                 ((a >> 1) & 2) | ((a >> 5) & 1)) * 255 / 3;
		full_palette[a] = SDL_MapRGBA(sdl_pix_fmt, r, g, b, 0xFF);
		//full_palette[a] = (0xFF << 24) | (r << 16) | (g << 8) | b;
		//DEBUG("PAL#%d = (%d,%d,%d) = %d" NL, a, r, g, b, full_palette[a]);
		// this is translation table for  4 colour modes
		col4trans[a * 4 + 0] = ((a >> 2) & 2) | ((a >> 7) & 1);
		col4trans[a * 4 + 1] = ((a >> 1) & 2) | ((a >> 6) & 1);
		col4trans[a * 4 + 2] = ((a     ) & 2) | ((a >> 5) & 1);
		col4trans[a * 4 + 3] = ((a << 1) & 2) | ((a >> 4) & 1);
		// this is translation table for 16 colour modes
		col16trans[a * 2 + 0] = ((a << 2) & 8) | ((a >> 3) & 4) | ((a >> 2) & 2) | ((a >> 7) & 1);
		col16trans[a * 2 + 1] = ((a << 3) & 8) | ((a >> 2) & 4) | ((a >> 1) & 2) | ((a >> 6) & 1);

	}
	nick_set_bias(ports[0x80] = rand());
	nick_set_border(ports[0x81] = rand());
	nick_set_lptl(ports[0x82] = rand());
	nick_set_lpth(ports[0x83] = rand() | 128 | 64);
	nick_last_byte = 0xFF;
	lpt_a = lpt_set;
	slot = 0;
	frameskip = 0;
	all_rasters = 0;
	scanlines = 0;
	vsync = 0;
	vram = memory + 0x3F0000;
	DEBUG("NICK: initialized." NL);
	return 0;
}



Uint8 nick_get_last_byte ( void )
{
	return nick_last_byte;
}



void nick_set_border ( Uint8 bcol )
{
	border = full_palette[bcol];
}



void nick_set_bias ( Uint8 value )
{
	int a;
	value = (value & 31) << 3;
	// update the second half of the internal palette ("bias palette") based on the given BIAS value
	// the first half of the palette is updated by the Nick LPB read process
	for (a = 0; a < 8; a++)
		palette_bias[a] = full_palette[value++];
}



void nick_set_lptl ( Uint8 value )
{
	lpt_set = (lpt_set & 0xF000) | (value << 4);
}



void nick_set_lpth ( Uint8 value )
{
	DEBUG("NICK SET LPT-H!" NL);
	lpt_set = (lpt_set & 0x0FF0) | ((value & 0xF) << 12);
	DEBUG("NICK: LPT is set to %04Xh" NL, lpt_set);
	if (!(value & 128)) {
		lpt_a = lpt_set;
		slot = 0;
		pixels = pixels_init;
		scanlines = 0;
	}
	lpt_clk = value & 64;
}


#define NICK_READ(a) (nick_last_byte = vram[a])


static inline void FILL( Uint32 colour )
{
	if (visible) {
		int a;
		for(a = 0; a < 16; a++)
			*(pixels++) = colour;
	} else
		pixels += 16;
}



static inline void TODO(void) {
	FILL(full_palette[1]);
	DEBUG("NO VM = %d CM = %d" NL, vm, cm);
}



static void _render_border ( void )
{
	FILL(border);
}



static void _render_pixel_2 ( void )
{
	if (!visible) {
		ld1 += 2;
		pixels += 16;
	} else {
		int j;
		for (j = 0; j < 2; j ++) {	
			int a, ps;
			Uint8 data = NICK_READ(ld1++);
			if (msbalt && (data & 128)) {
				data &= 127;
				ps = 2;
			} else
				ps = 0;
			if (lsbalt && (data & 1)) {
				data &= 254;
				ps |= 4;
			}
			for (a = 128; a; a >>= 1) {
				*(pixels++) = palette[(data & a ? 1 : 0) | ps];
			}
		}
	}
}



static void _render_lpixel_2 ( void )
{
	if (!visible) {
		ld1 += 1;
		pixels += 16;
	} else {
		int a, ps;
		Uint8 data = NICK_READ(ld1++);
		if (msbalt && (data & 128)) {
			data &= 127;
			ps = 2;
		} else
			ps = 0;
		if (lsbalt && (data & 1)) {
			data &= 254;
			ps |= 4;
		}
		for (a = 128; a; a >>= 1) {
			pixels[0] = pixels[1] = palette[(data & a ? 1 : 0) | ps];
			pixels += 2;
		}
	}
}


static void _render_pixel_256 ( void )
{
	if (!visible) {
		ld1 += 2;
		pixels += 16;
	} else {
		int a;
		Uint32 colour = full_palette[NICK_READ(ld1++)];
		for (a = 0; a < 8; a++)
			*(pixels++) = colour;
		colour = full_palette[NICK_READ(ld1++)];
		for (a = 0; a < 8; a++)
			*(pixels++) = colour;
	}
}
static void _render_lpixel_256 ( void )
{
	if (!visible) {
		ld1++;
		pixels += 16;
	} else {
		int a;
		Uint8 colour = full_palette[NICK_READ(ld1++)];
		for (a = 0; a < 16; a++)
			*(pixels++) = colour;
	}
}



static void _render_vsync(void)
{
	FILL(full_palette[4]);
}



static Uint8 _altind_modes[] = {
	0,	// no altind1, no altind0 (00)	0+0
	4,	// no altind1, do altind0 (01)	0+4
	2,	// do altind1, no altind0 (10)	2+0
	6,	// do altind1, do altind0 (11)
};


static void _render_char_2 ( void )
{
	if (!visible) {
		ld1++;
	} else {
		Uint8 data = NICK_READ(ld1++);
		Uint32 c1 = _altind_modes[(altind & data) >> 6];
		Uint32 c2 = palette[c1 + 1];
		c1 = palette[c1];
		data = NICK_READ(ld2 | (data & chm));
		pixels[ 0] = pixels[ 1] = (data & 0x80) ? c2 : c1;
		pixels[ 2] = pixels[ 3] = (data & 0x40) ? c2 : c1;
		pixels[ 4] = pixels[ 5] = (data & 0x20) ? c2 : c1;
		pixels[ 6] = pixels[ 7] = (data & 0x10) ? c2 : c1;
		pixels[ 8] = pixels[ 9] = (data & 0x08) ? c2 : c1;
		pixels[10] = pixels[11] = (data & 0x04) ? c2 : c1;
		pixels[12] = pixels[13] = (data & 0x02) ? c2 : c1;
		pixels[14] = pixels[15] = (data & 0x01) ? c2 : c1;
	}
	pixels += 16;
}


static void _render_char_4 ( void )
{
	if (!visible) {
		ld1++;
	} else {
		Uint8 data = NICK_READ(ld1++);
		Uint32 col[2];
		col[0] = _altind_modes[(altind & data) >> 6];
		col[1] = palette[col[0] + 1];
		col[0] = palette[col[0]];
		data = NICK_READ(ld2 | (data & chm));
		Uint8 *trans = col4trans + (data << 2);
		pixels[ 0] = pixels[ 1] = pixels[ 2] = pixels[ 3] = trans[0] < 2 ? col[trans[0]] : palette[trans[0]];
		pixels[ 4] = pixels[ 5] = pixels[ 6] = pixels[ 7] = trans[1] < 2 ? col[trans[1]] : palette[trans[1]];
		pixels[ 8] = pixels[ 9] = pixels[10] = pixels[11] = trans[2] < 2 ? col[trans[2]] : palette[trans[2]];
		pixels[12] = pixels[13] = pixels[14] = pixels[15] = trans[3] < 2 ? col[trans[3]] : palette[trans[3]];
	}
	pixels += 16;
}



static void _render_invalid ( void )
{
	FILL(full_palette[3]);
}


static void _render_attrib_2 ( void ) 
{ 
	if (!visible) {
		ld1++;
		ld2++;
	} else {
		int data = NICK_READ(ld1++); // read attribute byte
		Uint32 c1 = palette[data & 0xF];
		Uint32 c2 = palette[data >> 4];
		data = NICK_READ(ld2++); // read graphic byte
		pixels[ 0] = pixels[ 1] = (data & 0x80) ? c1 : c2;
		pixels[ 2] = pixels[ 3] = (data & 0x40) ? c1 : c2;
		pixels[ 4] = pixels[ 5] = (data & 0x20) ? c1 : c2;
		pixels[ 6] = pixels[ 7] = (data & 0x10) ? c1 : c2;
		pixels[ 8] = pixels[ 9] = (data & 0x08) ? c1 : c2;
		pixels[10] = pixels[11] = (data & 0x04) ? c1 : c2;
		pixels[12] = pixels[13] = (data & 0x02) ? c1 : c2;
		pixels[14] = pixels[15] = (data & 0x01) ? c1 : c2;
	}
	pixels += 16;
}


static void _render_pixel_4 ( void )
{
	if (!visible) {
		ld1 += 2;
	} else {
		Uint8 *trans = col4trans + (NICK_READ(ld1++) << 2);
		pixels[ 0] = pixels[ 1] = palette[trans[0]];
		pixels[ 2] = pixels[ 3] = palette[trans[1]];
		pixels[ 4] = pixels[ 5] = palette[trans[2]];
		pixels[ 6] = pixels[ 7] = palette[trans[3]];
		trans = col4trans + (NICK_READ(ld1++) << 2);
		pixels[ 8] = pixels[ 9] = palette[trans[0]];
		pixels[10] = pixels[11] = palette[trans[1]];
		pixels[12] = pixels[13] = palette[trans[2]];
		pixels[14] = pixels[15] = palette[trans[3]];
	}
	pixels += 16;
}

static void _render_lpixel_4 ( void )
{
	if (!visible) {
		ld1 += 1;
	} else {
		Uint8 *trans = col4trans + (NICK_READ(ld1++) << 2);
		pixels[ 0] = pixels[ 1] = pixels[ 2] = pixels[ 3] = palette[trans[0]];
		pixels[ 4] = pixels[ 5] = pixels[ 6] = pixels[ 7] = palette[trans[1]];
		pixels[ 8] = pixels[ 9] = pixels[10] = pixels[11] = palette[trans[2]];
		pixels[12] = pixels[13] = pixels[14] = pixels[15] = palette[trans[3]];
	}
	pixels += 16;
}


static void _render_pixel_16 ( void ) // TODO
{
	if (!visible) {
		ld1 += 2;
	} else {
		Uint8 *trans = col16trans + (NICK_READ(ld1++) << 1);
		pixels[ 0] = pixels[ 1] = pixels[ 2] = pixels[ 3] = palette[trans[0]];
		pixels[ 4] = pixels[ 5] = pixels[ 6] = pixels[ 7] = palette[trans[1]];
		trans = col16trans + (NICK_READ(ld1++) << 1);
		pixels[ 8] = pixels[ 9] = pixels[10] = pixels[11] = palette[trans[0]];
		pixels[12] = pixels[13] = pixels[14] = pixels[15] = palette[trans[1]];
		
		
	}
	pixels += 16;
}

static void _render_lpixel_16 ( void ) // TODO
{
	if (!visible) {
		ld1 += 1;
	} else {
		Uint8 *trans = col16trans + (NICK_READ(ld1++) << 1);
		pixels[ 0] = pixels[ 1] = pixels[ 2] = pixels[ 3] =
		pixels[ 4] = pixels[ 5] = pixels[ 6] = pixels[ 7] = palette[trans[0]];
		pixels[ 8] = pixels[ 9] = pixels[10] = pixels[11] =
		pixels[12] = pixels[13] = pixels[14] = pixels[15] = palette[trans[1]];		
	}
	pixels += 16;
}

static void _render_char_16 ( void ) { TODO(); }
static void _render_char_256 ( void ) { TODO(); }





static void (*_render)(void);
static void (*render_modes[])(void) = {
//static const void (*render_modes)(void)[] = {
	_render_vsync,		// col-2 vsync
	_render_pixel_2,	// col-2 pixel
	_render_attrib_2,	// col-2 attrib
	_render_char_2,		// col-2 ch256
	_render_char_2,		// col-2 ch128
	_render_char_2,		// col-2 ch64
	_render_invalid,	// col-2 invalid
	_render_lpixel_2,	// col-2 lpixel
	_render_vsync,		// col-4 vsync
	_render_pixel_4,	// col-4 pixel
	_render_attrib_2,	// col-4 attrib  TODO
	_render_char_4,		// col-4 ch256
	_render_char_4,		// col-4 ch128
	_render_char_4,		// col-4 ch64
	_render_invalid,	// col-4 invalid
	_render_lpixel_4,	// col-4 lpixel
	_render_vsync,		// col-16 vsync
	_render_pixel_16,	// col-16 pixel
	_render_attrib_2,	// col-16 attrib   TODO
	_render_char_16,	// col-16 ch256
	_render_char_16,	// col-16 ch128
	_render_char_16,	// col-16 ch64
	_render_invalid,	// col-16 invalid
	_render_lpixel_16,	// col-16 lpixel
	_render_vsync,		// col-256 vsync
	_render_pixel_256,	// col-256 pixel
	_render_attrib_2,	// col-256 attrib  TODO
	_render_char_256,	// col-256 ch256
	_render_char_256,	// col-256 ch128
	_render_char_256,	// col-256 ch64
	_render_invalid,	// col-256 invalid
	_render_lpixel_256	// col-256 lpixel
};
//static const int chs_for_modes[] = { 0, 0, 0, 256, 128, 64, 0, 0 };
static const int chb_for_modes[] = { 0, 0, 0,   8,   7,  6, 0, 0 };





void nick_set_frameskip ( int val )
{
	frameskip = val;
}


static int frames = 0;
//static Uint32 omg = 0x8040C0;

static inline void _update ( void )
{
	/*int a;
	for(a=0;a<100;a++)
		pixels_limit_up[100*736+400+a]=omg++;*/
	emu_one_frame(all_rasters, frameskip);
	all_rasters = 0;
	pixels = pixels_init;
	frames++;
}


static const char *_vm_names[] = {"vsync", "pixel", "attrib", "ch256", "ch128", "ch64", "invalid", "lpixel"};
static const char *_cm_names[] = {"2c", "4c", "16c", "256c"};


/* Result should be free()'d by the caller then! */
char *nick_dump_lpt ( const char *newline_seq )
{
	int a = lpt_set;
	int scs = 0;
	char *p = NULL;
	char buffer[256];
	do {
		snprintf(buffer, sizeof buffer, "%04X SC=%3d VINT=%d CM=%d VRES=%d VM=%d RELOAD=%d LM=%2d RM=%2d LD1=%04X LD2=%04X %s/%s%s",
			a,
			256 - vram[a], // sc
			vram[a + 1] >> 7, // vint
			(vram[a + 1] >> 5) & 3, // cm
			(vram[a + 1] >> 4) & 1, // vres
			(vram[a + 1] >> 1) & 7, // vm
			vram[a + 1] & 1, // reload
			vram[a + 2] & 63, // LM
			vram[a + 3] & 63, // RM
			vram[a + 4] | (vram[a + 5] << 8), // LD1
			vram[a + 6] | (vram[a + 7] << 8), // LD2
			_vm_names[(vram[a + 1] >> 1) & 7],
			_cm_names[(vram[a + 1] >> 5) & 3],
			newline_seq
		);
		p = xemu_realloc(p, p ? strlen(p) + strlen(buffer) + 256 : strlen(buffer) + 256);
		if (a == lpt_set)
			*p = '\0';
		strcat(p, buffer);
		scs += 256 - vram[a];
		if (vram[a + 1] & 1) {
			sprintf(buffer, "Total scanlines = %d%s", scs, newline_seq);
			strcat(p, buffer);
			return p;
		}
		a = (a + 16) & 0xFFFF;
	} while (a != lpt_set);
	sprintf(buffer, "ERROR: LPT is endless!%s", newline_seq);
	strcat(p, buffer);
	return p;
}




void nick_render_slot ( void )
{
	register int a;
	switch (slot) {
		case 57:
			raster_time++;
			all_rasters++;
			scanlines++;
			if (scanlines >= max_scanlines) {
				scanlines = 0;
				if (!lpt_clk) // LPT clocking is inhibited?
					lpt_a -= 16; // stay at the current LPB if so
				else if (reload)	// if LPT clocking is OK (it is here) but reload LPT bit is set, then start from the beginning
					lpt_a = lpt_set;
				// if non of the above, the default is to continue read the LPT, which is the next LPB, as it should be
			} else
				lpt_a -= 16; // stay at the current LPB, it still applies for one or more scanlines
			pixels += pixels_gap;
			slot = 0; // NO break after this, control flows over "case 0". This is not a bug, but a feature! :)
		case 0:
			max_scanlines = 256 - NICK_READ(lpt_a++);
			a = NICK_READ(lpt_a++);
			dave_int1(a & 128);
			reload = a & 1;
			vres = a & 16;
			vm = (a >> 1) & 7;
			cm = (a >> 5) & 3;
			if (!vm) { // vsync mode is the actual video mode (vm=0)
				if (!vsync) { // if previous mode line was not vsync, this is start of the vsync!
					vsync = 1;
					if (pixels >= pixels_limit_vsync_shortest)
						_update();
				}
			} else
				vsync = 0;
			if (pixels >= pixels_limit_vsync_long_force)
				_update();
			visible = (pixels >= pixels_limit_up && pixels < pixels_limit_bottom && (!frameskip));
			if (XEMU_UNLIKELY((vm | ((a >> 2) & 0x18)) >= 8*4))
				FATAL("FATAL ERROR: NICK: render funcarray bound check failure!");
			_render = render_modes[vm | ((a >> 2) & 0x18)];
			break;
		case 1:
			a = NICK_READ(lpt_a++);
			lm = a & 63;
			lsbalt = a & 64;
			msbalt = a & 128;
			balt_mask = lsbalt ? 0xFE : 0xFF;
			if (msbalt) balt_mask &= 0x7F;
			a = NICK_READ(lpt_a++);
			rm = a & 63;
			//altind1 = a & 64;
			//altind0 = a & 128;
			altind = ((a & 128) >> 1) | ((a & 64) << 1);
			break;
		case 2:
			a  = NICK_READ(lpt_a++);
			a |= NICK_READ(lpt_a++) << 8;
			if ((!scanlines) || (!vres))
				ld1 = a;
			break;
		case 3:
			a  = NICK_READ(lpt_a++);
			a |= NICK_READ(lpt_a++) << 8;
			if (!scanlines) {
				chb = chb_for_modes[vm];
				if (chb) {
					ld2 = a << chb;
					chs = 1 << chb;
					chm = chs - 1;
				} else {
					ld2 = a;
					chs = 0;
				}
			} else
				ld2 += chs; // if chs==0 (non-ch mode) it won't make any difference anyway ...
			break;
		// these slots are used to read the palette related info from LPB
		// note: the high 8 colours of the palette is set by BIAS register, not by the LPB!
		case 4:
			palette[0] = full_palette[NICK_READ(lpt_a++)];
			palette[1] = full_palette[NICK_READ(lpt_a++)];
			break;
		case 5:
			palette[2] = full_palette[NICK_READ(lpt_a++)];
			palette[3] = full_palette[NICK_READ(lpt_a++)];
			break;
		case 6:
			palette[4] = full_palette[NICK_READ(lpt_a++)];
			palette[5] = full_palette[NICK_READ(lpt_a++)];
			break;
		case 7:
			palette[6] = full_palette[NICK_READ(lpt_a++)];
			palette[7] = full_palette[NICK_READ(lpt_a++)];
			break;
		case 54:
		case 55:
		case 56:
			// Nick does VRAM refresh here, and generates HSYNC, not so much needed in an emulator, though :)
			break;
		case  8: case  9: case 10: case 11: case 12: case 13: case 14: case 15: case 16: case 17: case 18: case 19: case 20: case 21: case 22: case 23: case 24: case 25: case 26: case 27: case 28:
		case 29: case 30: case 31: case 32: case 33: case 34: case 35: case 36: case 37: case 38: case 39: case 40: case 41: case 42: case 43: case 44: case 45: case 46: case 47: case 48: case 49:
		case 50: case 51: case 52: case 53:
			// TODO: real nick does not use complicated comparsion just disables border and enables again while hitting lm and rm ...
			if (slot < lm || slot >= rm) {
				if (vsync)
					_render_vsync();
				else
					_render_border();
			} else
				_render();
			break;
		default:
			FATAL("NICK: FATAL ERROR: invalid slot number for rendering: %d", slot);
			break;
	}
	slot++;
}
