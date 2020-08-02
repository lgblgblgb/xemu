/* A work-in-progess MEGA65 (Commodore 65 clone origins) emulator
   Part of the Xemu project, please visit: https://github.com/lgblgblgb/xemu
   Copyright (C)2017,2018 LGB (Gábor Lénárt) <lgblgblgb@gmail.com>

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

#ifndef __XEMU_MEGA65_MEMORY_MAPPER_H_INCLUDED
#define __XEMU_MEGA65_MEMORY_MAPPER_H_INCLUDED

extern void memory_init ( void );
extern void memory_set_do_map ( void );
extern void memory_set_vic3_rom_mapping ( Uint8 value );
extern void memory_set_cpu_io_port ( int addr, Uint8 value );
extern void memory_set_cpu_io_port_ddr_and_data ( Uint8 p0, Uint8 p1 );
extern Uint8 memory_get_cpu_io_port ( int addr );

extern Uint8 memory_debug_read_phys_addr  ( int addr );
extern void  memory_debug_write_phys_addr ( int addr, Uint8 data );
extern Uint8 memory_debug_read_cpu_addr   ( Uint16 addr );
extern void  memory_debug_write_cpu_addr  ( Uint16 addr, Uint8 data );

//#define SIZEOF_CHIP_RAM  0x20000
//#define SIZEOF_FAST_RAM  0x20000
//#define SIZEOF_EXTRA_RAM 0x20000

extern int map_mask, map_offset_low, map_offset_high, map_megabyte_low, map_megabyte_high;
extern int rom_protect, skip_unhandled_mem;
extern Uint8 main_ram[512 << 10], colour_ram[0x8000], char_wom[0x2000], hypervisor_ram[0x4000];
//extern Uint8 chip_ram[SIZEOF_CHIP_RAM], fast_ram[SIZEOF_FAST_RAM];
// Ugly hack for more RAM!
//#define chip_ram  (main_ram + 0)
//#define fast_ram  (main_ram + 0x20000)
//#define extra_ram (main_ram + 0x40000)

extern int cpu_rmw_old_data;

#endif
