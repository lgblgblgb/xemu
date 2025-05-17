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

#ifndef XEMU_EP128_CONFIGDB_H_INCLUDED
#define XEMU_EP128_CONFIGDB_H_INCLUDED

/* Important WARNING:
 * This is a trap! If you have something here with '#ifdef', it's quite possible that the macro is
 * not defined here, but defined elsewhere, thus the emulator sees totally different structs for
 * real but the problem is hidden! That is, be very careful at configdb_st (the type definition
 * itself also at the usage!) that should be only dependent on macros defined in xemu-target.h,
 * since that header file is always included by the build system, at command line level. */

#include "xemu/emutools.h"
#include "xemu/emutools_config.h"
#include "configdb.h"

struct configdb_st {
	int	sdlrenderquality;
	int	fullscreen_requested;
	int	skiplogo;
	double	clock;
	int	syscon, audio;
	int	mousemode;
	int	primo;
#ifndef	NO_CONSOLE
	int	monitor;
#endif
	char	*ram_setup_str;
	char	*gui_selection;
	char	*snapshot;
	char	*sdimg;
	char	*filedir;
	char	*ddn;
	char	*printfile;
	char	*wd_img_path;
};

extern struct configdb_st configdb;

extern void configdb_define_emulator_options ( void );

#endif
