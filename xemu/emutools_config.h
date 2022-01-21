/* Part of the Xemu project, please visit: https://github.com/lgblgblgb/xemu
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

#ifndef XEMU_COMMON_EMUTOOLS_CONFIG_H_INCLUDED
#define XEMU_COMMON_EMUTOOLS_CONFIG_H_INCLUDED

#define CONFIG_FILE_MAX_SIZE	0x10000
#define CONFIG_VALUE_MAX_LENGTH	256
#define OPT_NAME_MAX_LENGTH	16

#define CONFIG_FILE_TEMPL_NAME	"@%s-template.cfg"
#define CONFIG_FILE_USE_NAME	"@%s-default.cfg"

enum xemutools_option_type {
	XEMUCFG_OPT_STR, XEMUCFG_OPT_BOOL, XEMUCFG_OPT_NUM, XEMUCFG_OPT_FLOAT, XEMUCFG_OPT_PROC
};

struct xemutools_config_st;

#define EMUCFG_PARSER_CALLBACK_RET_TYPE const char*
#define EMUCFG_PARSER_CALLBACK_ARG_LIST struct xemutools_config_st *opt, const char *optname, const char *optvalue
#define EMUCFG_PARSER_CALLBACK(name) EMUCFG_PARSER_CALLBACK_RET_TYPE name ( EMUCFG_PARSER_CALLBACK_ARG_LIST )
typedef EMUCFG_PARSER_CALLBACK_RET_TYPE (*xemucfg_parser_callback_func_t)( EMUCFG_PARSER_CALLBACK_ARG_LIST );

#define XEMUCFG_FLAG_CLI_ONLY	1
#define XEMUCFG_FLAG_FIRST_ONLY	2
#define XEMUCFG_FLAG_DUMMY	4
#define XEMUCFG_FLAG_FILE_ONLY	8

struct xemutools_config_st {
	struct xemutools_config_st *next;
	const char *name;
	enum xemutools_option_type type;
	const char *help;
	unsigned int flags;
	union {
		// All structs must have a pointer first!!
		struct {
			void	*p;
		} opt_ANY;
		struct {
			int	*p;
			int	defval;
		} opt_bool;
		struct {
			int 	*p;
			int	min, max;
			int	defval;
		} opt_num;
		struct {
			double 	*p;
			double	min, max;
			double	defval;
		} opt_float;
		struct {
			char	**pp;
			const char	*defval;
		} opt_str;
		struct {
			xemucfg_parser_callback_func_t p;	// yes, even this is a pointer (function pointer)
		} opt_proc;
	};
};

struct xemutools_configdef_bool_st {
	const char  *optname;
	const int    defval;
	const char  *help;
	int         *store_here;
};
struct xemutools_configdef_str_st {
	const char  *optname;
	const char  *defval;
	const char  *help;
	char        **store_here;
};
struct xemutools_configdef_num_st {
	const char  *optname;
	const int    defval;
	const char  *help;
	int         *store_here;
	int          min;
	int          max;
};
struct xemutools_configdef_float_st {
	const char  *optname;
	const double defval;
	const char  *help;
	double      *store_here;
	double       min;
	double       max;
};
struct xemutools_configdef_proc_st {
	const char  *optname;
	const xemucfg_parser_callback_func_t cb;
	const char  *help;
};
struct xemutools_configdef_switch_st {
	const char  *optname;
	const char  *help;
	int         *store_here;
};

#define	XEMUCFG_DEFINE_BOOL_OPTIONS(...)   xemucfg_define_bool_option_multi(  (const struct xemutools_configdef_bool_st  []) { __VA_ARGS__ , {NULL} } )
#define	XEMUCFG_DEFINE_STR_OPTIONS(...)    xemucfg_define_str_option_multi(   (const struct xemutools_configdef_str_st   []) { __VA_ARGS__ , {NULL} } )
#define	XEMUCFG_DEFINE_NUM_OPTIONS(...)    xemucfg_define_num_option_multi(   (const struct xemutools_configdef_num_st   []) { __VA_ARGS__ , {NULL} } )
#define	XEMUCFG_DEFINE_FLOAT_OPTIONS(...)  xemucfg_define_float_option_multi( (const struct xemutools_configdef_float_st []) { __VA_ARGS__ , {NULL} } )
#define	XEMUCFG_DEFINE_PROC_OPTIONS(...)   xemucfg_define_proc_option_multi(  (const struct xemutools_configdef_proc_st  []) { __VA_ARGS__ , {NULL} } )
#define	XEMUCFG_DEFINE_SWITCH_OPTIONS(...) xemucfg_define_switch_option_multi((const struct xemutools_configdef_switch_st[]) { __VA_ARGS__ , {NULL} } )

extern void xemucfg_define_bool_option_multi   ( const struct xemutools_configdef_bool_st   p[] );
extern void xemucfg_define_str_option_multi    ( const struct xemutools_configdef_str_st    p[] );
extern void xemucfg_define_num_option_multi    ( const struct xemutools_configdef_num_st    p[] );
extern void xemucfg_define_float_option_multi  ( const struct xemutools_configdef_float_st  p[] );
extern void xemucfg_define_proc_option_multi   ( const struct xemutools_configdef_proc_st   p[] );
extern void xemucfg_define_switch_option_multi ( const struct xemutools_configdef_switch_st p[] );

extern void xemucfg_define_bool_option   ( const char *optname, const int    defval, const char *help, int    *storage  );
extern void xemucfg_define_str_option    ( const char *optname, const char  *defval, const char *help, char   **storage  );
extern void xemucfg_define_num_option    ( const char *optname, const int    defval, const char *help, int    *storage, int    min, int    max  );
extern void xemucfg_define_float_option  ( const char *optname, const double defval, const char *help, double *storage, double min, double max  );
extern void xemucfg_define_switch_option ( const char *optname,                      const char *help, int    *storage  );
extern void xemucfg_define_proc_option   ( const char *optname, xemucfg_parser_callback_func_t cb, const char *help     );

extern int  xemucfg_parse_all ( int argc, char **argv );
extern int  xemucfg_save_config_file  ( const char *filename, const char *initial_part, const char *cry );
extern int  xemucfg_parse_config_file ( const char *filename_in, const char *open_fail_msg );

extern void xemucfg_set_str ( char **ptr, const char *value );

extern int  xemucfg_integer_list_from_string ( const char *value, int *result, int maxitems, const char *delims );

extern int  xemucfg_str2int    ( const char *s,    int *result );
extern int  xemucfg_str2double ( const char *s, double *result );
extern int  xemucfg_str2bool   ( const char *s,    int *result );

#ifndef XEMU_RELEASE_BUILD
extern void xemucfg_dump_db ( const char *msg );
#endif

#endif
