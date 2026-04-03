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

#include "xemu/emutools.h"
#include "em_gw.h"
#include <emscripten.h>
#include "sdcard.h"
#include "mega65.h"
#include "matrix_mode.h"


static int em_msg = 0;


EMSCRIPTEN_KEEPALIVE int msg_gate ( int msg )
{
	if (!em_msg && msg)
		em_msg = msg;
	return em_msg;
}


static void mount_d81 ( const int id )
{
	static char fn[] = "hdos/MOUNT0.D81";
	fn[sizeof(fn) - 6] = '0' + id;
	DEBUGPRINT("MSG: trying to mount %s" NL, strrchr(fn, '/') + 1);
	sdcard_external_mount(0, fn, NULL);
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
			reset_mega65(RESET_MEGA65_CPU);
			break;
		case 3:
			xemu_set_full_screen(1);
			break;
		case 4:
			matrix_mode_toggle(!in_the_matrix);
			break;
		case 5:
		case 6:
			mount_d81(msg - 5);
			break;
		default:
			DEBUGPRINT("MSG: unknown gateway message: %d" NL, msg);
			break;
	}
}

#endif
