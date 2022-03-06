/* Various D81 access method for F011 core, for Xemu / C65 and M65 emulators.
   Part of the Xemu project, please visit: https://github.com/lgblgblgb/xemu
   Copyright (C)2016-2022 LGB (Gábor Lénárt) <lgblgblgb@gmail.com>

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

#ifndef XEMU_COMMON_D81ACCESS_H_INCLUDED
#define XEMU_COMMON_D81ACCESS_H_INCLUDED

#define D81_SIZE		819200

#define PRG_MIN_SIZE		16

#ifdef	PRG_MAX_SIZE_OVERRIDE
#define PRG_MAX_SIZE		PRG_MAX_SIZE_OVERRIDE
#else
#define PRG_MAX_SIZE		0xD700
#endif

#define D81ACCESS_EMPTY		0
#define D81ACCESS_IMG		1
#define D81ACCESS_PRG		2
#define D81ACCESS_DIR		4
#define D81ACCESS_CALLBACKS	8
#define D81ACCESS_RO		0x100
#define D81ACCESS_AUTOCLOSE	0x200
#define D81ACCESS_FAKE64	0x400
#define D81ACCESS_D64		0x800
#define D81ACCESS_D71		0x1000
#define D81ACCESS_D65		0x2000

#if 0
typedef int(*d81access_rd_cb_t)    ( int which, void *buffer, off_t offset, int sector_size );
typedef int(*d81access_wr_cb_t)    ( int which, void *buffer, off_t offset, int sector_size );
extern void d81access_attach_cb	   ( int which, off_t offset, d81access_rd_cb_t rd_callback, d81access_wr_cb_t wd_callback );
#endif

// must be defined by the caller!
extern void d81access_cb_chgmode   ( const int which, const int mode );

extern int  d81access_read_sect    ( const int which, Uint8 *buffer, const Uint8 side, const Uint8 track, const Uint8 sector, const int sector_size );
extern int  d81access_write_sect   ( const int which, Uint8 *buffer, const Uint8 side, const Uint8 track, const Uint8 sector, const int sector_size );

extern void d81access_init         ( void      );
extern int  d81access_get_mode     ( int which );
extern void d81access_close        ( int which );
extern void d81access_close_all    ( void      );
extern void d81access_attach_fd    ( int which, int fd, off_t offset, int mode );
extern int  d81access_attach_fsobj ( int which, const char *fn, int mode );
extern Uint8 *d81access_create_image ( Uint8 *img, const char *diskname, const int name_from_fn );
extern int  d81access_create_image_file ( const char *fn, const char *diskname, const int do_overwrite, const char *cry );

#endif
