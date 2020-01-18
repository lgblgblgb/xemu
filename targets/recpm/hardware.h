/* Re-CP/M: CP/M-like own implementation + Z80 emulator
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

#ifndef __XEMU_RECPM_HARDWARE_H_INCLUDED
#define __XEMU_RECPM_HARDWARE_H_INCLUDED

#include "xemu/z80.h"
#include "xemu/z80_dasm.h"

extern int   emu_cost_cycles, emu_cost_usecs, stop_emulation, cpu_mhz, trace, cpu_cycles, cpu_cycles_per_frame;
extern Uint8 memory[0x10000];
extern Uint8 modded[0x10000];
extern void  emu_mem_write ( int addr, int data );
extern int   emu_mem_read  ( int addr );
extern int   z80_custom_disasm ( int addr, char *buf, int buf_size );

#endif
