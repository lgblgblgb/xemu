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

#ifndef __XEMU_C65_SNAPSHOT_H_INCLUDED
#define __XEMU_C65_SNAPSHOT_H_INCLUDED

#ifdef XEMU_SNAPSHOT_SUPPORT
#include "emutools_snapshot.h"

// From other modules ...
extern struct Cia6526 cia1, cia2;
extern struct SidEmulation sids[2];
extern int c65emu_snapshot_load_state ( const struct xemu_snapshot_definition_st *def, struct xemu_snapshot_block_st *block );
extern int c65emu_snapshot_save_state ( const struct xemu_snapshot_definition_st *def );
extern int c65emu_snapshot_loading_finalize   ( const struct xemu_snapshot_definition_st *def, struct xemu_snapshot_block_st *block );

// From our .c file
extern const struct xemu_snapshot_definition_st c65_snapshot_definition[];

#endif
#endif
