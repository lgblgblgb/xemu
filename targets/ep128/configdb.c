/* Minimalistic Enterprise-128 emulator with focus on "exotic" hardware
   Part of the Xemu project, please visit: https://github.com/lgblgblgb/xemu
   Copyright (C)2015-2017,2020-2021 LGB (Gábor Lénárt) <lgblgblgb@gmail.com>

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
#include "roms.h"
#include "enterprise128.h"

/* Important WARNING:
 * This is a trap! If you have something here with '#ifdef', it's quite possible that the macro is
 * not defined here, but defined elsewhere, thus the emulator sees totally different structs for
 * real but the problem is hidden! That is, be very careful at configdb_st (the type definition
 * itself also at the usage!) that should be only dependent on macros defined in xemu-target.h,
 * since that header file is always included by the build system, at command line level. */

static const char *rom_parse_opt_cb ( struct xemutools_config_st *unused, const char *optname, const char *optvalue )
{
	return rom_parse_opt(optname, optvalue);
}

struct configdb_st configdb;

void configdb_define_emulator_options ( void )
{
	XEMUCFG_DEFINE_SWITCH_OPTIONS(
		{ "audio", "Enable (buggy) audio output", &configdb.audio },
		{ "syscon", "Keep console window open (Windows-specific)", &configdb.syscon },
		{ "fullscreen", "Start in fullscreen mode", &configdb.fullscreen_requested },
		{ "primo", "Start in Primo emulator mode", &configdb.primo },
		{ "skiplogo", "Disables Enterprise logo on start-up via XEP ROM", &configdb.skiplogo },
#ifndef		NO_CONSOLE
		{ "monitor", "Start monitor on console", &configdb.monitor },
#endif
		{ "noxeprom", "Disables XEP internal ROM", &cfg_noexprom }
	);
	XEMUCFG_DEFINE_STR_OPTIONS(
		{ "ddn", NULL, "Default device name (none = not to set)", &configdb.ddn },
		{ "filedir", "@files", "Default directory for FILE: device", &configdb.filedir },
		{ "printfile", PRINT_OUT_FN, "Printing into this file", &configdb.printfile },
		{ "ram", "128", "RAM size in Kbytes (decimal) or segment specification(s) prefixed with @ in hex (VRAM is always assumed), like: @C0-CF,E0,E3-E7", &configdb.ram_setup_str },
		{ "sdimg", SDCARD_IMG_FN, "SD-card disk image (VHD) file name/path", &configdb.sdimg },
		{ "snapshot", NULL, "Load and use ep128emu snapshot", &configdb.snapshot },
#ifdef CONFIG_EXDOS_SUPPORT
		{ "wdimg", NULL, "EXDOS WD disk image file name/path", &configdb.wd_img_path },
#endif
		{ "gui", NULL, "Select GUI type for usage. Specify some insane str to get a list", &configdb.gui_selection }
	);
	XEMUCFG_DEFINE_NUM_OPTIONS(
		{ "sdlrenderquality", RENDER_SCALE_QUALITY, "Setting SDL hint for scaling method/quality on rendering (0, 1, 2)", &configdb.sdlrenderquality, 0, 2 },
		{ "mousemode",	1, "Set mouse mode, 1-3 = J-column 2,4,8 bytes and 4-6 the same for K-column", &configdb.mousemode, 1, 6 }
	);
	xemucfg_define_float_option("clock", (double)DEFAULT_CPU_CLOCK, "Z80 clock in MHz", &configdb.clock, 1.0, 12.0);
	xemucfg_define_proc_option("rom", rom_parse_opt_cb, "ROM image, format is \"rom@xx=filename\" (xx=start segment in hex), use rom@00 for EXOS or combined ROM set");
	//{ "epkey",	CONFITEM_STR,	NULL,		1, "Define a given EP/emu key, format epkey@xy=SDLname, where x/y are row/col in hex or spec code (ie screenshot, etc)." },
}
