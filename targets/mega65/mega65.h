/* A work-in-progess MEGA65 (Commodore-65 clone origins) emulator
   Part of the Xemu project, please visit: https://github.com/lgblgblgb/xemu
   Copyright (C)2016-2021 LGB (Gábor Lénárt) <lgblgblgb@gmail.com>

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

#ifndef XEMU_MEGA65_MEGA65_H_INCLUDED
#define XEMU_MEGA65_MEGA65_H_INCLUDED

// These file names used by the generic Xemu loader. That is, they are searched in
// different directories, ie, most notably, in rom/
// Real M65 would not expect to have eg the ROM, as it can be loaded from the SD-card,
// but it's not the case with Xemu, as you wouldn't have charset either without prior
// loading it (however kickstart can overwrite "C65 ROM" anyway, later)
#define SDCARD_NAME		"@mega65.img"

#define NVRAM_FILE_NAME		"@nvram.bin"
#define UUID_FILE_NAME		"@uuid.bin"

// Used by updater, etc ... base name only, no path info!
#define MEGA65_ROM_NAME		"MEGA65.ROM"
#define MEGA65_ROM_SIZE		0x20000
#define CHAR_ROM_NAME		"CHARROM.ROM"
#define CHAR_ROM_SIZE		0x2000

/* Do *NOT* modify these, as other parts of the emulator currently depends on these values ...
   You can try RENDER_SCALE_QUALITY though with values 0, 1, 2 */
#define SCREEN_FORMAT           SDL_PIXELFORMAT_ARGB8888
#define USE_LOCKED_TEXTURE	1
#define RENDER_SCALE_QUALITY	0

// Default fast clock of M65, in MHz (can be overriden with CLI switch)
#define MEGA65_DEFAULT_FAST_CLOCK	40.0

// Needed CPU cycles for a (PAL) scanline for a given mode.
// For "fast clock", it's calculated, see MEGA65_DEFAULT_FAST_CLOCK
#define CPU_C65_CYCLES_PER_SCANLINE	227
#define CPU_C128_CYCLES_PER_SCANLINE	128
#define CPU_C64_CYCLES_PER_SCANLINE	64

#define SID_CYCLES_PER_SEC	1000000
#define AUDIO_SAMPLE_FREQ	44100

extern void m65mon_show_regs ( void );
extern void m65mon_dumpmem16 ( Uint16 addr );
extern void m65mon_dumpmem28 ( int addr );
extern void m65mon_setmem28  ( int addr, int cnt, Uint8* vals );
extern void m65mon_set_trace ( int m );
extern void m65mon_do_trace  ( void );
#ifdef TRACE_NEXT_SUPPORT
extern void m65mon_next_command ( void );
#endif
extern void m65mon_empty_command ( void );
extern void m65mon_do_trace_c ( void );
extern void m65mon_breakpoint ( int brk );

extern void machine_set_speed ( int verbose );

extern void reset_mega65      ( void );
extern int  reset_mega65_asked( void );

extern int  dump_memory       ( const char *fn );

extern int  refill_c65_rom_from_preinit_cache ( void );

extern int newhack;
// extern unsigned int frames_total_counter;	XXX remove this
extern int register_screenshot_request;
extern Uint8 last_dd00_bits;

#endif
