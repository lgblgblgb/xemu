/* A work-in-progess MEGA65 (Commodore-65 clone origins) emulator
   Part of the Xemu project, please visit: https://github.com/lgblgblgb/xemu
   Copyright (C)2016-2026 LGB (Gábor Lénárt) <lgblgblgb@gmail.com>

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

#ifndef XEMU_MEGA65_UMON_H_INCLUDED
#define XEMU_MEGA65_UMON_H_INCLUDED
#ifdef  HAVE_XEMU_UMON

#define UMON_DEFAULT_PORT 4503

extern int  umon_init ( const int port );
extern int  umon_stop ( void );
extern void umon_main_iterate ( void );
extern bool umon_execute_command ( char *output, unsigned int output_sizeof, const char *input_raw, unsigned int input_size );
extern void umon_format_answer_text ( char *output );
extern int  umon_get_port ( void );

#endif
#endif
