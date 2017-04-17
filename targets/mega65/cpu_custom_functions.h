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

/* This source defines the memory access primitives used by the CPU emulator.
   Do *NOT* include this header, it's intended for the CPU emulator, and
   for memory_mapper.c. Some dual-mode solution is here, ie it can support
   in-lined functions and "normal" ones. You must edit xemu-target.h for
   that, commeting in/out the line with CPU_CUSTOM_MEMORY_FUNCTIONS_H #define */

#ifndef __XEMU_MEGA65_CPU_CUSTOM_FUNCTIONS_H_INCLUDED
#define __XEMU_MEGA65_CPU_CUSTOM_FUNCTIONS_H_INCLUDED

#ifdef CPU_CUSTOM_MEMORY_FUNCTIONS_H
#define CPU_CUSTOM_FUNCTIONS_INLINE_DECORATOR static INLINE
#else
#define CPU_CUSTOM_FUNCTIONS_INLINE_DECORATOR
#endif

#if 1
#define CALL_MEMORY_READER(slot,addr)		mem_page_rd_f[slot](mem_page_rd_o[slot] + ((addr) & 0xFF))
#define CALL_MEMORY_WRITER(slot,addr,data)	mem_page_wr_f[slot](mem_page_wr_o[slot] + ((addr) & 0xFF), data)
#define SAVE_USED_SLOT(slot)			last_slot_ref = slot
#define MEMORY_HANDLERS_ADDR_TYPE		int area_offset
#define GET_READER_OFFSET()			area_offset
#define	GET_WRITER_OFFSET()			area_offset
#define GET_OFFSET_BYTE_ONLY()			area_offset
#define GET_USED_SLOT()				last_slot_ref
#endif

#if 0
#define CALL_MEMORY_READER(slot,addr)		mem_page_rd_f[slot](slot, addr)
#define CALL_MEMORY_WRITER(slot,addr,data)	mem_page_wr_f[slot](slot, addr, data)
#define SAVE_USED_SLOT(slot)
#define MEMORY_HANDLERS_ADDR_TYPE		int slot, Uint8 lo_addr
#define GET_READER_OFFSET()			(mem_page_rd_o[slot] + lo_addr)
#define	GET_WRITER_OFFSET()			(mem_page_wr_o[slot] + lo_addr)
#define GET_OFFSET_BYTE_ONLY()			lo_addr
#define GET_USED_SLOT()				slot
#endif


typedef Uint8 (*mem_page_rd_f_type)(MEMORY_HANDLERS_ADDR_TYPE);
typedef void  (*mem_page_wr_f_type)(MEMORY_HANDLERS_ADDR_TYPE, Uint8 data);

extern int mem_page_rd_o[];
extern int mem_page_wr_o[];
extern mem_page_rd_f_type mem_page_rd_f[];
extern mem_page_wr_f_type mem_page_wr_f[];


//extern void  cpu_write     ( Uint16 addr, Uint8 data );
extern void  cpu_write_rmw ( Uint16 addr, Uint8 old_data, Uint8 new_data );
//extern Uint8 cpu_read      ( Uint16 addr );
extern void  cpu_write_linear_opcode ( Uint8 data );
extern Uint8 cpu_read_linear_opcode  ( void );

CPU_CUSTOM_FUNCTIONS_INLINE_DECORATOR Uint8 cpu_read ( Uint16 addr ) {
	return CALL_MEMORY_READER(addr >> 8, addr);
}
CPU_CUSTOM_FUNCTIONS_INLINE_DECORATOR void  cpu_write ( Uint16 addr, Uint8 data ) {
	CALL_MEMORY_WRITER(addr >> 8, addr, data);
}

#undef CPU_CUSTOM_FUNCTIONS_INLINE_DECORATOR

#endif
