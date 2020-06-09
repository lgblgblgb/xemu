/* Test-case for a very simple, inaccurate, work-in-progress Commodore 65 emulator.
   Copyright (C)2016-2020 LGB (Gábor Lénárt) <lgblgblgb@gmail.com>

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

#ifndef XEMU_COMMODORE65_COMMODORE_65_H_INCLUDED
#define XEMU_COMMODORE65_COMMODORE_65_H_INCLUDED

/* Do *NOT* modify these, as other parts of the emulator currently depends on these values ...
   You can try RENDER_SCALE_QUALITY though with values 0, 1, 2 */
#define SCREEN_FORMAT           SDL_PIXELFORMAT_ARGB8888
#define USE_LOCKED_TEXTURE	1
#define RENDER_SCALE_QUALITY	1

#define FAST_CPU_CYCLES_PER_SCANLINE	227
#define SLOW_CPU_CYCLES_PER_SCANLINE	64

#define SID_CYCLES_PER_SEC	1000000
#define AUDIO_SAMPLE_FREQ	44100

// Note: this is a hack, maybe not standard! No REC (Ram Expansion Controller) emulation is involved here ...
// it's merely just that the upper 512K of the 1Mbyte addressing space is "free" and can be handled (maybe in a non standard way!) as RAM too ...
// It may cause incompatibilities (ie: real REC would allow VIC3 to access REC as well, I think ... It's not the case here. And it's just one example I know about)
#define ALLOW_512K_RAMEXP

// It's *another* entity like the above, but now at the place where "expansion *ROM*" should be ... It's maybe even *worse* ... [??]
// Combined with the 512K, the total RAM size would be 128K (base) + 512K + 256K = 896K, quite nice from a 8 bit machine
// Enable only at your own risk!
//#define ALLOW_256K_RAMEXP


// If defined, a file name string must be used.
// Then RAM content (low 128K) will be written into this file on exit.
//#define MEMDUMP_FILE		"dump.mem"


extern Uint8 memory[0x100000];
extern char emulator_speed_title[];

extern void  apply_memory_config ( void );
extern Uint8 io_read  ( int addr );
extern void  io_write ( int addr, Uint8 data );
extern void  write_phys_mem ( int addr, Uint8 data );
extern Uint8 read_phys_mem  ( int addr );
extern void  c65_reset ( void );
extern void  c65_reset_asked ( void );

#endif
