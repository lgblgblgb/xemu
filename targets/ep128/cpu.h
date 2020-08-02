/* Xep128: Minimalistic Enterprise-128 emulator with focus on "exotic" hardware
   Copyright (C)2015,2016,2020 LGB (Gábor Lénárt) <lgblgblgb@gmail.com>
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

#ifndef XEMU_EP128_CPU_H_INCLUDED
#define XEMU_EP128_CPU_H_INCLUDED

#include "xemu/z80.h"

#define CPU_Z80		0
#define CPU_Z80C	1
#define CPU_Z180	2

#define PORT_B6_READ_OTHERS 0xC0

extern void  xep_rom_write_support ( int towrite );
extern void  set_ep_cpu ( int type );
extern int   ep_set_ram_config ( const char *spec );
extern int   ep_init_ram ( void );
extern Uint8 read_cpu_byte ( Uint16 addr );
extern Uint8 read_cpu_byte_by_segmap ( Uint16 addr, Uint8 *segmap );
extern void  write_cpu_byte_by_segmap ( Uint16 addr, Uint8 *segmap, Uint8 data );
extern void  z80_reset ( void );
extern void  ep_reset ( void );


extern int CPU_CLOCK;
extern Z80EX_CONTEXT z80ex;
extern Uint8 memory[0x400000];
extern Uint8 ports[0x100] VARALIGN;
extern const char *memory_segment_map[0x100];
extern int nmi_pending;
extern char *mem_desc;

extern const char *memory_segment_map[0x100];
extern const char ROM_SEGMENT[];
extern const char XEPROM_SEGMENT[];
extern const char RAM_SEGMENT[];
extern const char VRAM_SEGMENT[];
extern const char SRAM_SEGMENT[];
extern const char UNUSED_SEGMENT[];

#endif
