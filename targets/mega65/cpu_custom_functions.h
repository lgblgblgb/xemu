/* A work-in-progess MEGA65 (Commodore 65 clone origins) emulator
   Part of the Xemu project, please visit: https://github.com/lgblgblgb/xemu
   Copyright (C)2016-2024 LGB (Gábor Lénárt) <lgblgblgb@gmail.com>

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
#	define CPU_CUSTOM_FUNCTIONS_INLINE_DECORATOR static XEMU_INLINE
#else
#	ifndef ALLOW_CPU_CUSTOM_FUNCTIONS_INCLUDE
#		error "cpu_custom_functions.h must not be included by anything other than the CPU emulator and memory_mapper.c"
#	endif
#	define CPU_CUSTOM_FUNCTIONS_INLINE_DECORATOR
#endif

#define MEM_SLOT_RD_ARGLIST	const Uint32 slot, const Uint8 ofs8
#define MEM_SLOT_WR_ARGLIST	const Uint32 slot, const Uint8 ofs8, const Uint8 data
typedef Uint8 (*mem_slot_rd_func_t)(MEM_SLOT_RD_ARGLIST);
typedef void  (*mem_slot_wr_func_t)(MEM_SLOT_WR_ARGLIST);

#define MEM_SLOTS_TOTAL_EXPORTED 0x106
extern mem_slot_rd_func_t mem_slot_rd_func[MEM_SLOTS_TOTAL_EXPORTED];
extern mem_slot_wr_func_t mem_slot_wr_func[MEM_SLOTS_TOTAL_EXPORTED];

extern int cpu_rmw_old_data;

extern void   cpu65_write_linear_opcode_callback ( Uint8 data );
extern Uint8  cpu65_read_linear_opcode_callback  ( void );
extern void   cpu65_write_linear_long_opcode_callback ( const Uint8 index, Uint32 data );
extern Uint32 cpu65_read_linear_long_opcode_callback  ( const Uint8 index );

extern void  cpu65_illegal_opcode_callback ( void );

#define memory_cpurd2linear_xlat(_cpu_addr) memory_cpu_addr_to_linear(_cpu_addr,NULL)

static XEMU_INLINE Uint8 cpu65_read_callback ( const Uint16 addr ) {
	return mem_slot_rd_func[addr >> 8](addr >> 8, addr & 0xFFU);
}
static XEMU_INLINE void cpu65_write_callback ( const Uint16 addr, const Uint8 data ) {
	mem_slot_wr_func[addr >> 8](addr >> 8, addr & 0xFFU, data);
}

// Called in case of an RMW (read-modify-write) opcode write access.
// Original NMOS 6502 would write the old_data first, then new_data.
// It has no inpact in case of normal RAM, but it *does* with an I/O register in some cases!
// CMOS line of 65xx (probably 65CE02 as well?) seems not write twice, but read twice.
// However this leads to incompatibilities, as some software used the RMW behavour by intent.
// Thus MEGA65 fixed the problem to "restore" the old way of RMW behaviour.
// I also follow this path here, even if it's *NOT* what 65CE02 would do actually!
static XEMU_INLINE void cpu65_write_rmw_callback ( const Uint16 addr, const Uint8 old_data, const Uint8 new_data ) {
	cpu_rmw_old_data = old_data;
	// It's the backend's (which realizes the op) responsibility to handle or not handle the RMW behaviour,
	// based on the fact if cpu_rmw_old_data is non-negative (being an int type) when it holds the "old_data".
	mem_slot_wr_func[addr >> 8](addr >> 8, addr, new_data);
	cpu_rmw_old_data = -1;
}

#endif
