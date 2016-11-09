/* Xemu - Somewhat lame emulation (running on Linux/Unix/Windows/OSX, utilizing
   SDL2) of some 8 bit machines, including the Commodore LCD and Commodore 65
   and some Mega-65 features as well.
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

#ifndef __XEMU_COMMON_EMUTOOLS_CONFIG_H_INCLUDED
#define __XEMU_COMMON_EMUTOOLS_CONFIG_H_INCLUDED

enum emutools_option_type {
	OPT_STR,
	OPT_BOOL,
	OPT_NUM,
	OPT_NO
};


struct emutools_config_st;

//typedef const char* (*emuopt_parser_func_t)(struct emutools_config_st *, );


struct emutools_config_st {
	struct emutools_config_st *next;
	const char *name;
	enum emutools_option_type type;
	//emuopt_parser_func_t *parser;
	void *value;
	const char *help;
};

extern void emucfg_define_option ( const char *optname, enum emutools_option_type type, void *defval, const char *help );
extern int  emucfg_parse_commandline ( int argc, char **argv );
extern const char *emucfg_get_str ( const char *optname );
extern int  emucfg_get_num ( const char *optname );
extern int  emucfg_get_bool ( const char *optname );

#endif
