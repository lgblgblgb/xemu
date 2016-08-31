/* Test-case for a very simple and inaccurate Commodore VIC-20 emulator using SDL2 library.
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

#ifndef __LGB_CPU65C02_H_INCLUDED
#define __LGB_CPU65C02_H_INCLUDED

extern int cpu_irqLevel;
extern int cpu_nmiEdge;

extern Uint16 cpu_pc, cpu_old_pc;
extern Uint8  cpu_op;

extern Uint8 cpu_a, cpu_x, cpu_y, cpu_sp;
extern int cpu_pfn,cpu_pfv,cpu_pfb,cpu_pfd,cpu_pfi,cpu_pfz,cpu_pfc;
#ifdef CPU_65CE02
extern int cpu_pfe;
extern Uint8 cpu_z;
extern int cpu_inhibit_interrupts;
extern Uint16 cpu_bphi;	// NOTE: it must store the value shifted to the high byte!
extern Uint16 cpu_sphi;	// NOTE: it must store the value shifted to the high byte!
#endif

extern void  cpu_write     ( Uint16 addr, Uint8 data );
extern void  cpu_write_rmw ( Uint16 addr, Uint8 old_data, Uint8 new_data );
extern Uint8 cpu_read      ( Uint16 addr );
#ifdef MEGA65
extern void  cpu_write_linear_opcode ( Uint8 data );
#endif

extern void cpu_reset ( void );
extern int  cpu_step  ( void );

#ifdef CPU_TRAP
extern int  cpu_trap ( Uint8 opcode );
#endif
#ifdef CPU_65CE02
extern void cpu_do_aug ( void );
extern void cpu_do_nop ( void );
#endif

extern void  cpu_set_p  ( Uint8 st );
extern Uint8 cpu_get_p ( void );

#endif
