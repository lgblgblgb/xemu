/* Very primitive emulator of Commodore 65 + sub-set (!!) of Mega65 fetures.
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

#ifndef __XEMU_SDCARD_MEGA65_H_INCLUDED
#define __XEMU_SDCARD_MEGA65_H_INCLUDED

#define SD_ST_HALFSPEED	0x80
#define SD_ST_ERROR	0x40
#define SD_ST_FSM_ERROR	0x20
#define SD_ST_SDHC	0x10
#define SD_ST_MAPPED	0x08
#define SD_ST_RESET	0x04
#define SD_ST_BUSY1	0x02
#define SD_ST_BUSY0	0x01


extern int   sdcard_init           ( const char *fn, const char *extd81fn );
extern void  sdcard_write_register ( int reg, Uint8 data );
extern Uint8 sdcard_read_register  ( int reg  );
extern int   sdcard_read_buffer    ( int addr );
extern int   sdcard_write_buffer   ( int addr,   Uint8 data );

#ifdef XEMU_SNAPSHOT_SUPPORT
#include "xemu/emutools_snapshot.h"
extern int sdcard_snapshot_load_state ( const struct xemu_snapshot_definition_st *def , struct xemu_snapshot_block_st *block );
extern int sdcard_snapshot_save_state ( const struct xemu_snapshot_definition_st *def );
#endif

#endif
