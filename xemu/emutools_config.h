/* Xemu - Somewhat lame emulation (running on Linux/Unix/Windows/OSX, utilizing
   SDL2) of some 8 bit machines, including the Commodore LCD and Commodore 65
   and MEGA65 as well.
   Copyright (C)2016-2020 LGB (Gábor Lénárt) <lgblgblgb@gmail.com>

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

#ifndef XEMU_COMMON_EMUTOOLS_CONFIG_H_INCLUDED
#define XEMU_COMMON_EMUTOOLS_CONFIG_H_INCLUDED

#define CONFIG_FILE_MAX_SIZE	0x10000
#define CONFIG_VALUE_MAX_LENGTH	256

enum xemutools_option_type {
	OPT_STR, OPT_BOOL, OPT_NUM, OPT_NO, OPT_PROC
};

struct xemutools_config_st;
struct xemutools_config_st {
	struct xemutools_config_st *next;
	const char *name;
	enum xemutools_option_type type;
	void *value;
	const char *help;
};

#define EMUCFG_PARSER_CALLBACK_RET_TYPE const char*
#define EMUCFG_PARSER_CALLBACK_ARG_LIST struct xemutools_config_st *opt, const char *optname, const char *optvalue
#define EMUCFG_PARSER_CALLBACK(name) EMUCFG_PARSER_CALLBACK_RET_TYPE name ( EMUCFG_PARSER_CALLBACK_ARG_LIST )
typedef EMUCFG_PARSER_CALLBACK_RET_TYPE (*xemucfg_parser_callback_func_t)( EMUCFG_PARSER_CALLBACK_ARG_LIST );

extern void xemucfg_define_option        ( const char *optname, enum xemutools_option_type type, void *defval, const char *help );
extern void xemucfg_define_bool_option   ( const char *optname, int defval, const char *help );
extern void xemucfg_define_str_option    ( const char *optname, const char *defval, const char *help );
extern void xemucfg_define_num_option    ( const char *optname, int defval, const char *help );
extern void xemucfg_define_proc_option   ( const char *optname, xemucfg_parser_callback_func_t defval, const char *help );
extern void xemucfg_define_switch_option ( const char *optname, const char *help );

extern int  xemucfg_has_option ( const char *name );
extern int  xemucfg_parse_all ( int argc, char **argv );
extern const char *xemucfg_get_str ( const char *optname );
extern int  xemucfg_get_num ( const char *optname );
extern int  xemucfg_get_bool ( const char *optname );

extern int  xemucfg_integer_list_from_string ( const char *value, int *result, int maxitems, const char *delims );

#endif
