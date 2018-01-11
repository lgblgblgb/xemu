/* A work-in-progess Mega-65 (Commodore-65 clone origins) emulator
   Part of the Xemu project, please visit: https://github.com/lgblgblgb/xemu
   Copyright (C)2016-2018 LGB (Gábor Lénárt) <lgblgblgb@gmail.com>

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

#ifdef HAVE_ETHERTAP
#include "xemu/emutools.h"
#include "xemu/ethertap.h"
#include "ethernet65.h"
#include <errno.h>

static volatile int eth65_enabled = 0;
static SDL_Thread *thread_id = NULL;

// WARNING: this function run as *THREAD*
// It must be very careful not use anything too much.
// xemu_tuntap_read and write functions are used only here, so it's maybe OK
// but exchange data with the main thread (the emulator) must be done very carefully!
static int eth65_main ( void *user_data_unused )
{
	unsigned char buffer[8192];
	while (eth65_enabled) {
		int r = xemu_tuntap_read(buffer, 1, sizeof(buffer));
		printf("ETH-THREAD: return of read = %d" NL, r);
		if (r <= 0)
			break;
		if (r == sizeof buffer)
			continue;	// abnormally large packet received
	}
	printf("ETH-THREAD: leaving ..." NL);
	return 0;
}


void eth65_shutdown ( void )
{
	eth65_enabled = 0;
	xemu_tuntap_close();
}


void eth65_init ( const char *device_name )
{
	if (!device_name)
		DEBUGPRINT("ETH: not enabled" NL);
	else {
		if (xemu_tuntap_alloc(device_name, NULL, 0, XEMU_TUNTAP_IS_TAP | XEMU_TUNTAP_NO_PI) < 0)
			ERROR_WINDOW("ETH: Ethernet TAP device \"%s\" error: %s", device_name, strerror(errno));
		else {
			eth65_enabled = 1;
			// Initialize our thread for device read/write ...
			thread_id = SDL_CreateThread(eth65_main, "Xemu-EtherTAP", NULL);
			if (thread_id) {
				DEBUGPRINT("ETH: enabled - thread %p started, name = \"%s\"" NL, thread_id, device_name);
			} else {
				eth65_enabled = 0;
				xemu_tuntap_close();
				ERROR_WINDOW("ETH: Error creating thread for Ethernet emulation: %s", SDL_GetError());
				
			}
		}
	}
}




#endif
