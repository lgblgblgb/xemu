/* The Xemu project.
   Copyright (C)2016-2019 LGB (Gábor Lénárt) <lgblgblgb@gmail.com>

   This is the Commander X16 emulation. Note: the source is overcrowded with comments by intent :)
   That it can useful for other people as well, or someone wants to contribute, etc ...

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
#include "sdcard.h"

static int sd_selected = 0;
static int select_line_transitions = 0;


void  sdcard_spi_select ( int select )
{
	if (select != sd_selected) {
		sd_selected = select;
		select_line_transitions++;
		DEBUGPRINT("SDCARD: select line goes to %s (#%d)" NL, select ? "high" : "low", select_line_transitions);
	}
}


Uint8 sdcard_spi_transfer ( Uint8 data )
{
	DEBUGPRINT("SDCARD: initiated SPI transfer, MOSI=$%02X" NL, data);
	return 0xFF;
}
