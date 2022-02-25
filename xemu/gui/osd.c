/* Xemu - emulation (running on Linux/Unix/Windows/OSX, utilizing
   SDL2) of some 8 bit machines, including the Commodore LCD and Commodore 65
   and MEGA65 as well.
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

#include "xemu/osd_font_16x16.c"


int osd_status = 0;
int osd_notifications_enabled = 1;
void (*osd_update_callback)(void) = NULL;


static struct {
	int	enabled, available;
	int	xsize, ysize;
	int	fade_dec, fade_end, alpha_last;
	Uint32	colours[16];
	Uint32 *pixels;
	Uint32	colour_fg;
	Uint32	colour_bg;
	SDL_Texture *tex;
} osd = {
	.enabled	= 0,
	.available	= 0,
	.pixels		= NULL,
	.tex		= NULL
};


static XEMU_INLINE void _osd_render ( void )
{
	if (osd_status) {
		if (osd_update_callback)
			osd_update_callback();
		if (osd_status < OSD_STATIC)
			osd_status -= osd.fade_dec;
		if (osd_status <= osd.fade_end) {
			DEBUG("OSD: end of fade at %d" NL, osd_status);
			osd_status = 0;
			osd.alpha_last = 0;
		} else {
			int alpha = osd_status > 0xFF ? 0xFF : osd_status;
			if (alpha != osd.alpha_last) {
				osd.alpha_last = alpha;
				SDL_SetTextureAlphaMod(osd.tex, alpha);
			}
			SDL_RenderCopy(sdl_ren, osd.tex, NULL, NULL);
		}
	}
}


void osd_clear_with_colour ( const int index )
{
	if (osd.enabled) {
		DEBUG("OSD: osd_clear_with_colour() called." NL);
		for (int i = 0; i < osd.xsize * osd.ysize; i++)
			osd.pixels[i] = osd.colours[index];
	}
}


void osd_clear ( void )
{
	if (osd.enabled) {
		DEBUG("OSD: osd_clear() called." NL);
		memset(osd.pixels, 0, osd.xsize * osd.ysize * 4);
	}
}


void osd_texture_update ( const SDL_Rect *rect )
{
	if (osd.enabled) {
		DEBUG("OSD: %s() called." NL, __func__);
		SDL_UpdateTexture(osd.tex, rect, rect ? osd.pixels + rect->y * osd.xsize + rect->x : osd.pixels, osd.xsize * sizeof (Uint32));
	}
}


int osd_init ( int xsize, int ysize, const Uint8 *palette, int palette_entries, int fade_dec, int fade_end )
{
	// start with disabled state, so we can abort our init process without need to disable this
	osd_status = 0;
	osd.enabled = 0;
	if (osd.tex || osd.pixels)
		FATAL("Calling osd_init() multiple times?");
	osd.tex = SDL_CreateTexture(sdl_ren, sdl_pix_fmt->format, SDL_TEXTUREACCESS_STREAMING, xsize, ysize);
	if (!osd.tex) {
		ERROR_WINDOW("Error with SDL_CreateTexture(), OSD won't be available: %s", SDL_GetError());
		return 1;
	}
	if (SDL_SetTextureBlendMode(osd.tex, SDL_BLENDMODE_BLEND)) {
		ERROR_WINDOW("Error with SDL_SetTextureBlendMode(), OSD won't be available: %s", SDL_GetError());
		SDL_DestroyTexture(osd.tex);
		osd.tex = NULL;
		return 1;
	}
	osd.pixels = xemu_malloc_ALIGNED(xsize * ysize * 4);
	osd.xsize = xsize;
	osd.ysize = ysize;
	osd.fade_dec = fade_dec;
	osd.fade_end = fade_end;
	for (int a = 0; a < palette_entries; a++)
		osd.colours[a] = SDL_MapRGBA(sdl_pix_fmt, palette[a << 2], palette[(a << 2) + 1], palette[(a << 2) + 2], palette[(a << 2) + 3]);
	osd.enabled = 1;	// great, everything is OK, we can set enabled state!
	osd.available = 1;
	osd_clear();
	osd_texture_update(NULL);
	osd_set_colours(1, 0);
	DEBUG("OSD: init: %dx%d pixels, %d palette entries, %d fade_dec, %d fade_end" NL, xsize, ysize, palette_entries, fade_dec, fade_end);
	return 0;
}


int osd_init_with_defaults ( void )
{
	static const Uint8 palette[] = {
		0xC0, 0x40, 0x40, 0xFF,		// normal background for OSD text
		0xFF, 0xFF, 0x00, 0xFF,		// normal foreground for OSD text
		0x00, 0x00, 0x00, 0x80,		// "matrix-mode" like use-case, background
		0x00, 0xFF, 0x00, 0xFF		// "matrix-mode" like use-case, foreground
	};
	return osd_init(
		OSD_TEXTURE_X_SIZE, OSD_TEXTURE_Y_SIZE,
		palette,
		sizeof(palette) >> 2,
		OSD_FADE_DEC_VAL,
		OSD_FADE_END_VAL
	);
}


void osd_on ( int value )
{
	if (osd.enabled) {
		osd.alpha_last = 0;	// force alphamod to set on next screen update
		osd_status = value;
		DEBUG("OSD: osd_on(%d) called." NL, value);
	}
}


void osd_off ( void )
{
	osd_status = 0;
	DEBUG("OSD: osd_off() called." NL);
}


void osd_global_enable ( int status )
{
	osd.enabled = (status && osd.available);
	osd.alpha_last = -1;
	osd_status = 0;
	DEBUG("OSD: osd_global_enable(%d), result of status = %d" NL, status, osd.enabled);
}


void osd_set_colours ( int fg_index, int bg_index )
{
	osd.colour_fg = osd.colours[fg_index];
	osd.colour_bg = osd.colours[bg_index];
	DEBUG("OSD: osd_set_colours(%d,%d) called." NL, fg_index, bg_index);
}


void osd_write_char ( int x, int y, char ch )
{
	int row;
	const Uint16 *s;
	int warn = 1;
	Uint32 *d = osd.pixels + y * osd.xsize + x;
	Uint32 *e = osd.pixels + osd.xsize * osd.ysize;
	if ((signed char)ch < 32)	// also for >127 chars, since they're negative in 2-complements 8 bit type
		ch = '?';
	s = font_16x16 + (((unsigned char)ch - 32) << 4);
	for (row = 0; row < 16; row++) {
		Uint16 mask = 0x8000;
		do {
			if (XEMU_LIKELY(d >= osd.pixels && d < e))
				*d = *s & mask ? osd.colour_fg : osd.colour_bg;
			else if (warn) {
				warn = 0;
				DEBUG("OSD: ERROR: out of OSD dimensions for char %c at starting point %d:%d" NL, ch, x, y);
			}
			d++;
			mask >>= 1;
		} while (mask);
		s++;
		d += osd.xsize - 16;
	}
}


void osd_write_string ( int x, int y, const char *s )
{
	if (y < 0)	// negative y: standard place for misc. notifications
		y = osd.ysize / 2;
	for (;;) {
		int len = 0, xt;
		if (!*s)
			break;
		while (s[len] && s[len] != '\n')
			len++;
		xt = (x < 0) ? ((osd.xsize - len * 16) / 2) : x;	// request for centered? (if x < 0)
		while (len--) {
			osd_write_char(xt, y, *s);
			s++;
			xt += 16;
		}
		y += 16;
		if (*s == '\n')
			s++;
	}
}


void osd_hijack ( void(*updater)(void), int *xsize_ptr, int *ysize_ptr, Uint32 **pixel_ptr )
{
	if (updater) {
		osd_notifications_enabled = 0;	// disable OSD notification as it would cause any notify event would mess up the matrix mode
		osd_update_callback = updater;
		osd_clear();
		osd_on(OSD_STATIC);
		if (xsize_ptr)
			*xsize_ptr = osd.xsize;
		if (ysize_ptr)
			*ysize_ptr = osd.ysize;
		if (pixel_ptr)
			*pixel_ptr = osd.pixels;
	} else {
		osd_update_callback = NULL;
		osd_clear();
		osd_off();
		osd_set_colours(1, 0);		// restore standard colours for notifications (maybe changed in updater)
		osd_notifications_enabled = 1;	// OK, now we can re-allow OSD notifications already
	}
}
