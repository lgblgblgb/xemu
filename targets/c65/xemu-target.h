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


#define TARGET_NAME "c65"
#define TARGET_DESC "Commodore 65"
#define CPU_65CE02
#define CPU65_65CE02_6502NMOS_TIMING_EMULATION
#define XEMU_SNAPSHOT_SUPPORT "Commodore-65"
#define CPU_STEP_MULTI_OPS
#define CPU65 cpu65

#define FAKE_TYPING_SUPPORT
#define C65_FAKE_TYPING_LOAD_SEQS
#define C65_KEYBOARD
#define HID_KBD_MAP_CFG_SUPPORT

#define CONFIG_EMSCRIPTEN_OK

#ifndef XEMU_ARCH_HTML
#	define XEMU_USE_LODEPNG
#	define XEMU_FILES_SCREENSHOT_SUPPORT
#	define HAVE_XEMU_EXEC_API
#endif

#define XEMU_CONFIGDB_SUPPORT
#define XEMU_OSD_SUPPORT
#define CONFIG_DROPFILE_CALLBACK

// Workaround for hang causes by trying fast-seriel-IEC by ROM
// These defines are used by xemu/cia6526.c
//#define CIA_IN_SDR_SETS_ICR_BIT3
#define CIA_OUT_SDR_SETS_ICR_BIT3
