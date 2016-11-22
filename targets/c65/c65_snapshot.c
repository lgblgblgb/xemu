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

#ifdef XEMU_SNAPSHOT_SUPPORT

#include "xemu/emutools.h"
#include "xemu/emutools_snapshot.h"
#include "xemu/emutools_config.h"
#include "commodore_65.h"
#include "xemu/cpu65c02.h"
#include "xemu/cia6526.h"
#include "vic3.h"
#include "xemu/sid.h"
#include "c65dma.h"
#include "xemu/f011_core.h"
#include "c65_snapshot.h"
#include <string.h>


#define C65_MEMORY_BLOCK_VERSION	0

static int snapcallback_memory_loader ( const struct xemu_snapshot_definition_st *def, struct xemu_snapshot_block_st *block )
{
	if (block->block_version != C65_MEMORY_BLOCK_VERSION || block->sub_counter != 0 || block->sub_size > sizeof(memory))
		RETURN_XSNAPERR_USER("Bad memory block syntax ver=%d, subcount=%d, size=%d", block->block_version, block->sub_counter, block->sub_size);
	memset(memory, 0xFF, sizeof memory);
	return xemusnap_read_file(memory, block->sub_size);	// read that damn memory dump
}

static int snapcallback_memory_saver ( const struct xemu_snapshot_definition_st *def )
{
	int ret = xemusnap_write_block_header(def->idstr, C65_MEMORY_BLOCK_VERSION);
	if (ret) return ret;
	ret = sizeof memory;
	while (memory[--ret] == 0xFF) ;
	return xemusnap_write_sub_block(memory, ret + 1);
}


/* The heart of the Snapshot handling. For the Xemu level snapshot support (both loading and saving)
   A definition list like this must be created. Each entry defines a block type, a user parameter,
   a load and save callback. Callbacks can be NULL to signal that no need to handle that for load or
   save (that is: it's possible to support loading a block which is never written though on save).
   The last entry MUST have a NULL entry in the place of block-identify string. This last entry can
   specify callbacks too, if it's given it means, that Xemu snapshot handler will call those callbacks
   at the end of loading or saving a snapshot. It's especially useful in case of "load finalization"
   for example, when an emulator needs to execute some extra code to really use the loaded state. The
   user parameter is accessible for the callbacks. Some of these callbacks are common code, ie
   CIA/CPU/SID can be included this way without any emulator-specific snapshot code, and realized
   in the shared/common code base. */
const struct xemu_snapshot_definition_st c65_snapshot_definition[] = {
	{ "CPU",   NULL,  cpu_snapshot_load_state, cpu_snapshot_save_state },
	{ "CIA#1", &cia1, cia_snapshot_load_state, cia_snapshot_save_state },
	{ "CIA#2", &cia2, cia_snapshot_load_state, cia_snapshot_save_state },
	{ "VIC-3", NULL,  vic3_snapshot_load_state, vic3_snapshot_save_state },
	{ "C65",   NULL,  c65emu_snapshot_load_state, c65emu_snapshot_save_state },
	{ "SID#1", &sids[0], sid_snapshot_load_state, sid_snapshot_save_state },
	{ "SID#2", &sids[1], sid_snapshot_load_state, sid_snapshot_save_state },
	{ "DMA", NULL, dma_snapshot_load_state, dma_snapshot_save_state },
	{ "FDC-F011", NULL, fdc_snapshot_load_state, fdc_snapshot_save_state },
	{ "Memory", NULL, snapcallback_memory_loader, snapcallback_memory_saver },
	{ NULL, NULL, c65emu_snapshot_loading_finalize, NULL }
};

#endif
