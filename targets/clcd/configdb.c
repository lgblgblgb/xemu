/* Commodore LCD emulator (rewrite of my world's first working Commodore LCD emulator)
   Copyright (C)2016-2023 LGB (Gábor Lénárt) <lgblgblgb@gmail.com>
   Part of the Xemu project: https://github.com/lgblgblgb/xemu

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
#include "commodore_lcd.h"


struct configdb_st configdb;



void configdb_define_emulator_options ( void )
{
	XEMUCFG_DEFINE_SWITCH_OPTIONS(
		{ "fullscreen", "Start in fullscreen mode", &configdb.fullscreen_requested },
		{ "syscon", "Keep system console open (Windows-specific effect only)", &configdb.syscon },
		{ "besure", "Skip asking \"are you sure?\" on RESET or EXIT", &i_am_sure_override },
		{ "scrub", "Start with memory-scrub state", &configdb.scrub }
	);
	XEMUCFG_DEFINE_NUM_OPTIONS(
#ifdef SDL_HINT_RENDER_SCALE_QUALITY
		{ "sdlrenderquality", RENDER_SCALE_QUALITY, "Setting SDL hint for scaling method/quality on rendering (0, 1, 2)", &configdb.sdlrenderquality, 0, 2 },
#endif
		{ "zoom", 1, "Window zoom value (1-4)", &configdb.zoom, 1, 4 },
		{ "ram", 128, "Sets RAM size in KBytes (32-128)", &configdb.ram_size_kbytes, 32, 128 },
		{ "clock", 1, "Sets CPU speed in MHz (integer only) 1-16", &configdb.clock_mhz, 1, 16 }
	);
	XEMUCFG_DEFINE_STR_OPTIONS(
		{ "rom102", "#clcd-u102.rom", "Selects 'U102' ROM to use", &configdb.rom102_fn },
		{ "rom103", "#clcd-u103.rom", "Selects 'U103' ROM to use", &configdb.rom103_fn },
		{ "rom104", "#clcd-u104.rom", "Selects 'U104' ROM to use", &configdb.rom104_fn },
		{ "rom105", "#clcd-u105.rom", "Selects 'U105' ROM to use", &configdb.rom105_fn },
		{ "romchr", "#clcd-chargen.rom", "Selects character ROM to use", &configdb.romchr_fn },
		//{ "defprg", NULL, "Selects the ROM-program to set default to", &configdb.defprg },
		{ "prg", NULL, "Inject BASIC program on entering to BASIC", &configdb.prg_inject_fn },
		{ "gui", NULL, "Select GUI type for usage. Specify some insane str to get a list", &configdb.gui_selection },
		{ "dumpmem", NULL, "Save memory content on exit", &configdb.dumpmem }
	);
}
