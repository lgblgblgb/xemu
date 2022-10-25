/* Part of the Xemu project, please visit: https://github.com/lgblgblgb/xemu
   Copyright (C)2016-2022 LGB (Gábor Lénárt) <lgblgblgb@gmail.com>

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
#include "i2c.h"

static int sdc = 2;
static int sda_send = 1;
static int state = 0;



Uint8 i2c_bus_read ( void )
{
	return 3;
	if (!state)
		return 3;
	return sdc | 1;
}


void i2c_bus_write ( Uint8 data )
{
	data &= 3;
	static int old = 0, sda_old, sdc_old;
	if (old == data)
		return;
	const int sda = data & 1;
	sdc = data & 2;
	const int sdc_raising_edge = !sdc_old &&  sdc;
	const int sdc_falling_edge =  sdc_old && !sdc;

	DEBUGPRINT("I2C send: %d->%d" NL, old, data);
	static int command;
	if (old == 3 && data == 0 && state == 0) {
		// was a "start condition"
		DEBUGPRINT("I2C: start condition!" NL);
		state = 1;
		command = 0;
	} else {
		command = (command << 1) + sda;
		DEBUGPRINT("I2C: another change: %d -> %d command so far: $%02X" NL, old, data, command);
	}
	old = data;
	sda_old = sda;
	sdc_old = sdc;
}
