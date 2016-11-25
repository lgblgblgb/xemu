/* Xep128: Minimalistic Enterprise-128 emulator with focus on "exotic" hardware
   Copyright (C)2015,2016 LGB (Gábor Lénárt) <lgblgblgb@gmail.com>
   http://xep128.lgb.hu/

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

#ifndef __XEP128_CONFIGURATION_H_INCLUDED
#define __XEP128_CONFIGURATION_H_INCLUDED

#include <SDL_version.h>

extern char *app_pref_path, *app_base_path;
extern char current_directory[PATH_MAX + 1];
extern char sdimg_path[PATH_MAX + 1];
extern SDL_version sdlver_compiled, sdlver_linked;

extern int  config_init ( int argc, char **argv );
extern void *config_getopt ( const char *name, const int subopt, void *value );
extern void config_getopt_pointed ( void *st_in, void *value );
extern FILE *open_emu_file ( const char *name, const char *mode, char *pathbuffer );
extern void forget_emu_file ( const char *path );

static inline int config_getopt_int ( const char *name ) {
	int n;
	config_getopt(name, -1, &n);
	return n;
}
static inline const char *config_getopt_str ( const char *name ) {
	char *s;
	config_getopt(name, -1, &s);
	return s;
}

#include "xemu/emutools_buildinfo.h"

#endif
