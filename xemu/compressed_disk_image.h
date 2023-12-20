/* Part of the Xemu project, please visit: https://github.com/lgblgblgb/xemu
   Copyright (C)2016-2023 LGB (Gábor Lénárt) <lgblgblgb@gmail.com>

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

#ifdef RLE_COMPRESSED_DISK_IMAGE_SUPPORT
#ifndef XEMU_COMMON_COMPRESSED_DISK_IMAGE_H_INCLUDED
#define XEMU_COMMON_COMPRESSED_DISK_IMAGE_H_INCLUDED

struct compressed_diskimage_st {
	int		fd;
	char		*name;
	int		unpack_buffer_size;
	Uint32		*pagedir;
	Uint32		size_in_blocks;
	unsigned int	cached_page;
	Uint8		*page_cache;
};

extern int  compressed_diskimage_detect     ( struct compressed_diskimage_st *info, const int fd, const char *name );
extern int  compressed_diskimage_read_block ( struct compressed_diskimage_st *info, const Uint32 block, Uint8 *buffer );
extern void compressed_diskimage_free       ( struct compressed_diskimage_st *info );

#endif
#endif
