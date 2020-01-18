/* Re-CP/M: CP/M-like own implementation + Z80 emulator
   Part of the Xemu project, please visit: https://github.com/lgblgblgb/xemu
   Copyright (C)2016-2019 LGB (Gábor Lénárt) <lgblgblgb@gmail.com>

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

#ifndef __XEMU_RECPM_CPMFS_H_INCLUDED
#define __XEMU_RECPM_CPMFS_H_INCLUDED

// These must be this way, ALWAYS! Only one should be used from the DMA stuffs, and file find functions should return with
// these values (low two bits only) to the BDOS level at least!
#define CPMFS_SF_STORE_IN_DMA0	0x10
#define CPMFS_SF_STORE_IN_DMA1	0x11
#define CPMFS_SF_STORE_IN_DMA2	0x12
#define CPMFS_SF_STORE_IN_DMA3	0x13
// The rest of the options are regular bit mask, though
#define CPMFS_SF_JOKERY		0x20
#define CPMFS_SF_INPUT_IS_FCB	0x40

extern int   current_drive;

extern void  cpmfs_init ( void );
extern void  cpmfs_uninit ( void );
extern void  cpmfs_close_all_files ( void );
extern int   cpmfs_mount_drive ( int drive, const char *dir_path, int dirbase_part_only );
extern char *cpmfs_search_file_get_result_path ( void );
extern int   cpmfs_search_file ( void );
extern int   cpmfs_search_file_setup ( int drive, const Uint8 *input, int options );

#endif
