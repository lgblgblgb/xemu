/* A work-in-progess MEGA65 (Commodore 65 clone origins) emulator
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

#define TARGET_NAME "mega65"
#define TARGET_DESC "MEGA65"
#define CPU_65CE02
#define MEGA65
#define CPU65_65CE02_6502NMOS_TIMING_EMULATION
//#define XEMU_SNAPSHOT_SUPPORT "MEGA65"
#define CPU_STEP_MULTI_OPS
//#define DEBUG_CPU
#define CPU_CUSTOM_MEMORY_FUNCTIONS_H "cpu_custom_functions.h"
#define CPU65 cpu65
//#define CPU65_DISCRETE_PF_NZ

// #define DO_NOT_FORCE_UNREACHABLE

#define HAVE_XEMU_EXEC_API

#ifdef XEMU_HAS_SOCKET_API
#define HAS_UARTMON_SUPPORT
#define HAVE_XEMU_UMON
#endif
#define HAVE_XEMU_INSTALLER

#ifndef XEMU_ARCH_HTML
#define CONFIG_DROPFILE_CALLBACK
#define VIRTUAL_DISK_IMAGE_SUPPORT
//#define CBM_BASIC_TEXT_SUPPORT
#define SD_CONTENT_SUPPORT
#endif

//#define TRACE_NEXT_SUPPORT

/* Globally: XEMU_INLINE hints gcc to always inline a function. Using this define switches that behaviour off, defaulting to standard "inline" (as it would be without using gcc as well) */
//#define DO_NOT_FORCE_INLINE

// CPU emulation has always has these (originally NMOS) bugs, regardless of the CPU mode (1 = yes, 0 = no-or-mode-dependent)
#define M65_CPU_ALWAYS_BUG_JMP_INDIRECT			0
#define M65_CPU_ALWAYS_BUG_NO_RESET_PFD_ON_INT		0
#define M65_CPU_ALWAYS_BUG_BCD				0
// CPU emulation has only these NMOS-only bugs, if the CPU is in NMOS-persona mode (1=yes-only-in-nmos, 0=ALWAYS-setting-counts-for-this-bug-not-this-setting)
// To be able to use these, the corresponding ALWAYS setting above should be 0!
#define M65_CPU_NMOS_ONLY_BUG_JMP_INDIRECT		1
#define M65_CPU_NMOS_ONLY_BUG_NO_RESET_PFD_ON_INT	1
#define M65_CPU_NMOS_ONLY_BUG_BCD			1

// Currently only Linux-TAP device is supported to have emulated ethernet controller
// Also it seems ARM Raspbian/etc does have problem with ethertap, so let's not allow
// ethertap for ARM CPU, it's faulty there!
#if defined(XEMU_ARCH_LINUX) && !defined(XEMU_CPU_ARM)
#define HAVE_ETHERTAP
#endif

#define FAKE_TYPING_SUPPORT
#define C65_FAKE_TYPING_LOAD_SEQS
#define C65_KEYBOARD
#define HID_KBD_MAP_CFG_SUPPORT

#ifndef XEMU_ARCH_HTML
#define XEMU_USE_LODEPNG
#define XEMU_FILES_SCREENSHOT_SUPPORT
#endif

#define CONFIG_EMSCRIPTEN_OK

// Needed for the stub-ROM, also matrix-mode uses it
#define XEMU_VGA_FONT_8X8

#define XEMU_CONFIGDB_SUPPORT
#define XEMU_OSD_SUPPORT

// Workaround for hang causes by trying fast-seriel-IEC by ROM
// These defines are used by xemu/cia6526.c
//#define CIA_IN_SDR_SETS_ICR_BIT3
#define CIA_OUT_SDR_SETS_ICR_BIT3
