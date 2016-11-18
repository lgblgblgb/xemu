/* Test-case for a very simple, inaccurate, work-in-progress Commodore 65 emulator.
   Copyright (C)2016 LGB (Gábor Lénárt) <lgblgblgb@gmail.com>

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

#include "emutools.h"
#include "c65_snapshot.h"
#include "emutools_snapshot.h"
#include "emutools_config.h"
#include "commodore_65.h"
#include "cpu65c02.h"
#include <stdlib.h>

static char *savefile = NULL;



static int snap_memory_loader ( struct xemu_snapshot_block_st *block )
{
	if (block->block_version != 0 || block->sub_counter != 0 || block->sub_size != sizeof(memory))
		RETURN_XSNAPERR_USER("Bad memory block syntax");
	return xemusnap_read_file(memory, block->sub_size);	// read that damn memory dump
}

static int snap_memory_saver ( const struct xemu_snapshot_definition_st *def )
{
	int ret = xemusnap_write_block_header(def->idstr, 0);
	if (ret) return ret;
	return xemusnap_write_sub_block(memory, sizeof memory);
}






static const struct xemu_snapshot_definition_st snapshot_definition[] = {
	{ "CPU:65xx", cpu_snapshot_load_state, cpu_snapshot_save_state },
	{ "Memory", snap_memory_loader, snap_memory_saver },
	{ NULL, NULL, NULL }
};

static void save_now ( void )
{
	if (!savefile)
		return;
	if (xemusnap_save(savefile))
		ERROR_WINDOW("Couldn't save snapshot \"%s\": %s", savefile, xemusnap_error_buffer);
	else
		INFO_WINDOW("Snapshot has been saved into file \"%s\"", savefile);
}


void c65snapshot_init ( const char *load, const char *save )
{
	xemusnap_init(snapshot_definition, "Commodore-65");
	if (load) {
		if (xemusnap_load(load)) {
			ERROR_WINDOW("Couldn't load snapshot \"%s\": %s", load, xemusnap_error_buffer);
			save = NULL;
		} else {
			INFO_WINDOW("Snapshot has been loaded from file \"%s\"", load);
		}
	}
	if (save) {
		savefile = emu_strdup(save);
		atexit(save_now);
	}
}
