/* Various D81 access method for F011 core, for Xemu / C65 and M65 emulators.
   Part of the Xemu project, please visit: https://github.com/lgblgblgb/xemu
   Copyright (C)2018 LGB (Gábor Lénárt) <lgblgblgb@gmail.com>

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

#ifndef __XEMU_COMMON_D81ACCESS_H_INCLUDED
#define __XEMU_COMMON_D81ACCESS_H_INCLUDED

#ifndef D81_SIZE
#define D81_SIZE		819200
#endif

#define PRG_MIN_SIZE		16
#define PRG_MAX_SIZE		38000

#define D81ACCESS_EMPTY		0
#define D81ACCESS_IMG		1
#define D81ACCESS_PRG		2
#define D81ACCESS_DIR		4
#define D81ACCESS_CALLBACKS	8
#define D81ACCESS_RO		0x100
#define D81ACCESS_AUTOCLOSE	0x200

typedef int(*d81access_rd_cb_t)(void *buffer, off_t offset, int sector_size);
typedef int(*d81access_wr_cb_t)(void *buffer, off_t offset, int sector_size);

// must be defined by the caller!
extern void d81access_cb_chgmode ( int mode );

extern int  d81access_read_sect  ( Uint8 *buffer, int d81_offset, int sector_size );
extern int  d81access_write_sect ( Uint8 *buffer, int d81_offset, int sector_size );

extern void d81access_init       ( void );
extern int  d81access_get_mode   ( void );
extern void d81access_close      ( void );
extern void d81access_attach_fd  ( int fd, off_t offset, int mode );
extern int  d81access_attach_fsobj ( const char *fn, int mode );
extern void d81access_attach_cb	   ( off_t offset, d81access_rd_cb_t rd_callback, d81access_wr_cb_t wd_callback );

#endif
