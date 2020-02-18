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
#include "xemu/emutools_files.h"
#include "xemu/emutools_hid.h"
#include "xemu/emutools_config.h"
// #include "xemu/emutools_nativegui.h"

//#include <ctype.h>
//#include <strings.h>


#define SCREEN_FORMAT           SDL_PIXELFORMAT_ARGB8888
#define USE_LOCKED_TEXTURE      1
#define RENDER_SCALE_QUALITY    1
#define SCREEN_WIDTH            300
#define SCREEN_HEIGHT           230

static const struct KeyMappingDefault dummy_key_map[] = { { 0, -1 } };



void clear_emu_events ( void )
{
	hid_reset_events(1);
}


// HID needs this to be defined, it's up to the emulator if it uses or not ...
int emu_callback_key ( int pos, SDL_Scancode key, int pressed, int handled )
{
        return 0;
}


void emu_quit_callback ( void )
{
}



int main ( int argc, char **argv )
{
	xemu_pre_init(APP_ORG, TARGET_NAME, "Xemu Keyboard Configurator from LGB");
	xemucfg_define_str_option("defaultmap", NULL, "Full path of the default keyboard map");
	xemucfg_define_str_option("usermap", NULL, "Full path of the used, user-definiable map");
	xemucfg_define_str_option("symbolmap", NULL, "Full path of symbol map of the keyboard");
	if (xemucfg_parse_all(argc, argv))
		return 1;
	const char *defmap = xemucfg_get_str("defaultmap");
	const char *usrmap = xemucfg_get_str("usermap");
	const char *symmap = xemucfg_get_str("symbolmap");
	if (!defmap || !*defmap || !usrmap || !*usrmap || !symmap || !*symmap)
		FATAL("Missing specifier(s) from command line. This program is not meant to be used manually.\nPlease use the menu of an Xemu emulator which supports key re-mapping.");
	// xemunativegui_init();
	/* Initiailize SDL - note, it must be before loading ROMs, as it depends on path info from SDL! */
	if (xemu_post_init(
		TARGET_DESC APP_DESC_APPEND,	// window title
		1,				// resizable window
		SCREEN_WIDTH, SCREEN_HEIGHT,	// texture sizes
		SCREEN_WIDTH, SCREEN_HEIGHT,	// logical size (width is doubled for somewhat correct aspect ratio)
		SCREEN_WIDTH * 3, SCREEN_HEIGHT * 3,	// window size (tripled in size, original would be too small)
		SCREEN_FORMAT,		// pixel format
		0,			// Prepare for colour primo, we have many colours, we want to generate at our own, later
		NULL,			// -- "" --
		NULL,			// -- "" --
		RENDER_SCALE_QUALITY,	// render scaling quality
		USE_LOCKED_TEXTURE,	// 1 = locked texture access
		NULL			// no emulator specific shutdown function
	))
		return 1;
	//for (int a = 0; a < 0x100; a++)	// generate (colour primo's) palette
	//	primo_palette[a] = SDL_MapRGBA(sdl_pix_fmt, (a >> 5) * 0xFF / 7, ((a >> 2) & 7) * 0xFF / 7, ((a << 1) & 7) * 0xFF / 7, 0xFF);
	//primo_palette_white = SDL_MapRGBA(sdl_pix_fmt, 0xFF, 0xFF, 0xFF, 0xFF); // colour primo scheme seems to have no white :-O So we do an extra entry for non-colour primo's only colour :)
	hid_init(
		dummy_key_map,
		0,
		SDL_DISABLE		// joystick HID events
	);
	osd_init_with_defaults();
	clear_emu_events();	// also resets the keyboard
	xemu_timekeeping_start();	// we must call this once, right before the start of the emulation
	//XEMU_MAIN_LOOP(emulation_loop, 25, 1);
	return 0;
}
