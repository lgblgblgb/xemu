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

#ifndef __XEP128_EMU_ROM_INTERFACE_H_INCLUDED
#define __XEP128_EMU_ROM_INTERFACE_H_INCLUDED

#define XEP_ERROR_CODE 1

extern Uint8 exos_version;
extern Uint8 exos_info[8];

extern void exos_get_status_line ( char *buffer );
extern void xep_set_time_consts ( char *descbuffer );
extern void xep_set_default_device_name ( const char *name );
extern void xep_rom_trap ( Uint16 pc, Uint8 opcode );
extern void xep_set_error ( const char *msg );

#define XEP_SET_ERROR(...) do { \
	char __xep_error_format_buffer__[64]; \
	snprintf(__xep_error_format_buffer__, sizeof __xep_error_format_buffer__, __VA_ARGS__); \
	xep_set_error(__xep_error_format_buffer__); \
} while(0)

#endif
