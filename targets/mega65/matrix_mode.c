/* A work-in-progess MEGA65 (Commodore-65 clone origins) emulator
   Part of the Xemu project, please visit: https://github.com/lgblgblgb/xemu
   Copyright (C)2016-2022 LGB (Gábor Lénárt) <lgblgblgb@gmail.com>

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
#include "matrix_mode.h"

#include "xemu/cpu65.h"


int in_the_matrix = 0;



static void matrix_update ( void )
{
	if (!in_the_matrix)
		return;		// should not happen, but ...
	osd_write_string(0,  0, "Not implemented yet ..");
	osd_write_string(0, 16, "Press Right-ALT and TAB to exit");
	char buffer[100];
	snprintf(buffer, sizeof buffer, "PC:$%04X A:$%02X X:$%02X Y:$%02X Z:$%02X", cpu65.pc, cpu65.a, cpu65.x, cpu65.y, cpu65.z);
	osd_write_string(0, 32, buffer);
	osd_update();
}


void matrix_mode_toggle ( int status )
{
	status = !!status;
	if (status == !!in_the_matrix)
		return;
	in_the_matrix = status;
	if (in_the_matrix) {
		osd_notifications_enabled = 0;	// disable OSD notification as it would cause any notify event would mess up the matrix mode
		DEBUGPRINT("MATRIX: on" NL);
		osd_update_callback = matrix_update;
		osd_on(OSD_STATIC);
		osd_clear_with_colour(2);
		osd_set_colours(3, 2);
	} else {
		DEBUGPRINT("MATRIX: off" NL);
		osd_update_callback = NULL;
		osd_set_colours(1, 0);	// restore colours
		osd_clear();
		osd_off();
		osd_notifications_enabled = 1;
	}
}
