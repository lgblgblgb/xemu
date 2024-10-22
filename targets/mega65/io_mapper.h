/* A work-in-progess MEGA65 (Commodore-65 clone origins) emulator
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

#ifndef XEMU_MEGA65_IO_MAPPER_H_INCLUDED
#define XEMU_MEGA65_IO_MAPPER_H_INCLUDED

#include "xemu/cia6526.h"

// Hardware errata level what Xemu supports at max.
#define HW_ERRATA_MAX_LEVEL	2

// Default HW_ERRATA_RESET_LEVEL when calling reset_hw_errata_level()
#define HW_ERRATA_RESET_LEVEL	0

extern Uint8 io_read  ( unsigned int addr );
extern void  io_write ( unsigned int addr, Uint8 data );
extern void  set_hw_errata_level   ( const Uint8 desired_level, const char *reason );
extern void  reset_hw_errata_level ( void );

extern Uint8  D6XX_registers[0x100];
extern Uint8  D7XX[0x100];			// FIXME: newhack!
extern int    fpga_switches;
extern struct Cia6526 cia1, cia2;		// CIA emulation structures for the two CIAs
extern int    port_d607;			// ugly hack for C65 extended keys ...
extern int    core_age_in_days;

#endif
