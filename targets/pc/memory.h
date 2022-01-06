/* Test-case for a primitive PC emulator inside the Xemu project,
   currently using Fake86's x86 CPU emulation.
   Part of the Xemu project, please visit: https://github.com/lgblgblgb/xemu
   Copyright (C)2016-2022 LGB (Gábor Lénárt) <lgblgblgb@gmail.com>

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

#ifndef XEMU_PC_MEMORY_H_INCLUDED
#define XEMU_PC_MEMORY_H_INCLUDED

extern void memory_init ( void );
extern void memory_save ( const char *fn );

#define MEMORY_MAX 0x110000

#define MEMORY_64KPAGE_MAX ((MEMORY_MAX) >> 16)

// I/O access

extern void     portout   ( const uint16_t portnum, const uint8_t value );
extern void     portout16 ( const uint16_t portnum, const uint16_t value );
extern uint8_t  portin    ( const uint16_t portnum);
extern uint16_t portin16  ( const uint16_t portnum);

// For memory access decoding:

extern uint32_t memtop;	// the byte AFTER the last available byte in the memory space
extern uint8_t *memory_rd_data_ptr_tab[MEMORY_64KPAGE_MAX];
extern uint8_t *memory_wr_data_ptr_tab[MEMORY_64KPAGE_MAX];
extern uint8_t (*memory_rd_func_ptr_tab[MEMORY_64KPAGE_MAX])(const uint16_t);
extern void    (*memory_wr_func_ptr_tab[MEMORY_64KPAGE_MAX])(const uint16_t, const uint8_t);

#if (MEMORY_MAX) & 0xFFFF != 0
#error "MEMORY_MAX must be a 64K aligned number!"
#endif


static inline uint8_t read86 ( const uint32_t addr32 )
{
#if MEMORY_MAX > 0x110000
	if (XEMU_UNLIKELY(addr32 >= memtop))
		return 0xFF;
#endif
	register Uint8 *data_ptr = memory_rd_data_ptr_tab[addr32 >> 16];
	if (XEMU_LIKELY(data_ptr))
		return *(data_ptr + addr32);	// NOTE: data_ptr's are offset'ed thus it's ok to just add the linear address here!
	return memory_rd_func_ptr_tab[addr32 >> 16](addr32 & 0xFFFF);
}


static inline void write86 ( const uint32_t addr32, const uint8_t value )
{
#if MEMORY_MAX > 0x110000
	if (XEMU_UNLIKELY(addr32 >= memtop))
		return;
#endif
	register Uint8 *data_ptr = memory_wr_data_ptr_tab[addr32 >> 16];
	if (XEMU_LIKELY(data_ptr))
		*(data_ptr + addr32) = value;	// NOTE: data_ptr's are offset'ed thus it's ok to just add the linear address here!
	else
		memory_wr_func_ptr_tab[addr32 >> 16](addr32 & 0xFFFF, value);
}


static inline uint16_t readw86 ( const uint32_t addr32 )
{
	return (uint16_t)read86(addr32) | (uint16_t)(read86(addr32 + 1) << 8);
}


static inline void writew86 ( const uint32_t addr32, const uint16_t value )
{
	write86(addr32, (uint8_t)value);
	write86(addr32 + 1, (uint8_t)(value >> 8));
}

#endif
