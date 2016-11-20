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

#include "emutools.h"
#include "emutools_snapshot.h"
#include "emutools_config.h"
#include "mega65.h"
#include "cpu65c02.h"
#include "cia6526.h"
#include "vic3.h"
#include "sid.h"
#include "dmagic.h"
#include "hypervisor.h"
#include "sdcard.h"
#include "m65_snapshot.h"
#include <string.h>

#define M65_MEMORY_BLOCK_VERSION	0

struct memblock_st {
	Uint8	*data;
	int	size;
};


static int snapcallback_memory_loader ( const struct xemu_snapshot_definition_st *def, struct xemu_snapshot_block_st *block )
{
	const struct memblock_st *mem = (const struct memblock_st *)def->user_data;
	if (block->block_version != M65_MEMORY_BLOCK_VERSION || block->sub_counter != 0 || block->sub_size > mem->size)
		RETURN_XSNAPERR_USER("Bad memory block syntax @ %s", def->idstr);
	memset(mem->data, 0xFF, mem->size);
	return xemusnap_read_file(mem->data, block->sub_size);	// read that damn memory dump
}


static int snapcallback_memory_saver ( const struct xemu_snapshot_definition_st *def )
{
	const struct memblock_st *mem = (const struct memblock_st *)def->user_data;
	int ret = xemusnap_write_block_header(def->idstr, M65_MEMORY_BLOCK_VERSION);
	if (ret) return ret;
	ret = mem->size - 1;
	while (ret && mem->data[ret] == 0xFF)
		ret--;
	return xemusnap_write_sub_block(mem->data, ret + 1);
}


#define DEFINE_SNAPSHOT_MEMORY_BLOCK(name, structure) { "MemoryRegion:" name, (void*)&structure, snapcallback_memory_loader, snapcallback_memory_saver }


static const struct memblock_st memblock_126k_ram	= { memory, 126 * 1024 };
static const struct memblock_st memblock_colour_2k_ram	= { memory + 126 * 1024, 2 * 1024 };
static const struct memblock_st memblock_rom		= { memory + 128 * 1024, 128 * 1024 };
static const struct memblock_st memblock_unused_lo	= { memory + 256 * 1024, sizeof(memory) - (256 * 1024) };
static const struct memblock_st memblock_colour_ram	= { colour_ram, sizeof colour_ram };
static const struct memblock_st memblock_char_wom	= { character_rom, sizeof character_rom };
static const struct memblock_st memblock_hypervisor	= { hypervisor_memory, sizeof hypervisor_memory };

const struct xemu_snapshot_definition_st m65_snapshot_definition[] = {
	{ "CPU",   NULL,  cpu_snapshot_load_state, cpu_snapshot_save_state },
	{ "CIA#1", &cia1, cia_snapshot_load_state, cia_snapshot_save_state },
	{ "CIA#2", &cia2, cia_snapshot_load_state, cia_snapshot_save_state },
	{ "VIC-4", NULL,  vic4_snapshot_load_state, vic4_snapshot_save_state },
	{ "M65",   NULL,  m65emu_snapshot_load_state, m65emu_snapshot_save_state },
	{ "SID#1", &sid1, sid_snapshot_load_state, sid_snapshot_save_state },
	{ "SID#2", &sid2, sid_snapshot_load_state, sid_snapshot_save_state },
	{ "DMAgic", NULL, dma_snapshot_load_state, dma_snapshot_save_state },
	{ "SDcard", NULL, sdcard_snapshot_load_state, sdcard_snapshot_save_state },
	DEFINE_SNAPSHOT_MEMORY_BLOCK("RAM:Chip", memblock_126k_ram),
	DEFINE_SNAPSHOT_MEMORY_BLOCK("RAM:Colour2K", memblock_colour_2k_ram),
	DEFINE_SNAPSHOT_MEMORY_BLOCK("ROM", memblock_rom),
	DEFINE_SNAPSHOT_MEMORY_BLOCK("RAM:LoUnused", memblock_unused_lo),
	DEFINE_SNAPSHOT_MEMORY_BLOCK("RAM:Colour", memblock_colour_ram),
	DEFINE_SNAPSHOT_MEMORY_BLOCK("WOM:Char", memblock_char_wom),
	DEFINE_SNAPSHOT_MEMORY_BLOCK("RAM:HyperVisor", memblock_hypervisor),
	{ NULL, NULL, m65emu_snapshot_loading_finalize, NULL }
};

#endif
