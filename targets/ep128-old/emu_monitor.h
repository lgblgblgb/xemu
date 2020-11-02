/* Xep128: Minimalistic Enterprise-128 emulator with focus on "exotic" hardware
   Copyright (C)2015,2016 LGB (Gábor Lénárt) <lgblgblgb@gmail.com>
   http://xep128.lgb.hu/

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

#ifndef __XEP128_EMU_MONITOR_H_INCLUDED
#define __XEP128_EMU_MONITOR_H_INCLUDED

extern void monitor_execute ( char *in_input_buffer, int in_source, char *in_output_buffer, int in_output_max_size, const char *in_output_nl );
extern void monitor_process_queued ( void );
extern int  monitor_queue_used ( void );
extern int  monitor_queue_command ( char *buffer );

#endif
