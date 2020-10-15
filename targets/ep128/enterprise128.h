/* Minimalistic Enterprise-128 emulator with focus on "exotic" hardware
   Part of the Xemu project, please visit: https://github.com/lgblgblgb/xemu
   Copyright (C)2015-2016,2020 LGB (Gábor Lénárt) <lgblgblgb@gmail.com>

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

#ifndef XEMU_EP128_MAIN_H_INCLUDED
#define XEMU_EP128_MAIN_H_INCLUDED

#define SCREEN_WIDTH		736
#define SCREEN_HEIGHT		288
#define SCREEN_FORMAT		SDL_PIXELFORMAT_ARGB8888
// !!! currently ep128 emu does NOT work if you modify this !!!
#define USE_LOCKED_TEXTURE	0
#define RENDER_SCALE_QUALITY	1

#define DEFAULT_ROM_FN		"#exos.rom"
#define SDCARD_IMG_FN		"@sdcard.img"
#define PRINT_OUT_FN		"@print.out"
#define SRAM_BACKUP_FILE_FORMAT	"@sram-%02X.seg"

#define DEFAULT_CPU_CLOCK	4000000

// Used at various places to name the emulator (in prompts, etc)
#define XEP128_NAME		"XEMU::Xep128"

extern void emu_one_frame ( int rasters, int frameskip);
extern int  set_cpu_clock ( int hz );

extern int paused;
extern time_t unix_time;

#define VARALIGN		MAXALIGNED

#define CONFIG_USE_LODEPNG
#define CONFIG_EXDOS_SUPPORT
#ifdef	XEMU_HAS_SOCKET_API
#	define CONFIG_EPNET_SUPPORT
#	define EPNET_IO_BASE	0x90
#endif

#define ERRSTR()		strerror(errno)

#endif
