/* This is an odd emulator, emulating a Commodore 64 like machine only for the
   level needed for a special version of GEOS to be able to run on it.
   You should have a really special one with own disk drive etc, since there
   is no hardware support for drive emulation etc, but it's built in the emulator
   with a kind of CPU trap! The purpose: know GEOS better and slowly replace
   more and more functions on C/emulator level, so at one point it's possible
   to write a very own version of GEOS without *any* previously used code in
   the real GEOS. Then it can be even ported to other architectures GEOS wasn't
   mean to run ever.
   ---------------------------------------------------------------------------------
   One interesting plan: write a GEOS emulator which does not use VIC-II bitmapped
   screen anymore, but the GEOS functions mean to be targeted a "modern UI toolkit",
   ie GTK, so a dozens years old (unmodified) GEOS app would be able to run on a PC
   with modern look and feel, ie anti-aliased fonts, whatever ...
   ---------------------------------------------------------------------------------
   Copyright (C)2016 LGB (Gábor Lénárt) <lgblgblgb@gmail.com>
   ---------------------------------------------------------------------------------

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

#include <stdio.h>

#include <SDL.h>

#include "commodore_geos.h"
#include "cpu65c02.h"
#include "geos.h"
#include "emutools.h"



int geos_load_kernal ( void )
{
        if (emu_load_file("geos-kernal.bin", memory + 0x5000 - 2, 0xB000 + 2) > 0x1000) {
                // Ok, it seems we have loaded something!
                cpu_pc = 0x5000;        // execute our loaded stuff
                return 0;
        }
	return 0;
}


void geos_cpu_trap ( Uint8 opcode )
{
	FATAL("FATAL: Maybe GEOS CPU trap, but not supported yet @ PC=$%04X OP=$%02X" NL, cpu_pc, opcode);
}

