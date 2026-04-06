/* A work-in-progess MEGA65 (Commodore-65 clone origins) emulator
   Part of the Xemu project, please visit: https://github.com/lgblgblgb/xemu
   Copyright (C)2016-2026 LGB (Gábor Lénárt) <lgblgblgb@gmail.com>

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

#ifdef	XEMU_ARCH_HTML

// This source is only used for the EMSCRIPTEN build, for the official emulator
// demonstration page. Only makes sense with the custom shell: assets/shell.html

#include "xemu/emutools.h"
#include "em_gw.h"
#include <emscripten.h>
#include "sdcard.h"
#include "mega65.h"
#include "matrix_mode.h"
#include <errno.h>
#include <unistd.h>
#include "inject.h"


static int em_msg = 0;


EMSCRIPTEN_KEEPALIVE int msg_gate ( int msg )
{
	if (!em_msg && msg)
		em_msg = msg;
	return em_msg;
}


static int silly_silly_rename ( const char *oldname_o, const char *newname_o )
{
	static const char prefix[] = "/files/";
	char oldname[strlen(prefix) + strlen(oldname_o) + 1];
	char newname[strlen(prefix) + strlen(newname_o) + 1];
	strcpy(oldname, prefix);
	strcpy(newname, prefix);
	strcat(oldname, oldname_o);
	strcat(newname, newname_o);
	const int ret = rename(oldname, newname);
	if (ret)
		DEBUGPRINT("MSG: Cannot rename \"%s\" to \"%s\": %s" NL, oldname, newname, strerror(errno));
	else
		DEBUGPRINT("MSG: Successfully renamed \"%s\" to \"%s\"" NL, oldname, newname);
	return ret;
}


static void mount_d81 ( void )
{
	static int phase = 0;
	static const char input_d81[] = "hdos/NEW.D81";
	static char fn[] = "hdos/MOUNT0.D81";
	const int next_phase = phase ^ 1;
	fn[sizeof(fn) - 6] = '0' + next_phase;
	if (silly_silly_rename(input_d81, fn)) {
		ERROR_WINDOW("Could not rename file (check emulator output)");
		return;
	}
	DEBUGPRINT("MSG: trying to mount %s" NL, strrchr(fn, '/') + 1);
	if (sdcard_external_mount(0, fn, NULL)) {
		DEBUGPRINT("MSG: Unsuccessful mount :(" NL);
	} else {
		DEBUGPRINT("MSG: Successful mount :)" NL);
		phase = next_phase;
	}
}


static void import_basic_prg ( void )
{
	reset_mega65(RESET_MEGA65_SOFT);
	inject_register_import_basic_text("hdos/PRG.BAS");
}


static void run_demo ( void )
{
	sdcard_unmount(0);
	reset_mega65(RESET_MEGA65_SOFT);
	sdcard_external_mount(0, "files/hdos/mega65.d81", NULL);
	inject_register_command("RUN\"*\"");
}


void emgw_msg_gate_dispatch ( void )
{
	if (!em_msg)
		return;
	const int msg = em_msg;
	em_msg = 0;
	DEBUGPRINT("MSG: emscripten gateway message processing: %d" NL, msg);
	switch (msg) {
		case 1:
			reset_mega65(RESET_MEGA65_HARD);
			break;
		case 2:
			reset_mega65(RESET_MEGA65_SOFT);
			break;
		case 3:
			xemu_set_full_screen(1);
			break;
		case 4:
			matrix_mode_toggle(!in_the_matrix);
			break;
		case 5:
			mount_d81();
			break;
		case 6:
			import_basic_prg();
			break;
		case 7:
			run_demo();
			break;
		default:
			DEBUGPRINT("MSG: unknown gateway message: %d" NL, msg);
			break;
	}
}

#endif
