/* A work-in-progess MEGA65 (Commodore 65 clone origins) emulator
   Part of the Xemu project, please visit: https://github.com/lgblgblgb/xemu
   Copyright (C)2025 LGB (Gábor Lénárt) <lgblgblgb@gmail.com>

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

#ifndef XEMU_MEGA65_SERIALTCP_H_INCLUDED
#define XEMU_MEGA65_SERIALTCP_H_INCLUDED
#ifdef  XEMU_HAS_SOCKET_API

extern int serialtcp_init ( const char *connection );
extern int serialtcp_shutdown ( void );
extern int serialtcp_restart ( const char *connection );
extern bool serialtcp_get_connection_desc ( char *param, const unsigned int param_size, char *desc, const unsigned int desc_size, int *tx, int *rx );
extern const char *serialtcp_get_connection_error ( const bool remove_error );

extern void  serialtcp_write_reg ( const int reg, const Uint8 data );
extern Uint8 serialtcp_read_reg  ( const int reg );

#endif
#endif
