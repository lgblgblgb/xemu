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

extern void  cpu_write(Uint16 addr, Uint8 data);
extern Uint8 cpu_read(Uint16 addr);

extern void cpu_reset(void);
extern int  cpu_step (void);

extern int  cpu_trap (Uint8 opcode);

#endif
