/* Test-case for a very simple, inaccurate, work-in-progress Commodore 65 / MEGA65 emulator,
   within the Xemu project. F011 FDC core implementation.
   Copyright (C)2016,2018 LGB (Gábor Lénárt) <lgblgblgb@gmail.com>

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

#ifndef __XEMU_F011_CORE_H_INCLUDED
#define __XEMU_F011_CORE_H_INCLUDED

#ifndef D81_SIZE
#define D81_SIZE 819200
#endif

extern void  fdc_write_reg ( int addr, Uint8 data );
extern Uint8 fdc_read_reg  ( int addr );
extern void  fdc_init      ( Uint8 *cache_set );
extern void  fdc_set_disk  ( int in_have_disk, int in_have_write );

/* must defined by the user */
extern int   fdc_cb_rd_sec ( Uint8 *buffer, int offset );
extern int   fdc_cb_wr_sec ( Uint8 *buffer, int offset );

#ifdef XEMU_SNAPSHOT_SUPPORT
#include "xemu/emutools_snapshot.h"
extern int fdc_snapshot_load_state ( const struct xemu_snapshot_definition_st *def , struct xemu_snapshot_block_st *block );
extern int fdc_snapshot_save_state ( const struct xemu_snapshot_definition_st *def );
#endif

#endif
