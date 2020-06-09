/* Xemu - Somewhat lame emulation (running on Linux/Unix/Windows/OSX, utilizing
   SDL2) of some 8 bit machines, including the Commodore LCD and Commodore 65
   and MEGA65 as well.
   Copyright (C)2016-2020 LGB (Gábor Lénárt) <lgblgblgb@gmail.com>

| For more information about "cpu65" please also read comments in file cpu65.c |

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

#ifndef XEMU_COMMON_CPU65_H_INCLUDED
#define XEMU_COMMON_CPU65_H_INCLUDED

#define CPU65_PF_N 0x80
#define CPU65_PF_V 0x40
#define CPU65_PF_E 0x20
#define CPU65_PF_B 0x10
#define CPU65_PF_D 0x08
#define CPU65_PF_I 0x04
#define CPU65_PF_Z 0x02
#define CPU65_PF_C 0x01

/* Notes:
	* cpu flags (pf_*) are INT in type, and "logic" types not actual bit mask.
	  this is because using flags are frequent and faster (maybe ... ?) this way,
	  and actual getting of the full flags register is much rare event when we
	  need to "construct" it from individual flag info data.
	  Type is "int" since in some cases they're implemented with > 8 bit data
	  length steps during opcode emulation.
	* if CPU65_DISCRETE_PF_NZ is *NOT* defined:
	  pf_nz is an exception to the scheme above: 65xx CPU family is famous for
	  altering N and Z flags always if a general purpose register is filled,
	  even with eg TXA. So this is the way more frequent opcode flag operation
	  in general. Thus, I included two bits at the right bit position for N and
	  Z but not for the other flags though (see my previous comment).
	* OK, maybe my theory is not the best, and it's not even optimal this way,
	  I have to admit ...
	* 'B' bit (break flag) is not a real processor status flag. it depends on
	  the type of operation what the result is, eg BRK pushes it into the stack as
	  '1', while hardware interrupts as '0'
*/
struct cpu65_st {
	Uint8 a, x, y;
	Uint8 s;
	Uint8 op;
#ifdef MEGA65
	Uint8 previous_op;
	int nmos_mode;
	int neg_neg_prefix;
#endif
#ifdef CPU_65CE02
	Uint8 z;
	Uint16 bphi, sphi;		// NOTE: it must store the value shifted to the high byte!
	int cpu_inhibit_interrupts;
	int pf_e;
#endif
#ifdef CPU65_DISCRETE_PF_NZ
	int pf_n, pf_z;
#else
	Uint8 pf_nz;
#endif
	int pf_c, pf_v, pf_d, pf_i;
	Uint16 pc, old_pc;
	int multi_step_stop_trigger;	// not used, only with multi-op mode but still here because some devices (like DMA) would use it
	int irqLevel, nmiEdge;
	int op_cycles;
};

#ifndef CPU65
#error "CPU65 must be defined."
#endif

extern struct cpu65_st CPU65;

#ifdef MEGA65
extern int  cpu_linear_memory_addressing_is_enabled;
#endif

#ifdef CPU65_65CE02_6502NMOS_TIMING_EMULATION
extern void cpu65_set_ce_timing ( int is_ce );
#endif
//extern int cpu_multi_step_stop_trigger;

#ifndef CPU_CUSTOM_MEMORY_FUNCTIONS_H
extern void  cpu65_write_callback      ( Uint16 addr, Uint8 data );
#ifndef CPU65_NO_RMW_EMULATION
extern void  cpu65_write_rmw_callback  ( Uint16 addr, Uint8 old_data, Uint8 new_data );
#endif
extern Uint8 cpu65_read_callback       ( Uint16 addr );
#ifdef MEGA65
extern void  cpu65_write_linear_opcode_callback ( Uint8 data );
extern Uint8 cpu65_read_linear_opcode_callback  ( void );
#endif
#if defined(CPU_6502_NMOS_ONLY) || defined(MEGA65)
extern void  cpu65_illegal_opcode_callback ( void );
#endif
#else
#include CPU_CUSTOM_MEMORY_FUNCTIONS_H
#endif

extern void cpu65_reset ( void );
extern int  cpu65_step  (
#ifdef CPU_STEP_MULTI_OPS
	int run_for_cycles
#else
	void
#endif
);

#ifdef CPU65_TRAP_OPCODE
extern int  cpu65_trap_callback ( Uint8 opcode );
#endif
#ifdef CPU_65CE02
extern void cpu65_do_aug_callback ( void );
extern void cpu65_do_nop_callback ( void );
#endif

extern void  cpu65_set_pf ( Uint8 st );
extern Uint8 cpu65_get_pf ( void );

#ifdef XEMU_SNAPSHOT_SUPPORT
#include "xemu/emutools_snapshot.h"
extern int cpu65_snapshot_load_state ( const struct xemu_snapshot_definition_st *def, struct xemu_snapshot_block_st *block );
extern int cpu65_snapshot_save_state ( const struct xemu_snapshot_definition_st *def );
#endif

#endif
