/* A work-in-progess Mega-65 (Commodore-65 clone origins) emulator
   Part of the Xemu project, please visit: https://github.com/lgblgblgb/xemu
   Copyright (C)2016,2017 LGB (Gábor Lénárt) <lgblgblgb@gmail.com>

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

#ifndef __XEMU_MEGA65_CPU_CUSTOM_FUNCTIONS_H_INCLUDED
#define __XEMU_MEGA65_CPU_CUSTOM_FUNCTIONS_H_INCLUDED

#ifdef CPU_CUSTOM_MEMORY_FUNCTIONS_H
#define INLINE_DECORATOR static INLINE
#else
#define INLINE_DECORATOR
#endif

#define HANDLERS_ADDR_TYPE int
typedef Uint8 (*mem_page_rd_f_type)(HANDLERS_ADDR_TYPE);
typedef void  (*mem_page_wr_f_type)(HANDLERS_ADDR_TYPE, Uint8);

extern int mem_page_rd_o[];
extern int mem_page_wr_o[];
extern mem_page_rd_f_type mem_page_rd_f[];
extern mem_page_wr_f_type mem_page_wr_f[];


//extern void  cpu_write     ( Uint16 addr, Uint8 data );
extern void  cpu_write_rmw ( Uint16 addr, Uint8 old_data, Uint8 new_data );
//extern Uint8 cpu_read      ( Uint16 addr );
extern void  cpu_write_linear_opcode ( Uint8 data );
extern Uint8 cpu_read_linear_opcode  ( void );

INLINE_DECORATOR Uint8 cpu_read ( Uint16 addr ) {
	return mem_page_rd_f[addr >> 8](mem_page_rd_o[addr >> 8] + (addr & 0xFF));
}
INLINE_DECORATOR void  cpu_write ( Uint16 addr, Uint8 data ) {
	mem_page_wr_f[addr >> 8](mem_page_wr_o[addr >> 8] + (addr & 0xFF), data);
}

#undef INLINE_DECORATOR

#endif
