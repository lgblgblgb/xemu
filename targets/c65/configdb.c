/* Test-case for simple, work-in-progress Commodore 65 emulator.

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

#include "xemu/emutools.h"
#include "xemu/emutools_config.h"
#include "configdb.h"
#include "commodore_65.h"
#include "xemu/emutools_hid.h"
#include "vic3.h"

/* Important WARNING:
 * This is a trap! If you have something here with '#ifdef', it's quite possible that the macro is
 * not defined here, but defined elsewhere, thus the emulator sees totally different structs for
 * real but the problem is hidden! That is, be very careful at configdb_st (the type definition
 * itself also at the usage!) that should be only dependent on macros defined in xemu-target.h,
 * since that header file is always included by the build system, at command line level. */

struct configdb_st configdb;

void configdb_define_emulator_options ( void )
{
	XEMUCFG_DEFINE_STR_OPTIONS(
		{ "8", NULL, "Path of the D81 disk image to be attached as drive#0", &configdb.disk8 },
//		{ "9", NULL, "Path of the D81 disk image to be attached as drive#1", &configdb.disk9 },
		{ "hostfsdir", NULL, "Path of the directory to be used as Host-FS base", &configdb.hostfsdir },
		{ "rom", DEFAULT_ROM_FILE, "Override system ROM path to be loaded", &configdb.rom },
		{ "keymap", KEYMAP_USER_FILENAME, "Set keymap configuration file to be used", &configdb.keymap },
		{ "gui", NULL, "Select GUI type for usage. Specify some insane str to get a list", &configdb.gui },
		{ "dumpmem", NULL, "Save memory content on exit", &configdb.dumpmem },
#ifdef XEMU_SNAPSHOT_SUPPORT
		{ "snapload", NULL, "Load a snapshot from the given file", &configdb.snapload },
		{ "snapsave", NULL, "Save a snapshot into the given file before Xemu would exit", &configdb.snapsave },
#endif
		{ "prg", NULL, "Load a PRG file directly into the memory (/w C64/65 auto-detection on load address)", &configdb.prg }
	);
	XEMUCFG_DEFINE_SWITCH_OPTIONS(
		{ "allowmousegrab", "Allow auto mouse grab with left-click", &allow_mouse_grab },
		{ "d81ro", "Force read-only status for image specified with -8 option", &configdb.d81ro },
		{ "driveled", "Render drive LED at the top right corner of the screen", &show_drive_led },
		{ "fullscreen", "Start in fullscreen mode", &configdb.fullscreen },
		//{xemucfg_define_switch_option("noaudio", "Disable audio");
#ifdef FAKE_TYPING_SUPPORT
		{ "go64", "Go into C64 mode after start", &configdb.go64 },
		{ "autoload", "Load and start the first program from disk", &configdb.autoload },
#endif
		{ "syscon", "Keep system console open (Windows-specific effect only)", &configdb.syscon },
		{ "besure", "Skip asking \"are you sure?\" on RESET or EXIT", &i_am_sure_override }
	);
	XEMUCFG_DEFINE_NUM_OPTIONS(
		{ "sdlrenderquality", RENDER_SCALE_QUALITY, "Setting SDL hint for scaling method/quality on rendering (0, 1, 2)", &configdb.sdlrenderquality, 0, 2 },
		{ "dmarev", 2, "Revision of the DMAgic chip (0/1=F018A/B, 2=rom_auto, +512=modulo))", &configdb.dmarev, 0, 1000 },
		{ "prgmode", 0, "Override auto-detect option for -prg (64 or 65 for C64/C65 modes, 0 = default, auto detect)", &configdb.prgmode, 0, 65 }
	);
}
