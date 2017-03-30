/* Very primitive emulator of Commodore 65 + sub-set (!!) of Mega65 fetures.
   Copyright (C)2016 LGB (Gábor Lénárt) <lgblgblgb@gmail.com>

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

#ifndef __XEMU_UART_MONITOR_MEGA65_H_INCLUDED
#define __XEMU_UART_MONITOR_MEGA65_H_INCLUDED

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

extern void reset_machine(void);

extern int  m65mon_update ( void );
extern void m65mon_show_regs(void);
extern void m65mon_dumpmem16(Uint16 addr);
extern void m65mon_dumpmem16_bulk(Uint16 addr);
extern void m65mon_dumpmem24 ( Uint32 addr );
extern void m65mon_dumpmem24_bulk ( Uint32 addr );
extern void m65mon_storemem24 ( Uint32 addr,char * values );
extern void m65mon_do_trace(void);
extern void m65mon_do_trace_c(void);
extern void m65mon_set_trace(int n);
extern void m65mon_breakpoint(int brk);
extern void m65mon_do_reset(void);
extern void m65mon_empty_command(void); // emulator can use it, if it wants


extern const char emulator_paused_title[];

#endif
