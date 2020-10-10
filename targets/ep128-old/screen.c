/* Xep128: Minimalistic Enterprise-128 emulator with focus on "exotic" hardware
   Copyright (C)2015,2016 LGB (Gábor Lénárt) <lgblgblgb@gmail.com>
   http://xep128.lgb.hu/

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

#include "xep128.h"
#define XEP128_NEED_SDL_WMINFO
#include "screen.h"
#include "dave.h"
#include "configuration.h"
#include "main.h"
#include "input.h"
#ifndef NO_SCREENSHOT
#ifdef CONFIG_USE_LODEPNG
#	include "xemu/lodepng.h"
#else
#	include "png.h"
#endif
#endif
#ifndef __EMSCRIPTEN__
#include "xemu/../rom/ep128/app_icon.c"
#endif
#include <SDL.h>


#include "xemu/osd_font_16x16.c"


int is_fullscreen = 0;
SDL_Window   *sdl_win = NULL;
SDL_SysWMinfo sdl_wminfo;
SDL_PixelFormat *sdl_pixel_format;
static SDL_Renderer *sdl_ren = NULL;
static SDL_Texture  *sdl_tex = NULL, *sdl_osdtex = NULL;
int warn_for_mouse_grab = 1;
Uint32 sdl_winid;
static int win_xsize, win_ysize, resize_counter = 0, win_size_changed = 0;
static int screenshot_index = 0;
static Uint32 *osd_pixels = NULL;
static int osd_on = 0, osd_fade = 0;
static Uint32 osd_fg_colour, osd_bg_colour;



static void _osd_set_alpha ( int alpha )
{
	if (alpha > 0xFF)
		alpha = 0xFF;
	SDL_SetTextureAlphaMod(sdl_osdtex, alpha);
}


void osd_disable ( void )
{
	osd_on = 0;
}


void osd_clear ( void )
{
	memset(osd_pixels, 0, SCREEN_WIDTH * SCREEN_HEIGHT * 4);
}


void osd_update ( void )
{
	if (osd_pixels)
		SDL_UpdateTexture(sdl_osdtex, NULL, osd_pixels, SCREEN_WIDTH * sizeof (Uint32));
}


void osd_set_colour ( int fg_r, int fg_g, int fg_b, int fg_a, int bg_r, int bg_g, int bg_b, int bg_a )
{
	osd_fg_colour = SDL_MapRGBA(sdl_pixel_format, fg_r, fg_g, fg_b, fg_a);
	osd_bg_colour = SDL_MapRGBA(sdl_pixel_format, bg_r, bg_g, bg_b, bg_a);
}


void osd_write_char ( int x, int y, char ch )
{
	int row;
	const Uint16 *s = font_16x16 + ((ch - 32) << 4);
	Uint32 *d = osd_pixels + y * SCREEN_WIDTH + x;
	for (row = 0; row < 16; row++) {
		Uint16 mask = 0x8000;
		do {
			*(d++) = *s & mask ? osd_fg_colour : osd_bg_colour;
			mask >>= 1;
		} while (mask);
		s++;
		d += SCREEN_WIDTH - 16;
	}
}


void osd_write_string ( int x, int y, const char *s )
{
	while (*s) {
		osd_write_char(x, y, *s);
		s++;
		x += 16;
	}
}


void osd_write_string_centered ( int y, const char *s )
{
	do {
		char *p = strchr(s, '\n');
		int n = p ? (p++) - s : strlen(s);
		int x = (SCREEN_WIDTH >> 1) - (n << 3);
		while (n) {
			osd_write_char(x, y, *(s++));
			x += 16;
			n--;
		}
		y += 24;
		s = p;
	} while (s);
}


void osd_notification ( const char *s )
{
	osd_clear();
	osd_write_string_centered(100, s);
	osd_update();
	osd_on = 1;
	osd_fade = OSD_FADE_START;
}


void osd_replay ( int fade )
{
	osd_on = 1;
	_osd_set_alpha(0xFF);
	osd_write_string_centered(70, "*** REPLAY ***");
	osd_update();
	osd_fade = fade;
}


void screen_grab ( SDL_bool state )
{
	if (warn_for_mouse_grab) {
		//INFO_WINDOW("Clicking in emulator window causes to enter BoxSoft mouse emulation mode.\nThis will try to grab your mouse pointer. To exit, press key ESC.\nYou won't get this notice next time within this session of Xep128");
		warn_for_mouse_grab = 0;
	} /*else*/ if (state == SDL_TRUE)
		OSD("Mouse grab\nPress ESC to leave.");
	DEBUG("UI: GRAB: %d" NL, state);
	SDL_SetRelativeMouseMode(state);
	SDL_SetWindowGrab(sdl_win, state);
}


#define SCREEN_RATIO ((double)SCREEN_WIDTH / (double)(SCREEN_HEIGHT * 2))


void screen_window_resized ( int new_xsize, int new_ysize )
{
	float ratio;
	if (is_fullscreen)
		return;
	if (new_xsize == win_xsize && new_ysize == win_ysize)
		return;
	DEBUG("UI: window geometry change from %d x %d to %d x %d" NL, win_xsize, win_ysize, new_xsize, new_ysize);
	if (new_ysize == 0) new_ysize = 1;
	ratio = (float)new_xsize / (float)new_ysize;
	if (new_xsize * new_ysize > win_xsize * win_ysize) {	// probably user means making window larger (area of window increased)
		if (ratio > SCREEN_RATIO)
			new_ysize = new_xsize / SCREEN_RATIO;
		else
			new_xsize = new_ysize * SCREEN_RATIO;
	} else {						// probably user means making window smaller (area of window decreased)
		if (ratio > SCREEN_RATIO)
			new_xsize = new_ysize * SCREEN_RATIO;
		else
			new_ysize = new_xsize / SCREEN_RATIO;
	}
	if (new_xsize < SCREEN_WIDTH || new_ysize < SCREEN_HEIGHT * 2) {
		win_xsize = SCREEN_WIDTH;
		win_ysize = SCREEN_HEIGHT * 2;
	} else {
		win_xsize = new_xsize;
		win_ysize = new_ysize;
	}
	win_size_changed = 1;
}


void screen_set_fullscreen ( int state )
{
	if (is_fullscreen == state) return;
	is_fullscreen = state;
	if (state) {
		SDL_GetWindowSize(sdl_win, &win_xsize, &win_ysize); // save window size, it seems there are some problems with leaving fullscreen then
		if (SDL_SetWindowFullscreen(sdl_win, SDL_WINDOW_FULLSCREEN_DESKTOP)) {
			ERROR_WINDOW("Cannot enter fullscreen: %s", SDL_GetError());
			is_fullscreen = 0;
		} else {
			DEBUG("UI: entering full screen mode" NL);
			OSD("Full screen");
		}
		SDL_RaiseWindow(sdl_win);
	} else {
		SDL_SetWindowFullscreen(sdl_win, 0);
		SDL_SetWindowSize(sdl_win, win_xsize, win_ysize); // restore window size saved on entering fullscreen, there can be some bugs ...
		SDL_RaiseWindow(sdl_win); // also I had problems with having the window behind other windows on exit fullscreen. Make sure to raise the window
		DEBUG("UI: leaving full screen mode" NL);
	}
}


void screen_present_frame (Uint32 *ep_pixels)
{
	int need = (osd_on && sdl_osdtex != NULL) || !paused;
	if (resize_counter == 10) {
		if (win_size_changed) {
			SDL_SetWindowSize(sdl_win, win_xsize, win_ysize);
			DEBUG("UI: correcting window size to %d x %d" NL, win_xsize, win_ysize);
			win_size_changed = 0;
			need = 1;
		}
		resize_counter = 0;
	} else
		resize_counter++;
	if (!need)
		return; // Ugly paused hack ... do not consume CPU for SDL screen update when no OSD and emu is paused
	SDL_UpdateTexture(sdl_tex, NULL, ep_pixels, SCREEN_WIDTH * sizeof (Uint32));
	SDL_RenderClear(sdl_ren);
	SDL_RenderCopy(sdl_ren, sdl_tex, NULL, NULL);
	if (osd_on && sdl_osdtex != NULL)
		SDL_RenderCopy(sdl_ren, sdl_osdtex, NULL, NULL);
	else
		osd_fade = 0;
	SDL_RenderPresent(sdl_ren);
	if (osd_fade > OSD_FADE_STOP) {	// OSD / fade mode
		osd_fade -= OSD_FADE_DEC;
		if (osd_fade > OSD_FADE_STOP) {
			_osd_set_alpha(osd_fade);
		} else {
			osd_on = 0; // faded off, switch OSD texture rendering off
			DEBUG("OSD: turned off on fading out" NL);
		}
	}
}


// TODO: use libpng in Linux, smaller binary (on windows I wouldn't introduce another DLL dependency though ...)
int screen_shot ( Uint32 *ep_pixels, const char *directory, const char *filename )
{
#ifdef NO_SCREENSHOT
	return 1;
#else
	char fn[PATH_MAX + 1], *p;
	Uint8 *pix = malloc(SCREEN_WIDTH * SCREEN_HEIGHT * 2 * 3);
	int a;
	if (pix == NULL) {
		ERROR_WINDOW("Not enough memory for taking a screenshot :(");
		return 1;
	}
	if (directory)
		strcpy(fn, directory);
	else
		*fn = 0;
	p = strchr(filename, '*');
	if (p) {
		a = strlen(fn);
		memcpy(fn + a, filename, p - filename);
		sprintf(fn + a + (p - filename), "%d", screenshot_index);
		strcat(fn, p + 1);
		screenshot_index++;
	} else
		strcat(fn, filename);
	for (a = 0; a < SCREEN_HEIGHT * SCREEN_WIDTH; a++) {
		int d = (a / SCREEN_WIDTH) * SCREEN_WIDTH * 6 + (a % SCREEN_WIDTH) * 3;
		pix[d + 0] = pix[d + 0 + SCREEN_WIDTH * 3] = (ep_pixels[a] >> 16) & 0xFF;
		pix[d + 1] = pix[d + 1 + SCREEN_WIDTH * 3] = (ep_pixels[a] >> 8) & 0xFF;
		pix[d + 2] = pix[d + 2 + SCREEN_WIDTH * 3] = ep_pixels[a] & 0xFF;
	}
	if (lodepng_encode24_file(fn, (unsigned char*)pix, SCREEN_WIDTH, SCREEN_HEIGHT * 2)) {
		free(pix);
		ERROR_WINDOW("LodePNG screenshot taking error");
		return 1;
	} else {
		free(pix);
		OSD("Screenshot:\n%s", fn + strlen(directory));
		return 0;
	}
#endif
}



#ifndef __EMSCRIPTEN__
static void set_app_icon ( SDL_Window *win, const void *app_icon )
{
	SDL_Surface *surf = SDL_CreateRGBSurfaceFrom((void*)app_icon,96,96,32,96*4,0x000000ff,0x0000ff00,0x00ff0000,0xff000000);
	if (surf == NULL)
		DEBUG("Cannot create surface for window icon: %s" NL, SDL_GetError());
	else {
		SDL_SetWindowIcon(win, surf);
		SDL_FreeSurface(surf);
	}
}
#endif


static SDL_bool XEP128_SDL_SetHint ( const char *sdl_hint_name, const char *cfg_sub_key, const char *defval )
{
	char res[256];
	const char *p = config_getopt_str("sdl");
	if (!strchr(p, '=')) {
		if (defval)
			return SDL_SetHint(sdl_hint_name, defval);
		else
			return SDL_TRUE;	// ignored
	}
	while (p && *p) {
		char *q = strchr(p, ':');
		if (q) q++;
		if (!strncasecmp(p, cfg_sub_key, strlen(cfg_sub_key)) && p[strlen(cfg_sub_key)] == '=') {
			p += strlen(cfg_sub_key) + 1;
			if (q) {
				memcpy(res, p, q - p - 1);
				res[q - p - 1] = '\0';
			} else
				strcpy(res, p);
			DEBUGPRINT("SDL: setting hint \"%s\" [cfg: \"%s\"] to \"%s\"" NL, sdl_hint_name, cfg_sub_key, res);
			return SDL_SetHint(sdl_hint_name, res);
		}
		p = q;
	}
	DEBUGPRINT("SDL: cannot found hint \"%s\" [cfg: \"%s\"] in sdl opt" NL, sdl_hint_name, cfg_sub_key);
	if (defval) {
		DEBUGPRINT("SDL: ... so setting the Xep128 internal specified default: \"%s\"" NL, defval);
		return SDL_SetHint(sdl_hint_name, defval);
	}
	return SDL_FALSE;
}



int screen_init ( void )
{
	win_xsize = SCREEN_WIDTH;
	win_ysize = SCREEN_HEIGHT * 2;
	XEP128_SDL_SetHint(SDL_HINT_RENDER_DRIVER, "driver", NULL);
	sdl_pixel_format = SDL_AllocFormat(SCREEN_FORMAT);
	sdl_win = SDL_CreateWindow(
                WINDOW_TITLE " v" VERSION,
                SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
                win_xsize, win_ysize,
                SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE
        );
        if (!sdl_win) {
		ERROR_WINDOW("Cannot open SDL window: %s", SDL_GetError());
		return 1;
	}
	SDL_VERSION(&sdl_wminfo.version);
#ifndef __EMSCRIPTEN__
	if (!SDL_GetWindowWMInfo(sdl_win, &sdl_wminfo)) {
		// ERROR_WINDOW("Cannot issue WMInfo call: %s", SDL_GetError());
		// return 1;
		DEBUGPRINT("UI: warning WMInfo call falied: %s", SDL_GetError());
	}
#endif
	SDL_SetWindowMinimumSize(sdl_win, SCREEN_WIDTH, SCREEN_HEIGHT * 2);
	//screen_window_resized();
#ifndef __EMSCRIPTEN__
	set_app_icon(sdl_win, _icon_pixels);
#endif
	sdl_ren = SDL_CreateRenderer(sdl_win, -1,
#ifdef __EMSCRIPTEN__
		// It seems, Emscripten uses WebGL context rather than browser-canvas 2D context, however that seems to be slower!!!
		0 /* SDL_RENDERER_SOFTWARE */
#else
		0
#endif
	);
	if (sdl_ren == NULL) {
		ERROR_WINDOW("Cannot create SDL renderer: %s", SDL_GetError());
		return 1;
	}
	SDL_RenderSetLogicalSize(sdl_ren, SCREEN_WIDTH, SCREEN_HEIGHT * 2);
	XEP128_SDL_SetHint(SDL_HINT_VIDEO_X11_NET_WM_PING, "wmping", "0");		// disable WM ping, SDL dialog boxes makes WMs things Xep128 is dead :-/
	XEP128_SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "quality", "1");		// render scale quality 0, 1, 2
	XEP128_SDL_SetHint(SDL_HINT_RENDER_VSYNC, "vsync", "0");			// disable vsync
#ifdef _WIN32
	XEP128_SDL_SetHint(SDL_HINT_WINDOWS_NO_CLOSE_ON_ALT_F4, "noaltf4", "1");	// 1 = disable ALT-F4 close on Windows
#endif
	XEP128_SDL_SetHint(SDL_HINT_VIDEO_MINIMIZE_ON_FOCUS_LOSS, "focuslossmin", "1");	// 1 = do minimize the SDL_Window if it loses key focus when in fullscreen mode
	XEP128_SDL_SetHint(SDL_HINT_VIDEO_ALLOW_SCREENSAVER, "screensaver", "1");	// 1 = enable screen saver
	sdl_tex = SDL_CreateTexture(sdl_ren, SCREEN_FORMAT, SDL_TEXTUREACCESS_STREAMING, SCREEN_WIDTH, SCREEN_HEIGHT);
	if (sdl_tex == NULL) {
		ERROR_WINDOW("Cannot create SDL texture: %s", SDL_GetError());
		return 1;
	}
	osd_pixels = alloc_xep_aligned_mem(SCREEN_WIDTH * SCREEN_HEIGHT * 4);
	if (osd_pixels != NULL) {
		sdl_osdtex = SDL_CreateTexture(sdl_ren, SCREEN_FORMAT, SDL_TEXTUREACCESS_STREAMING, SCREEN_WIDTH, SCREEN_HEIGHT);
		if (sdl_osdtex == NULL) {
			ERROR_WINDOW("Cannot create texture for OSD rendering, OSD won't work: %s", SDL_GetError());
			free(osd_pixels);
			osd_pixels = NULL;
		} else {
			osd_set_colour(0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0xFF, 0xFF);
			if (SDL_SetTextureBlendMode(sdl_osdtex, SDL_BLENDMODE_BLEND))
				ERROR_WINDOW("Warning, SDL BLEND mode cannot be used for OSD, there can be fade out problems.\n%s", SDL_GetError());
		}
	} else
		ERROR_WINDOW("Not enough memory for OSD pixel buffer. OSD won't work");
        sdl_winid = SDL_GetWindowID(sdl_win);
	DEBUG("SDL: everything seems to be OK ..." NL);
	return 0;
}


/* During SDL or Native dialogs/widgets, it seems some events detected by SDL event _later_ which is of course not wanted.
   This function simply "pops" out SDL events and do not use them to prevent them to be detected later by the emulator. */
void sdl_burn_events ( void )
{
	SDL_PumpEvents();
	SDL_FlushEvent(SDL_KEYDOWN);
	SDL_FlushEvent(SDL_KEYUP);
	SDL_FlushEvent(SDL_MOUSEMOTION);
	SDL_FlushEvent(SDL_MOUSEWHEEL);
	SDL_FlushEvent(SDL_MOUSEBUTTONDOWN);
	SDL_FlushEvent(SDL_MOUSEBUTTONUP);
	kbd_matrix_reset();	// also reset the keyboard matrix as it seems some keys can be detected "stucked" ...
	mouse_reset_button();	// ... and also the mouse buttons :)
}


int _sdl_emu_secured_message_box_ ( Uint32 sdlflag, const char *msg )
{
        int mg = mouse_grab, r;
	audio_stop();
	if (mg == SDL_TRUE) screen_grab(SDL_FALSE);
#ifdef __EMSCRIPTEN__
	if (1 || sdlflag == SDL_MESSAGEBOX_ERROR) {
		EM_ASM_INT({
			window.alert(Pointer_stringify($0));
		}, msg);
	}
#endif
	r = SDL_ShowSimpleMessageBox(sdlflag, WINDOW_TITLE, msg, sdl_win);
	if (mg == SDL_TRUE) screen_grab(mg);
	audio_start();
	sdl_burn_events();
	return r;
}



int _sdl_emu_secured_modal_box_ ( const char *items_in, const char *msg )
{
	char items_buf[512], *items = items_buf;
	int buttonid, mg = mouse_grab;
	SDL_MessageBoxButtonData buttons[16];
	SDL_MessageBoxData messageboxdata = {
		SDL_MESSAGEBOX_INFORMATION, /* .flags */
		sdl_win, /* .window */
		WINDOW_TITLE, /* .title */
		msg, /* .message */
		0,	/* number of buttons, will be updated! */
		buttons,
		NULL	// &colorScheme
	};
	strcpy(items_buf, items_in);
	for (;;) {
		char *p = strchr(items, '|');
		switch (*items) {
			case '!':
#ifdef __EMSCRIPTEN__
				DEBUGPRINT("Emscripten: faking chooser box answer %d for \"%s\"" NL, messageboxdata.numbuttons, msg);
				return messageboxdata.numbuttons;
#endif
				buttons[messageboxdata.numbuttons].flags = SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT;
				items++;
				break;
			case '?':
				buttons[messageboxdata.numbuttons].flags = SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT;
				items++;
				break;
			default:
				buttons[messageboxdata.numbuttons].flags = 0;
				break;
		}
		buttons[messageboxdata.numbuttons].text = items;
		buttons[messageboxdata.numbuttons].buttonid = messageboxdata.numbuttons;
		messageboxdata.numbuttons++;
		if (p == NULL) break;
		*p = 0;
		items = p + 1;
	}
	/* win grab, kbd/mouse emu reset etc before the window! */
	audio_stop();
	if (mg == SDL_TRUE) screen_grab(SDL_FALSE);
	SDL_ShowMessageBox(&messageboxdata, &buttonid);
	if (mg == SDL_TRUE) screen_grab(mg);
	audio_start();
	sdl_burn_events();
	return buttonid;
}
