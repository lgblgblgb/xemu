/* A work-in-progess Mega-65 (Commodore-65 clone origins) emulator
   Part of the Xemu project, please visit: https://github.com/lgblgblgb/xemu
   Copyright (C)2016-2019 LGB (Gábor Lénárt) <lgblgblgb@gmail.com>

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
#include "ui.h"
#include "xemu/emutools_nativegui.h"
#include "mega65.h"
#include "xemu/emutools_files.h"
#include "xemu/d81access.h"
#include "sdcard.h"


#if defined(CONFIG_DROPFILE_CALLBACK) || defined(XEMU_NATIVEGUI)

static void attach_d81 ( const char *fn )
{
	if (fd_mounted) {
		if (mount_external_d81(fn, 0))
			ERROR_WINDOW("Mount failed for some reason.");
	} else
		ERROR_WINDOW("Cannot mount external D81, since Mega65 was not instructed to mount any FD access yet.");
}


// #if defined(CONFIG_DROPFILE_CALLBACK) || defined(XEMU_NATIVEGUI_C)
#endif


#ifdef CONFIG_DROPFILE_CALLBACK
void emu_dropfile_callback ( const char *fn )
{
	DEBUGPRINT("UI: drop event, file: %s" NL, fn);
	int ret = xemu_load_file(fn, NULL, 10, D81_SIZE, "Cannot load and/or process the dropped file.");
	DEBUGPRINT("BUFFER=%p" NL, xemu_load_buffer_p);
	if (ret >= 0) {
		if (ret == 128 * 1024) {
			INFO_WINDOW("Maybe ROM image?");
		}
		free(xemu_load_buffer_p);
		// !!! buffer is not valid anymore, only things can go below, who does not need to access the loaded buffer which is lost now!
		if (ret == D81_SIZE) {
			if (ARE_YOU_SURE("According to its size, the dropped file can be a D81 image. Shall I mount it for you?")) {
				attach_d81(fn);
			}
		}
	}
}
#endif


#ifdef XEMU_NATIVEGUI


static void attach_d81_by_browsing ( void )
{
	char fnbuf[PATH_MAX + 1];
	static char dir[PATH_MAX + 1] = "";
	if (!xemunativegui_file_selector(
		XEMUNATIVEGUI_FSEL_OPEN | XEMUNATIVEGUI_FSEL_FLAG_STORE_DIR,
		"Select D81 to attach",
		dir,
		fnbuf,
		sizeof fnbuf
	))
		attach_d81(fnbuf);
}



void ui_enter ( void )
{
	DEBUGPRINT("UI: right-click" NL);
	switch (QUESTION_WINDOW("Reset|Quit|Fullscr|Pref.dir|Console|Attach D81", "Xemu Quick Task Menu")) {
		case 0:
			reset_mega65();
			break;
		case 1:
			if (ARE_YOU_SURE(NULL)) {
				SDL_Event ev;
				ev.type = SDL_QUIT;
				SDL_PushEvent(&ev);
				//exit(0);
			}
			break;
		case 2:
			xemu_set_full_screen(-1);
			break;
		case 3:
			xemuexec_open_native_file_browser(sdl_pref_dir);
			break;
		case 4:
			sysconsole_toggle(-1);
			break;
		case 5:
			attach_d81_by_browsing();
			break;
		default:
			break;
	}
}

// #ifdef XEMU_NATIVEGUI_C
#else
void ui_enter ( void ) {
	DEBUGPRINT("UI: no menu handler is implemented :(" NL);
}
#endif
