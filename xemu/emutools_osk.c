/* Xemu - emulation (running on Linux/Unix/Windows/OSX, utilizing SDL2) of some
   8 bit machines, including the Commodore LCD and Commodore 65 and MEGA65 as well.
   Copyright (C)2016-2025 LGB (Gábor Lénárt) <lgblgblgb@gmail.com>

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

#ifdef	XEMU_OSK_SUPPORT

#include "xemu/emutools.h"
#include "xemu/emutools_osk.h"

#define SIZE_X 800
#define SIZE_Y 600

#define RETURN_IF_NO_OSK()		\
	if (!osk_in_use || !tex)	\
		return false;

struct osk_key_st {
	int x1,y1,x2,y2;
	const struct osk_desc_st *p;
};

bool osk_in_use = false;

static SDL_Texture *tex = NULL;
static Uint32 *osk = NULL;
static struct osk_key_st *keys;



// Must be called from the event handler
// Return value: true -> event handled by OSK, stop dealing with the event!
// false -> event not handled by OSK
bool osk_mouse_button_event ( void )
{
	RETURN_IF_NO_OSK();
}


// Same rules as for osk_mouse_button_event
// The purpose of this function is to provide visual feedback
bool osk_mouse_movement_event ( void )
{
	RETURN_IF_NO_OSK();
}


// Event handler should notify OSK to show a key pressed
bool osk_press_key ( void )
{
	RETURN_IF_NO_OSK();
}


bool osk_release_key ( void )
{
	RETURN_IF_NO_OSK();
}



bool osk_render ( void )
{
	RETURN_IF_NO_OSK();
	SDL_UpdateTexture(tex, NULL, osk, SIZE_X * 4);
	SDL_RenderCopy(sdl_ren, tex, NULL, NULL);
	return true;
}










static void draw_key ( const int i )
{
	for (int y = keys[i].y1; y <= keys[i].y2; y++) {
		for (int x = keys[i].x1; x <= keys[i].x2; x++) {
			if (x == keys[i].x1 || x == keys[i].x2 || y == keys[i].y1 || y == keys[i].y2)
				osk[y * SIZE_X + x] = 0xFFFFFFFFU;
			else
				osk[y * SIZE_X + x] = 0x808080FFU;

		}
	}
	const char *s = keys[i].p->key_str;
	const int l = strlen(s);
	int xo = (keys[i].x2 - keys[i].x1 - l * 8) / 2 + keys[i].x1;
	int yo = (keys[i].y2 - keys[i].y1 - 4) / 2 + keys[i].y1;
	while (*s) {
		const Uint8 *f = vga_font_8x8 + (*s) * 8;
		for (int y = yo; y < yo + 8; y++) {
			Uint32 *p = osk + y * SIZE_X + xo;
			Uint8 c = *(f++);
			for (int x = 0; x < 8; x++, c <<= 1, p++)
				if (c & 0x80)
					*p = 0xFFFFFFFFU;
		}
		s++;
		xo += 8;
	}
}


static void calc_osk ( const struct osk_desc_st *const desc )
{
	// Traverse the map to work out the dimensions of the virtual keyboard
	int maxx_now = 0, maxx = 0, maxy = 0, all_keys = 0;
	for (const struct osk_desc_st *p = desc; p->key_str; p++, all_keys++) {
		if ((p->flags & OSK_DESC_NEW_LINE)) {
			maxy++;
			maxx_now = 0;
		}
		maxx_now += p->gap_before + p->key_len;
		if (maxx_now > maxx)
			maxx = maxx_now;
	}
	// We can calculate the scaling factor now
	const float factorx = (float)SIZE_X / (float)maxx;
	const float factory = desc[0].key_len * factorx;
	const int starty = SIZE_Y - (maxy + 1) * factory - 0;
	DEBUGPRINT("OSK: v-keyboard dimension: %dx%d, %d keys, unit size = %fx%fpx" NL, maxx, maxy, all_keys, factorx, factory);
	// Traverse the stuff again, now generating actual texture coordinates for buttons
	keys = xemu_malloc(sizeof(struct osk_key_st) * (all_keys + 2));	// +2 -> reserve space for MENU and HIDE buttons
	for (int i = 0, x = 0, y = 0; i < all_keys; i++) {
		if ((desc[i].flags & OSK_DESC_NEW_LINE)) {
			y++;
			x = 0;
		}
		x += desc[i].gap_before;
		int x1 = x * factorx;
		x += desc[i].key_len;
		int x2 = (x * factorx) - 1;
		int y1 = starty + y * factory;
		int y2 = y1 + factory - 1;
		DEBUGPRINT("OSK: key \"%s\" at (%d;%d)-(%d;%d)" NL, desc[i].key_str, x1, y1, x2, y2);
		keys[i].p = &desc[i];
		keys[i].x1 = x1;
		keys[i].x2 = x2;
		keys[i].y1 = y1;
		keys[i].y2 = y2;
		draw_key(i);
	}
	static const struct osk_desc_st spec_desc[2] = {
		{
			.key_str = "HIDE",
			.key_id	= -1
		}, {
			.key_str = "MENU",
			.key_id	= -2
		}
	};
	static const struct osk_key_st spec_keys[2] = {
		{
			.p	= &spec_desc[0],
			.x2	= 48,
			.y2	= 32,
		}, {
			.p	= &spec_desc[1],
			.x1	= SIZE_X - 48,
			.y1	= 0,
			.x2	= SIZE_X - 1,
			.y2	= 32
		}
	};
	memcpy(keys + all_keys, spec_keys, sizeof(spec_keys));
	draw_key(all_keys++);
	draw_key(all_keys++);
}


bool osk_init ( const struct osk_desc_st *desc )
{
	if (tex)
		FATAL("You cannot call %s() twice!", __func__);
	osk = malloc(SIZE_X * SIZE_Y * 4);
	if (!osk) {
		ERROR_WINDOW("Not enough memory for OSK");
		goto error;
	}
	tex = SDL_CreateTexture(sdl_ren, sdl_pixel_format_id, SDL_TEXTUREACCESS_STREAMING, SIZE_X, SIZE_Y);
	if (!tex) {
		ERROR_WINDOW("Cannot create texture for OSK: %s", SDL_GetError());
		goto error;
	}
	if (SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND)) {
		ERROR_WINDOW("Cannot set alpha blend mode for OSK: %s", SDL_GetError());
		goto error;
	}
	DEBUGPRINT("OSK: ready, %dx%d pixel texture" NL, SIZE_X, SIZE_Y);
	memset(osk, 0, SIZE_X * SIZE_Y);
	calc_osk(desc);
	//const float factor = (float)SIZE_X / (float)osk_x;
	//xemu_malloc(sizeof(struct osk_key_st) * all_keys);

#if 0
	for (int y = 0; y < SIZE_Y; y++)
		for (int x = 0; x < SIZE_X; x++)
			if (y == (int)(SIZE_Y * .25) || y == (int)(SIZE_Y * .75) || x == (int)(SIZE_X * .25) || x == (int)(SIZE_X * .75))
				osk[y * SIZE_X + x] = 0xFFFFFFFFU;
#endif
	return true;
error:
	if (tex)
		SDL_DestroyTexture(tex);
	free(osk);
	osk = NULL;
	tex = NULL;
	return false;
}

#endif
