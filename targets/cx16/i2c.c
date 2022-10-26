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

static struct {
	int	clock;
	int	data;
	int	old_clock;
	int	old_data;
} bus; /* = {
	.clock	= 1,
	.data	= 1,
	.
};*/



Uint8 i2c_bus_read ( void )
{
	return 3;
	return (bus.clock ? 2 : 0) + (bus.data ? 1 : 0);
}


void i2c_bus_write ( const Uint8 data, const Uint8 mask )
{
	bus.old_clock = bus.clock;
	bus.old_data = bus.data;
	// check CLK
	if (mask & 2)			// DDR: output ...
		bus.clock = data & 2;	// ... so VIA drives the bus.
	else				// DDR: input ...
		bus.clock = 2;		// ... so pull-up resistor.
	// check DATA
	if (mask & 1)			// DDR: output ...
		bus.data = data & 1;	// ... so VIA drives the bus.
	else				// DDR: input ...
		bus.data = 1;		// ... so pull-up resistor.
	if (bus.data != bus.old_data || bus.clock != bus.old_clock) {
		static Uint64 collect = 0;
		collect = (collect << 1) | (Uint64)bus.data;
		DEBUGPRINT("I2C: change: CLK:%d->%d DAT:%d->%d DATA:%X" NL, bus.old_clock, bus.clock, bus.old_data, bus.data, (unsigned int)collect);
	}
}
