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


#define SCREEN_FORMAT           SDL_PIXELFORMAT_ARGB8888
// Do not modify this, this very program depends on it being 0.
#define USE_LOCKED_TEXTURE      0
#define RENDER_SCALE_QUALITY    1
#define SCREEN_WIDTH            800
#define SCREEN_HEIGHT           600
#define FRAME_DELAY		40

extern Uint32 *sdl_pixel_buffer;
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
static Uint32 palette[256];


void clear_emu_events ( void )
{
	xemu_drop_events();
}

static void clear_screen ( void )
{
	for (unsigned int a = 0; a < SCREEN_WIDTH * SCREEN_HEIGHT; a++)
		sdl_pixel_buffer[a] = palette[6];
}

static void clear_area ( int x1, int y1, int x2, int y2, Uint32 colour )
{
	Uint32 *pix = sdl_pixel_buffer + y1 * SCREEN_WIDTH;
	while (y1 <= y2) {
		y1++;
		for (int x = x1; x <= x2; x++)
			pix[x] = colour;
		pix += SCREEN_WIDTH;
	}
}

static void write_char ( int x1, int y1, char chr, Uint32 colour )
{
	Uint32 *pix = sdl_pixel_buffer + y1 * SCREEN_WIDTH + x1;
	chr = ((signed char)chr < 32) ? '?' - 32 : chr - 32;
	for (int y = 0; y < 16; y++) {
		for (Uint32 b = font_16x16[(chr << 4) + y], x = 0; x < 16; b <<= 1, x++) {
			if ((b & 0x8000))
				pix[x] = colour;
		}
		pix += SCREEN_WIDTH;
	}
}

static void write_string ( int x1, int y1, const char *str, Uint32 colour )
{
	while (*str) {
		write_char(x1, y1, *str++, colour);
		x1 += 9;
	}
}

static void construct_keyboard ( void )
{
}

static struct {
	int position;
} keyboard[] = {
};


#define OSD_TRAY(...)	OSD(-1,SCREEN_HEIGHT-20,__VA_ARGS__)


int main ( int argc, char **argv )
{
	xemu_pre_init(APP_ORG, TARGET_NAME, "Xemu Keyboard Configurator");
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
	//osd_init_with_defaults();
	const Uint8 osd_palette[] = {
		0xFF, 0, 0, 0x80,	// with alpha channel 0x80
		0xFF,0xFF,0xFF,0xFF	// white
	};
	osd_init(
		//OSD_TEXTURE_X_SIZE, OSD_TEXTURE_Y_SIZE,
		SCREEN_WIDTH * 1, SCREEN_HEIGHT * 1,
		osd_palette,
		sizeof(osd_palette) >> 2,
		OSD_FADE_DEC_VAL,
		OSD_FADE_END_VAL
	);
	OSD_TRAY("Welcome to the Keymap Configurator!");
	clear_screen();
	SDL_Surface *surf = SDL_LoadBMP("mega65-kbd.bmp");
	if (surf) {
		printf("Colours=%d\n", surf->format->palette->ncolors);
		for (int a = 0; a < 128; a++)
			palette[128 + (a & 127)] = SDL_MapRGBA(
				sdl_pix_fmt,
				surf->format->palette->colors[a].r,
				surf->format->palette->colors[a].g,
				surf->format->palette->colors[a].b,
				0xFF
			);
		printf("BitsPerPixel=%d BytesPerPixel=%d\n",
			surf->format->BitsPerPixel,
			surf->format->BytesPerPixel
		);
		for (int a = 0; a < 800 * 300; a++)
			sdl_pixel_buffer[a] = palette[128 + (((Uint8*)surf->pixels)[a] & 127)];
	} else
		FATAL("Cannot load keyboard image: %s", SDL_GetError());
	//write_char(10,10,'A', palette[1]);
	write_string(10, 10, "Printout!", palette[1]);
	Uint32 old_ticks;
	xemu_update_screen();
	SDL_Scancode result_for_assignment = SDL_SCANCODE_UNKNOWN;
	int wait_for_assignment = 0;
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
				case SDL_KEYUP:
					if (wait_for_assignment) {
						if (ev.key.repeat) {
							OSD_TRAY("ERROR: key repeats, too long press");
						} else if (result_for_assignment != ev.key.keysym.scancode && ev.key.keysym.scancode != SDL_SCANCODE_UNKNOWN) {
							OSD_TRAY("ERROR: multiple keys used!");
							result_for_assignment = SDL_SCANCODE_UNKNOWN;
						} else if (result_for_assignment == ev.key.keysym.scancode && result_for_assignment != SDL_SCANCODE_UNKNOWN) {
							// FIXME TODO: check is key is free! And only allow to accept then!
							OSD_TRAY("OK: assigned key: %s", SDL_GetScancodeName(ev.key.keysym.scancode));
							result_for_assignment = SDL_SCANCODE_UNKNOWN;
							wait_for_assignment = 0;
						}
					}/*else
						OSD_TRAY("No key is clicked on the map");*/
					break;
				case SDL_KEYDOWN:
					if (wait_for_assignment) {
						if (ev.key.repeat) {
							OSD_TRAY("ERROR: key repeats, too long press");
						} else if (result_for_assignment != SDL_SCANCODE_UNKNOWN) {
							OSD_TRAY("ERROR: multiple keypressed used!");
							//result_for_assignment = ev.key.keysym.scancode;
						} else {
							if (ev.key.keysym.scancode == SDL_SCANCODE_UNKNOWN)
								OSD_TRAY("ERROR: This key has no proper SDL decode");
							else
								result_for_assignment = ev.key.keysym.scancode;
						}
					}/* else
						OSD_TRAY("No key is clicked on the map");*/
					break;
				case SDL_MOUSEBUTTONDOWN:
					DEBUGPRINT("X=%d, Y=%d" NL, ev.button.x, ev.button.y);
					OSD_TRAY("Waiting your keypress to assign!!");
					wait_for_assignment = 1;
					break;
				//case SDL_MOUSEBUTTONUP:
				case SDL_MOUSEMOTION:
					if (ev.motion.x >= 0 && ev.motion.x < SCREEN_WIDTH && ev.motion.y >= 0 && ev.motion.y < SCREEN_HEIGHT) {
						//DEBUGPRINT("X=%d, Y=%d" NL, ev.motion.x, ev.motion.y);
						sdl_pixel_buffer[ev.motion.x + ev.motion.y * SCREEN_WIDTH] = palette[1];
						force_render = 1;
					}
					break;
				case SDL_WINDOWEVENT:
					// it's a bit cruel, but it'll do
					// basically I just want to avoid rendering texture to screen if not needed
					// any window event MAY mean some reason to do it, some of them may be not
					// but it's not fatal anyway to do so.
					force_render = 1;
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
