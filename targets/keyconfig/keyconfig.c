/* Part of the Xemu project, please visit: https://github.com/lgblgblgb/xemu
   Copyright (C)2020 LGB (Gábor Lénárt) <lgblgblgb@gmail.com>

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
//#include "xemu/emutools_files.h"
//#include "xemu/emutools_hid.h"
//#include "xemu/emutools_config.h"
// #include "xemu/emutools_nativegui.h"


//#include <ctype.h>
//#include <strings.h>


extern Uint32 *sdl_pixel_buffer;

#define SCREEN_FORMAT           SDL_PIXELFORMAT_ARGB8888
// Do not modify this, this very program depends on it being 0.
#define USE_LOCKED_TEXTURE      0
#define RENDER_SCALE_QUALITY    1
#define SCREEN_WIDTH            800
#define SCREEN_HEIGHT           600

#define FRAME_DELAY		40

//static const struct KeyMappingDefault dummy_key_map[] = { { 0, -1 } };


static const Uint8 init_vic2_palette_rgb[16 * 3] = {    // VIC2 palette given by RGB components
	0x00, 0x00, 0x00,
	0xFF, 0xFF, 0xFF,
	0x74, 0x43, 0x35,
	0x7C, 0xAC, 0xBA,
	0x7B, 0x48, 0x90,
	0x64, 0x97, 0x4F,
	0x40, 0x32, 0x85,
	0xBF, 0xCD, 0x7A,
	0x7B, 0x5B, 0x2F,
	0x4f, 0x45, 0x00,
	0xa3, 0x72, 0x65,
	0x50, 0x50, 0x50,
	0x78, 0x78, 0x78,
	0xa4, 0xd7, 0x8e,
	0x78, 0x6a, 0xbd,
	0x9f, 0x9f, 0x9f
};


static Uint32 palette[16];








void clear_emu_events ( void )
{
	xemu_drop_events();
}




int main ( int argc, char **argv )
{
	xemu_pre_init(APP_ORG, TARGET_NAME, "Xemu Keyboard Configurator from LGB");
	if (argc != 4)
		FATAL("Missing specifier(s) from command line. This program is not meant to be used manually.\nPlease use the menu of an Xemu emulator which supports key re-mapping.");
	if (xemu_post_init(
		TARGET_DESC APP_DESC_APPEND,	// window title
		1,				// resizable window
		SCREEN_WIDTH, SCREEN_HEIGHT,	// texture sizes
		SCREEN_WIDTH, SCREEN_HEIGHT,	// logical size
		SCREEN_WIDTH, SCREEN_HEIGHT,	// window size
		SCREEN_FORMAT,		// pixel format
		16,			// number of colours to init
		init_vic2_palette_rgb,	// init from this
		palette,		// .. and into this!
		RENDER_SCALE_QUALITY,	// render scaling quality
		USE_LOCKED_TEXTURE,	// 1 = locked texture access
		NULL			// no emulator specific shutdown function
	))
		return 1;
	osd_init_with_defaults();
	OSD(-1, -1, "Welcome to the Keymap Configurator!");
	for (unsigned int a = 0; a < SCREEN_WIDTH * SCREEN_HEIGHT; a++)
		sdl_pixel_buffer[a] = palette[6];
	Uint32 old_ticks;
	xemu_update_screen();
	for (;;) {
		int force_render = 0;
		SDL_Event ev;
		old_ticks = SDL_GetTicks();
		while (SDL_PollEvent(&ev)) {
			switch(ev.type) {
				case SDL_QUIT:
					if (ARE_YOU_SURE(NULL, ARE_YOU_SURE_DEFAULT_YES))
						exit(1);
					break;
				case SDL_KEYDOWN:
					OSD(-1, -1, "%s", SDL_GetScancodeName(ev.key.keysym.scancode));
					break;
				//case SDL_KEYUP:
				//case SDL_MOUSEBUTTONDOWN:
				//case SDL_MOUSEBUTTONUP:
				case SDL_MOUSEMOTION:
					if (ev.motion.x >= 0 && ev.motion.x < SCREEN_WIDTH && ev.motion.y >= 0 && ev.motion.y < SCREEN_HEIGHT) {
						DEBUGPRINT("X=%d, Y=%d" NL, ev.motion.x, ev.motion.y);
						sdl_pixel_buffer[ev.motion.x + ev.motion.y * SCREEN_WIDTH] = palette[1];
						force_render = 1;
					}
					break;
				case SDL_WINDOWEVENT:
					switch (ev.window.event) {
						case SDL_WINDOWEVENT_SHOWN:
						case SDL_WINDOWEVENT_HIDDEN:
						case SDL_WINDOWEVENT_EXPOSED:
						case SDL_WINDOWEVENT_RESIZED:
						case SDL_WINDOWEVENT_SIZE_CHANGED:
						case SDL_WINDOWEVENT_MOVED:
						case SDL_WINDOWEVENT_MINIMIZED:
						case SDL_WINDOWEVENT_MAXIMIZED:
						case SDL_WINDOWEVENT_RESTORED:
							DEBUGPRINT("EVENT: handled window event" NL);
							force_render = 1;
							break;
						default:
							DEBUGPRINT("EVENT: un-handled window event" NL);
							break;
					}
					break;
				default:
					break;
			}
		}
		if (force_render || osd_status)
			xemu_update_screen();
		else
			DEBUGPRINT("No need to update, yeah-youh!" NL);
		Uint32 new_ticks = SDL_GetTicks();
		Uint32 delay = FRAME_DELAY - (new_ticks - old_ticks);
		if (delay > 0 && delay <= FRAME_DELAY)
			SDL_Delay(FRAME_DELAY);
	}
}
