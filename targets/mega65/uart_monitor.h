/* A work-in-progess MEGA65 (Commodore-65 clone origins) emulator
   Part of the Xemu project, please visit: https://github.com/lgblgblgb/xemu
   Copyright (C)2016,2020 LGB (Gábor Lénárt) <lgblgblgb@gmail.com>

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

#ifndef XEMU_MEGA65_UART_MONITOR_H_INCLUDED
#define XEMU_MEGA65_UART_MONITOR_H_INCLUDED

#if !defined(XEMU_ARCH_WIN) && !defined(XEMU_ARCH_HTML)
#define HAS_UARTMON_SUPPORT

#define SYNTAX_ERROR "?SYNTAX ERROR  "

#define UMON_WRITE_BUFFER_SIZE	0x4000
#define umon_printf(...)	umon_write_size += sprintf(umon_write_buffer + umon_write_size, __VA_ARGS__)

extern int  umon_write_size;
extern int  umon_send_ok;
extern char umon_write_buffer[UMON_WRITE_BUFFER_SIZE];

extern int  uartmon_init   ( const char *fn );
extern void uartmon_update ( void );
extern void uartmon_close  ( void );
extern void uartmon_finish_command ( void );

#endif

#endif
