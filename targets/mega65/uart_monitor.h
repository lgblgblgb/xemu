/* A work-in-progess MEGA65 (Commodore-65 clone origins) emulator
   Part of the Xemu project, please visit: https://github.com/lgblgblgb/xemu
   Copyright (C)2016-2021 LGB (Gábor Lénárt) <lgblgblgb@gmail.com>

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

#ifdef HAS_UARTMON_SUPPORT

#define UMON_SYNTAX_ERROR "?SYNTAX ERROR  "

#define UMON_DEFAULT_PORT ":4510"

// seems as though m65.c actions like fetch_ram(0xFFF8000, 0x4000, hyppo_data)
// cause a lot of umon_writes to accumulate quickly, so had to increase this buffer
#define UMON_WRITE_BUFFER_SIZE	0x10000

extern void umon_printf(const char* format, ...);

extern int  uartmon_init   ( const char *fn );
extern int  uartmon_is_active ( void );
extern void uartmon_update ( void );
extern void uartmon_close  ( void );
extern void uartmons_finish_command ( void );
extern void set_umon_send_ok(int val);

#endif

#endif
