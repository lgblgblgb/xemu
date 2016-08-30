/* Very primitive emulator of Commodore 65 + sub-set (!!) of Mega65 fetures.
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

#ifndef __XEMU_MEGA65_MEGA65_H_INCLUDED
#define __XEMU_MEGA65_MEGA65_H_INCLUDED

// These file names used by the generic Xemu loader. That is, they are searched in
// different directories, ie, most notably, in rom/
// Real M65 would not expect to have eg the ROM, as it can be loaded from the SD-card,
// but it's not the case with Xemu, as you wouldn't have charset either without prior
// loading it (however kickstart can overwrite "C65 ROM" anyway, later)
#define KICKSTART_NAME			"KICKUP.M65"
#define ROM_NAME			"c65-system.rom"
#define SDCARD_NAME			"mega65.img"
#define KICKSTART_LIST_FILE_NAME	"kickstart.list"

// If it's defined, DMA operations will halts the CPU
// NOTE: it seems this is the de-facto behaviour with M65, you should use it!
#define DMA_STOPS_CPU

// If you define this, you will got *VERY HUGE* number of messages into the debug log
// It slows down the emulator *A LOT* as well!
// With FPGA switch 12 too, it created 20Gbytes(!) log file and it even not booted yet :)
#define HYPERVISOR_DEBUG

// State of FPGA board switches (bits 0 - 15), set switch 12 (hypervisor serial output)
// 12 is useful, but it also slows things down
#define FPGA_SWITCHES		1 << 12

// You may want to disable audio emulation since it can disturb non-real-time emulation
#define AUDIO_EMULATION

#define UARTMON_SOCKET		"uart.sock"


/* Do *NOT* modify these, as other parts of the emulator currently depends on these values ...
   You can try RENDER_SCALE_QUALITY though with values 0, 1, 2 */
#define SCREEN_FORMAT           SDL_PIXELFORMAT_ARGB8888
#define USE_LOCKED_TEXTURE	1
#define RENDER_SCALE_QUALITY	1
#define SCREEN_WIDTH		640
#define SCREEN_HEIGHT		200

#define CPU_CLOCK		3546875
#define FULL_FRAME_CPU_CYCLES	141875

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
// #define MEMDUMP_FILE		"dump.mem"


extern Uint8 memory[0x104001];
extern int   mega65_capable;
extern int   in_hypervisor;
extern Uint8 kicked_hypervisor;
extern int map_mask;
extern int map_offset_low;
extern int map_offset_high;
extern Uint8 gs_regs[0x1000];
extern Uint8 cpu_port[2];

extern void  apply_memory_config ( void );
extern Uint8 io_read  ( int addr );
extern void  io_write ( int addr, Uint8 data );
extern void  write_phys_mem ( int addr, Uint8 data );
extern Uint8 read_phys_mem  ( int addr );

extern void m65mon_show_regs ( void );
extern void m65mon_dumpmem16 ( Uint16 addr );
extern void m65mon_set_trace ( int m );
extern void m65mon_do_trace  ( void );
extern void m65mon_empty_command ( void );
extern void m65mon_do_trace_c ( void );
extern void m65mon_breakpoint ( int brk );

#endif
