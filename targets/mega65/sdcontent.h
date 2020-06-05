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

#ifndef XEMU_MEGA65_SDCONTENT_H_INCLUDED
#define XEMU_MEGA65_SDCONTENT_H_INCLUDED
#ifdef SD_CONTENT_SUPPORT

// You should not use multiple *_FDISK and *_FILES options!

// FORCE fdisk/format whatever the image is valid or not!
#define SDCONTENT_FORCE_FDISK		1
// ASK fdisk/format *IF* image seems to be not valid, and do it on user's prompt
#define SDCONTENT_ASK_FDISK		2
// Ask the user to correct missing system files needed, and do according to the answer [only if FS could be checked, see *_FDISK options]
#define	SDCONTENT_ASK_FILES		4
// Always check for missing files and do put them without question [only if FS could be checked, see *_FDISK options]
#define SDCONTENT_DO_FILES		8
// Even OVERWRITE existing files without questions, all of them! [only if FS could be checked, see *_FDISK options]
#define SDCONTENT_OVERWRITE_FILES	16
// For INTERNAL user!!
#define SDCONTENT_SYS_FILE		32


extern int sdcontent_handle ( Uint32 size_in_blocks, const char *update_dir_path, int options );


#endif
#endif
