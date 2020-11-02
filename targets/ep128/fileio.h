/* Minimalistic Enterprise-128 emulator with focus on "exotic" hardware
   Part of the Xemu project, please visit: https://github.com/lgblgblgb/xemu
   Copyright (C)2015-2016,2020 LGB (Gábor Lénárt) <lgblgblgb@gmail.com>

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

#ifndef XEMU_EP128_FILEIO_H_INCLUDED
#define XEMU_EP128_FILEIO_H_INCLUDED

extern char  fileio_cwd[PATH_MAX + 1];

extern void fileio_init ( const char *dir, const char *subdir );

/* Internal functions between emu ROM interface and fileio: */
extern void fileio_func_not_used_call( void );
extern void fileio_func_open_or_create_channel ( int create );
extern void fileio_func_open_channel_remember( void );
extern void fileio_func_close_channel( void );
extern void fileio_func_destroy_channel(void );
extern void fileio_func_read_character(void );
extern void fileio_func_read_block(void );
extern void fileio_func_write_character( void );
extern void fileio_func_write_block( void );
extern void fileio_func_channel_read_status( void );
extern void fileio_func_set_channel_status( void );
extern void fileio_func_special_function( void );
extern void fileio_func_init( void );
extern void fileio_func_buffer_moved( void );

#endif
