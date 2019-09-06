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

#ifndef __EMSCRIPTEN__

#include "mega65.h"
#include "xemu/emutools_files.h"
#include "xemu/d81access.h"
#include "sdcard.h"

#ifdef _WIN32
#	define FILE_BROWSER "explorer"
#elif __APPLE__
#	define FILE_BROWSER "open"
#	define UNIXISH
#else
#	define FILE_BROWSER "xdg-open"
#	define UNIXISH
#endif


static int are_you_sure ( const char *s )
{
	return (QUESTION_WINDOW("YES|NO", (s != NULL && *s != '\0') ? s : "Are you sure?") == 0);
}


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
			if (are_you_sure("According to its size, the dropped file can be a D81 image. Shall I mount it for you?")) {
				if (fd_mounted) {
					if (mount_external_d81(fn, 0))
						ERROR_WINDOW("Mount failed for some reason.");
				} else
					ERROR_WINDOW("Cannot mount external D81, since Mega65 was not instructed to mount any FD access yet.");
			}
		}
	}
}


static void open_native_file_browser ( char *dir )
{
#ifdef HAVE_XEMU_EXEC_API
	static xemuexec_process_t fbp = XEMUEXEC_NULL_PROCESS_ID;
	char *args[] = {FILE_BROWSER, dir, NULL};
	if (fbp) {
		int w = xemuexec_check_status(fbp, 0);
		DEBUGPRINT("UI: previous file browser process (" PRINTF_LLD ") status was: %d" NL, (unsigned long long int)(uintptr_t)fbp, w);
		if (w == XEMUEXEC_STILL_RUNNING)
			ERROR_WINDOW("A file browser is already has been opened.");
		else if (w == -1)
			ERROR_WINDOW("Process communication problem");
		else
			fbp = 0;
	}
	if (!fbp)
		fbp = xemuexec_run(args);	// FIXME: process on exit will be "orpahned" (ie zombie) till exit from Xemu, because it won't be wait()'ed by the parent (us) ...
#else
	ERROR_WINDOW("Sorry, no execution API is supported by this Xemu build\nto allow to launch an OS-native file browser for you on directory:\n%s", dir);
#endif
}


void ui_enter ( void )
{
	DEBUGPRINT("UI: right-click" NL);
	switch (QUESTION_WINDOW("Reset|Quit|Fullscr|Pref.dir", "Xemu Quick Task Menu")) {
		case 0:
			reset_mega65();
			break;
		case 1:
			if (are_you_sure(NULL)) {
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
			open_native_file_browser(sdl_pref_dir);
			break;
		default:
			break;
	}
}


#else
void ui_enter ( void ) {}
#endif
