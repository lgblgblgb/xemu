/* A work-in-progess MEGA65 (Commodore 65 clone origins) emulator
   Part of the Xemu project, please visit: https://github.com/lgblgblgb/xemu
   Copyright (C)2016-2019 LGB (Gábor Lénárt) <lgblgblgb@gmail.com>

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
#define CPU_CUSTOM_FUNCTIONS_INLINE_DECORATOR static XEMU_INLINE
#else
#ifndef ALLOW_CPU_CUSTOM_FUNCTIONS_INCLUDE
#error "cpu_custom_functions.h must not be included by anything other than the CPU emulator and memory_mapper.c"
#endif
#define CPU_CUSTOM_FUNCTIONS_INLINE_DECORATOR
#endif

#if 1
#define CALL_MEMORY_READER(slot,addr)		mem_page_rd_f[slot](mem_page_rd_o[slot] + ((addr) & 0xFF))
#define CALL_MEMORY_WRITER(slot,addr,data)	mem_page_wr_f[slot](mem_page_wr_o[slot] + ((addr) & 0xFF), data)
#define CALL_MEMORY_READER_PAGED(slot,addr)		mem_page_rd_f[slot](mem_page_rd_o[slot] + addr)
#define CALL_MEMORY_WRITER_PAGED(slot,addr,data)	mem_page_wr_f[slot](mem_page_wr_o[slot] + addr, data)
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
#define CALL_MEMORY_READER_PAGED(slot,addr)		mem_page_rd_f[slot](slot, addr)
#define CALL_MEMORY_WRITER_PAGED(slot,addr,data)	mem_page_wr_f[slot](slot, addr, data)
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
extern int cpu_rmw_old_data;

extern void   cpu65_write_linear_opcode_callback ( Uint8 data );
extern Uint8  cpu65_read_linear_opcode_callback  ( void );
extern void   cpu65_write_linear_long_opcode_callback ( Uint32 data );
extern Uint32 cpu65_read_linear_long_opcode_callback  ( void );

extern void  cpu65_illegal_opcode_callback ( void );

CPU_CUSTOM_FUNCTIONS_INLINE_DECORATOR Uint8 cpu65_read_callback ( Uint16 addr ) {
	return CALL_MEMORY_READER(addr >> 8, addr);
}
CPU_CUSTOM_FUNCTIONS_INLINE_DECORATOR void  cpu65_write_callback ( Uint16 addr, Uint8 data ) {
	CALL_MEMORY_WRITER(addr >> 8, addr, data);
}
CPU_CUSTOM_FUNCTIONS_INLINE_DECORATOR Uint8 cpu65_read_paged_callback ( Uint8 page, Uint8 addr8 ) {
	return CALL_MEMORY_READER_PAGED(page, addr8);
}
CPU_CUSTOM_FUNCTIONS_INLINE_DECORATOR void  cpu65_write_paged_callback ( Uint8 page, Uint8 addr8, Uint8 data ) {
	CALL_MEMORY_WRITER_PAGED(page, addr8, data);
}
// Called in case of an RMW (read-modify-write) opcode write access.
// Original NMOS 6502 would write the old_data first, then new_data.
// It has no inpact in case of normal RAM, but it *does* with an I/O register in some cases!
// CMOS line of 65xx (probably 65CE02 as well?) seems not write twice, but read twice.
// However this leads to incompatibilities, as some software used the RMW behavour by intent.
// Thus Mega65 fixed the problem to "restore" the old way of RMW behaviour.
// I also follow this path here, even if it's *NOT* what 65CE02 would do actually!
CPU_CUSTOM_FUNCTIONS_INLINE_DECORATOR void  cpu65_write_rmw_callback ( Uint16 addr, Uint8 old_data, Uint8 new_data ) {
	cpu_rmw_old_data = old_data;
	// It's the backend's (which realizes the op) responsibility to handle or not handle the RMW behaviour,
	// based on the fact if cpu_rmw_old_data is non-negative (being an int type) when it holds the "old_data".
	CALL_MEMORY_WRITER(addr >> 8, addr, new_data);
	cpu_rmw_old_data = -1;
}
CPU_CUSTOM_FUNCTIONS_INLINE_DECORATOR void cpu65_write_rmw_paged_callback ( Uint8 page, Uint8 addr8, Uint8 old_data, Uint8 new_data ) {
	cpu_rmw_old_data = old_data;
	CALL_MEMORY_WRITER_PAGED(page, addr8, new_data);
	cpu_rmw_old_data = -1;
}

#undef CPU_CUSTOM_FUNCTIONS_INLINE_DECORATOR

#endif
