/* A work-in-progess MEGA65 (Commodore 65 clone origins) emulator
   Part of the Xemu project, please visit: https://github.com/lgblgblgb/xemu
   Copyright (C)2016-2020 LGB (Gábor Lénárt) <lgblgblgb@gmail.com>

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

#ifndef XEMU_MEGA65_SDCARD_H_INCLUDED
#define XEMU_MEGA65_SDCARD_H_INCLUDED

#define SD_ST_EXT_BUS	0x80
#define SD_ST_ERROR	0x40
#define SD_ST_FSM_ERROR	0x20
// 0x10 was the SDHC bit, but now Mega65 does not support non-SDHC cards!
#define SD_ST_MAPPED	0x08
#define SD_ST_RESET	0x04
#define SD_ST_BUSY1	0x02
#define SD_ST_BUSY0	0x01

extern int    sdcard_init           ( const char *fn, const char *extd81fn, int virtsd_flag );
extern Uint32 sdcard_get_size       ( void );
extern void   sdcard_write_register ( int reg, Uint8 data );
extern Uint8  sdcard_read_register  ( int reg  );

extern int    sdcard_read_block     ( Uint32 block, Uint8 *buffer );
extern int    sdcard_write_block    ( Uint32 block, Uint8 *buffer );

extern int    mount_external_d81    ( const char *name, int force_ro );

#define SD_BUFFER_POS 0x0E00
#define FD_BUFFER_POS 0x0C00

#define sd_buffer	(disk_buffers+SD_BUFFER_POS)

// disk buffer for SD (can be mapped to I/O space too), F011, and some "3.5K scratch space"
extern Uint8  disk_buffers[0x1000];
extern Uint8  sd_status;

extern int fd_mounted;

extern const char xemu_external_d81_signature[];

static inline int has_block_nonzero_byte ( const Uint8 *p )
{
	for (register unsigned int a = 0; a < 512; a++)
		if (p[a])
			return 1;
	return 0;
}

#ifdef XEMU_SNAPSHOT_SUPPORT
#include "xemu/emutools_snapshot.h"
extern int sdcard_snapshot_load_state ( const struct xemu_snapshot_definition_st *def , struct xemu_snapshot_block_st *block );
extern int sdcard_snapshot_save_state ( const struct xemu_snapshot_definition_st *def );
#endif

#endif
