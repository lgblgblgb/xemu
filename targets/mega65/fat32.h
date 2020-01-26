/* A work-in-progess Mega-65 (Commodore-65 clone origins) emulator
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

#ifndef __XEMU_FAT32_MEGA65_H_INCLUDED
#define __XEMU_FAT32_MEGA65_H_INCLUDED

typedef int(*mfat_io_callback_func_t)(Uint32 block, Uint8 *data);

extern void mfat_init ( mfat_io_callback_func_t reader, mfat_io_callback_func_t writer, Uint32 device_size );
extern int mfat_init_mbr ( void );

#endif
