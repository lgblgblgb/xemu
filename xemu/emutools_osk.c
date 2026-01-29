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
#include "xemu/emutools_hid.h"
#include <string.h>

// Do not change these, identifiers
#define OSK_KEY_HIDE	-100
#define OSK_KEY_MENU	-101

// Release key after this amount of milliseconds (unless it's a toogle key)
#define AUTO_RELEASE_MS	50
// Zoom factor (>=1 integer) of the current key shown. Set to zero, to disable this feature
#define ZOOM_FACTOR	3

// Colours (R,G,B,A) - 'A' being the alpha channel
#define	COLOUR_OUTLINE	0xFF, 0xFF, 0xFF, 0xFF
#define COLOUR_TEXT	0xFF, 0xFF, 0xFF, 0xFF
#define COLOUR_BG	0xFF, 0xFF, 0xFF, 0x80
#define COLOUR_BGHOVER	0xFF, 0x00, 0x00, 0xFF
#define COLOUR_BGPRESS	0x00, 0xFF, 0x00, 0xFF
#define COLOUR_BGLOCKED	0x00, 0x00, 0xFF, 0xFF

#define RETURN_IF_NO_OSK()	\
	if (!in_use || !tex)	\
		return false;

struct osk_key_st {
	int x1,y1,x2,y2;
	const struct osk_desc_st *p;
	bool pressed;
};

static int size_x, size_y, kheight;
static bool in_use = false;
static bool mod_use = false;
static SDL_Texture *tex = NULL;
static Uint32 *osk = NULL;
static struct osk_key_st *keys;
static struct {	Uint32 outline, text, bg, bghover, bgpress, bglocked; } colour;
static int all_keys = 0;
static int hovered = -1;
static int last_pressed = -1;
static Uint32 press_time;
static struct { int x1, y1, x2, y2; } last_zoomed;



static void draw_text ( const char *s, int len, int xo, int yo, int scale, bool clear_too )
{
	while (len > 0) {
		const Uint8 *f = vga_font_8x8 + (*s) * 8;
		for (int y = 0; y < 8; y++, f++)
			for (int ysc = 0; ysc < scale; ysc++) {
				Uint32 *p = osk + (y * scale + ysc + yo) * size_x + xo;
				Uint8 c = *f;
				for (int x = 0; x < 8; x++, c <<= 1)
					for (int xsc = 0; xsc < scale; xsc++, p++)
						if (c & 0x80)
							*p = colour.text;
						else if (clear_too)
							*p = colour.bg;
			}
		s++;
		len--;
		xo += 8 * scale;
	}
}


static void fill_rectangle ( const int x1, const int y1, const int x2, const int y2, const Uint32 sdl_colour )
{
	for (int y = y1; y <= y2; y++) {
		Uint32 *p = osk + size_x * y + x1;
		for (int x = x1; x <= x2; x++)
			*p++ = sdl_colour;
	}
}


static void draw_key ( const int i, const bool active )
{
	if (i < 0)
		return;
	Uint32 bgcolour = colour.bg;
	if (active)
		bgcolour = colour.bghover;
	else if ((keys[i].p->flags & OSK_DESC_TOGGLE) && keys[i].pressed)
		bgcolour = colour.bglocked;
	for (int y = keys[i].y1; y <= keys[i].y2; y++)
		for (int x = keys[i].x1; x <= keys[i].x2; x++)
			osk[y * size_x + x] = (x == keys[i].x1 || x == keys[i].x2 || y == keys[i].y1 || y == keys[i].y2) ? colour.outline : bgcolour;
	const char *s = keys[i].p->key_str;
	const char *s2 = strchr(s, '\n');
	int l;
	if (mod_use) {
		if (s2)
			s = s2 + 1;
		l = strlen(s);
	} else {
		l = s2 ? s2 - s : strlen(s);
	}
	draw_text(
		s, l,
		(keys[i].x2 - keys[i].x1 - l * 8) / 2 + keys[i].x1,
		(keys[i].y2 - keys[i].y1 - 4    ) / 2 + keys[i].y1,
		1, false
	);
	// Clearing/showing a "zoomed" version of the current key
	if (ZOOM_FACTOR > 0) {
		if (last_zoomed.x1 >= 0) {
			fill_rectangle(last_zoomed.x1, last_zoomed.y1, last_zoomed.x2, last_zoomed.y2, 0);
			last_zoomed.x1 = -1;
		}
		if (active) {
			// Also add 4 pix "border" (y will be 4 where text is positioned, etc)
			last_zoomed.x1 = (size_x - l * ZOOM_FACTOR * 8 - 2*4) / 2;
			last_zoomed.y1 = 0;
			last_zoomed.x2 = last_zoomed.x1 + l * ZOOM_FACTOR * 8 + 4;
			last_zoomed.y2 = ZOOM_FACTOR * 8 + 8;
			if (last_zoomed.x1 >= 0) {
				fill_rectangle(last_zoomed.x1, last_zoomed.y1, last_zoomed.x2, last_zoomed.y2, colour.bg);
				draw_text(s, l, last_zoomed.x1 + 4, last_zoomed.y1 + 4, ZOOM_FACTOR, false);
			}
		}
	}
}


static void redraw_kbd ( void )
{
	memset(osk, 0, size_x * size_y);
	for (int k = 0; k < all_keys; k++)
		draw_key(k, false);
}


static int get_key_at_mouse ( int x, int y )
{
	// Convert coordinates
	x = size_x * x / sdl_default_win_x_size;
	y = size_y * y / sdl_default_win_y_size;
	// Search for matching button - if any
	for (int i = 0; i < all_keys; i++)
		if (x > keys[i].x1 && x < keys[i].x2 && y > keys[i].y1 && y < keys[i].y2)
			return i;
	return -1;
}


bool osk_mouse_movement_event ( const int x, const int y )
{
	RETURN_IF_NO_OSK();
#if 0
	char debug[100];
	sprintf(debug, "Mouse: %04d %04d", x, y);
	draw_text(debug, strlen(debug), 0, 40, 2, true);
#endif
	const int k = get_key_at_mouse(x, y);
	if (k != hovered) {
		draw_key(hovered, false);
		hovered = k;
		draw_key(hovered, true);
	}
	return k >= 0;
}


bool osk_key_activation_event ( const int x, const int y )
{
	RETURN_IF_NO_OSK();
	const int k = get_key_at_mouse(x, y);
	if (k < 0)
		return false;
	const int key_id = keys[k].p->key_id;
	if (key_id >= 0) {
		// Release previous key, OSK only supports one key at a time (other than the toggle/mod keys)
		if (last_pressed >= 0) {
			hid_sdl_synth_key_event(keys[last_pressed].p->key_id, 0);
			keys[last_pressed].pressed = false;
			draw_key(last_pressed, false);
			last_pressed = -1;
		}
		if (!(keys[k].p->flags & OSK_DESC_TOGGLE)) {	// the condition is here, because we don't want toggle keys to auto-release
			press_time = SDL_GetTicks();
			last_pressed = k;
		}
		if ((keys[k].p->flags & OSK_DESC_TOGGLE) && keys[k].pressed) {
			hid_sdl_synth_key_event(key_id, 0);
			keys[k].pressed = false;
		} else {
			hid_sdl_synth_key_event(key_id, 1);
			keys[k].pressed = true;
		}
		bool mod_status = false;
		for (int i = 0; i < all_keys; i++)
			if ((keys[i].p->flags & OSK_DESC_MOD_KEY) && keys[i].pressed) {
				mod_status = true;
				break;
			}
		if (mod_status != mod_use) {
			mod_use = mod_status;
			redraw_kbd();
		}
	} else if (key_id == OSK_KEY_HIDE) {
		osk_show(false);
	} else if (key_id == OSK_KEY_MENU) {
		// Hide OSK, OSK+OSD menu at the same time would be a mess
		osk_show(false);
		// Synthetise fake events (mouse) to enter the menu via right click
		hid_sdl_synth_mouse_button_click(SDL_BUTTON_RIGHT);
	}
	return true;
}


bool osk_status ( void )
{
	return in_use;
}


bool osk_show ( const bool on )
{
	if (!tex || on == in_use)
		return in_use;
	// Make sure to unpress previously pressed keys
	for (int k = 0; k < all_keys; k++)
		if (keys[k].pressed || k == last_pressed) {
			hid_sdl_synth_key_event(keys[k].p->key_id, 0);
			keys[k].pressed = false;
		}
	last_pressed = -1;
	in_use = on;
	hid_ignore_mouse_lbutton = on;
	mod_use = false;
	return in_use;
}


// No need to call manually, emutools.c will do it, if XEMU_OSK_SUPPORT is enabled globally
bool osk_render ( void )
{
	RETURN_IF_NO_OSK();
	// if a key was registered to be pressed, release after a while
	if (last_pressed >= 0 && SDL_GetTicks() - press_time >= AUTO_RELEASE_MS) {
		hid_sdl_synth_key_event(keys[last_pressed].p->key_id, 0);
		keys[last_pressed].pressed = false;
		draw_key(last_pressed, false);	// so pressed key won't remain highlighted forever
		last_pressed = -1;
	}
	SDL_UpdateTexture(tex, NULL, osk, size_x * 4);
	SDL_RenderCopy(sdl_ren, tex, NULL, NULL);
	return true;
}


static void calc_osk ( const struct osk_desc_st *const desc )
{
	// Traverse the map to work out the dimensions of the virtual keyboard
	int maxx_now = 0, maxx = 0, maxy = 0;
	all_keys = 0;
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
	const float factorx = (float)size_x / (float)maxx;
	//const float factory = desc[0].key_len * factorx;
	const int starty = size_y - (maxy + 1) * kheight - 0;
	DEBUGPRINT("OSK: v-keyboard dimension: %dx%d, %d keys, map scaling = %f, key height = %dpx" NL, maxx, maxy, all_keys, factorx, kheight);
	// Traverse the stuff again, now generating actual texture coordinates for buttons
	keys = xemu_malloc(sizeof(struct osk_key_st) * (all_keys + 2));	// +2 -> reserve space for MENU and HIDE buttons
	for (int i = 0, x = 0, y = 0; i < all_keys; i++) {
		if ((desc[i].flags & OSK_DESC_NEW_LINE)) {
			y++;
			x = 0;
		}
		x += desc[i].gap_before;
		const int x1 = x * factorx;
		x += desc[i].key_len;
		const int x2 = (x * factorx) - 1;
		const int y1 = starty + y * kheight;
		const int y2 = y1 + kheight - 1;
		//DEBUGPRINT("OSK: key \"%s\" at (%d;%d)-(%d;%d)" NL, desc[i].key_str, x1, y1, x2, y2);
		keys[i].p = &desc[i];
		keys[i].x1 = x1;
		keys[i].x2 = x2;
		keys[i].y1 = y1;
		keys[i].y2 = y2;
		keys[i].pressed = false;
	}
	static const struct osk_desc_st spec_desc[2] = {
		{
			.key_str = "MENU",
			.key_id	= OSK_KEY_MENU
		}, {
			.key_str = "HIDE",
			.key_id	= OSK_KEY_HIDE
		}
	};
	const struct osk_key_st spec_keys[2] = {
		{
			.p	= &spec_desc[0],
			.x2	= 48,
			.y2	= 32,
			.pressed= false
		}, {
			.p	= &spec_desc[1],
			.x1	= size_x - 48,
			.y1	= 0,
			.x2	= size_x - 1,
			.y2	= 32,
			.pressed= false
		}
	};
	memcpy(keys + all_keys, spec_keys, sizeof(spec_keys));
	all_keys += 2;
}


bool osk_init ( const struct osk_desc_st *desc, const int sx, const int sy, const int kh )
{
	if (tex)
		FATAL("You cannot call %s() twice!", __func__);
	size_x = sx;
	size_y = sy;
	kheight = kh;
	osk = malloc(size_x * size_y * 4);
	if (!osk) {
		ERROR_WINDOW("Not enough memory for OSK");
		goto error;
	}
	tex = SDL_CreateTexture(sdl_ren, sdl_pixel_format_id, SDL_TEXTUREACCESS_STREAMING, size_x, size_y);
	if (!tex) {
		ERROR_WINDOW("Cannot create texture for OSK: %s", SDL_GetError());
		goto error;
	}
	if (SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND)) {
		ERROR_WINDOW("Cannot set alpha blend mode for OSK: %s", SDL_GetError());
		goto error;
	}
	DEBUGPRINT("OSK: ready, %dx%d pixel texture" NL, size_x, size_y);
	colour.outline	= SDL_MapRGBA(sdl_pix_fmt, COLOUR_OUTLINE);
	colour.text	= SDL_MapRGBA(sdl_pix_fmt, COLOUR_TEXT);
	colour.bg	= SDL_MapRGBA(sdl_pix_fmt, COLOUR_BG);
	colour.bghover	= SDL_MapRGBA(sdl_pix_fmt, COLOUR_BGHOVER);
	colour.bgpress	= SDL_MapRGBA(sdl_pix_fmt, COLOUR_BGPRESS);
	colour.bglocked	= SDL_MapRGBA(sdl_pix_fmt, COLOUR_BGLOCKED);
	calc_osk(desc);
	last_zoomed.x1 = -1;
	redraw_kbd();
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
