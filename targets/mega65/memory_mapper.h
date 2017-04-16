/* A work-in-progess Mega-65 (Commodore-65 clone origins) emulator.
   Copyright (C)2017 LGB (Gábor Lénárt) <lgblgblgb@gmail.com>

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

extern Uint8 memory_dma_source_mreader ( int addr );
extern void  memory_dma_source_mwriter ( int addr, Uint8 data );
extern Uint8 memory_dma_target_mreader ( int addr );
extern void  memory_dma_target_mwriter ( int addr, Uint8 data );
extern Uint8 memory_dma_list_reader    ( int addr );

extern int map_mask, map_offset_low, map_offset_high, map_megabyte_low, map_megabyte_high;
extern int rom_protect, skip_unhandled_mem;
extern Uint8 chip_ram[0x20000], fast_ram[0x20000], colour_ram[0x8000], char_wom[0x1000], hypervisor_ram[0x4001];

#endif
