/* Part of the Xemu project, please visit: https://github.com/lgblgblgb/xemu
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

#ifndef XEMU_CONFIGDB_SUPPORT
#error "XEMU_CONFIGDB_SUPPORT was not defined in your xemu-target.h but xemu/emutools_config.c got compiled!"
#else

#include "xemu/emutools.h"
#include "xemu/emutools_files.h"
#include "xemu/emutools_config.h"
#include <string.h>
#include <errno.h>
// About using strtod and maybe strtol too, some issues on Windows:
// ERROR: FIXME: windows does not now about HUGE_VALF and VALL ... it seems it want .._VAL with +/- sign?
// Also FIXME: widnows talks about EINVAL as well :-O
// https://docs.microsoft.com/en-us/cpp/c-runtime-library/reference/strtod-strtod-l-wcstod-wcstod-l?view=msvc-160
// ... but maybe that is for invalid paramers only (null pointer, invalid base, such like that)
// it seems windows needs math.h to have HUGE_VAL :-O
#include <math.h>
// Also, stdlib.h "should" be needed anyway
#include <stdlib.h>


static struct xemutools_config_st *config_head = NULL;
static struct xemutools_config_st *config_current;
static int configdb_finalized = 0;	// no new option can be defined if it's set to non-zero (after first use of any parse functionality)

static const char CONFIGDB_ERROR_MSG[] = "Xemu internal ConfigDB error:";


void xemucfg_set_str ( char **ptr, const char *value )
{
	if (value) {
		size_t len = strlen(value) + 1;
		*ptr = xemu_realloc(*ptr, len);
		memcpy(*ptr, value, len);
	} else if (*ptr) {
		free(*ptr);
		*ptr = NULL;
	}
}


// Warning! No check, be sure to call this ONLY if "p" has really p->type == XEMUCFG_OPT_STR!
static int is_str_opt_default ( struct xemutools_config_st *p )
{
	if (*p->opt_str.pp == NULL) {
		return p->opt_str.defval == NULL ? 1 : 0;
	}
	if (p->opt_str.defval == NULL)
		return 0;
	return !strcmp(*p->opt_str.pp, p->opt_str.defval);
}


static const char *set_option_value ( struct xemutools_config_st *p, const void *value )
{
	static char error_str[128];
	if ((p->flags & XEMUCFG_FLAG_DUMMY))
		return NULL;
	if (value == NULL && p->type != XEMUCFG_OPT_STR)
		FATAL("%s option %s gets value to set as NULL", CONFIGDB_ERROR_MSG, p->name);
	int error_flag = 0;
	int i;
	double f;
	switch (p->type) {
		case XEMUCFG_OPT_STR:
			// remove leading " if any (by moving the pointer ...)
			if (value && *(const char*)value == '"')
				value++;
			// Set it now
			xemucfg_set_str(p->opt_str.pp, value);
			if (value) {
				// remove trailing " if any
				i = strlen(value) - 1;
				if (i >= 0 && ((const char*)value)[i] == '"')
					*p->opt_str.pp[i] = '\0';
			}
			break;
		case XEMUCFG_OPT_NUM:
			i = *(int*)value;
			if (i < p->opt_num.min) {
				i = p->opt_num.min;
				error_flag = 1;
			}
			if (i > p->opt_num.max) {
				i = p->opt_num.max;
				error_flag = 1;
			}
			if (error_flag)
				snprintf(error_str, sizeof error_str, "requires integer value between %d and %d, your value of %d is invalid",
					p->opt_num.min, p->opt_num.max, *(int*)value
				);
			*p->opt_num.p = i;
			break;
		case XEMUCFG_OPT_BOOL:
			i = *(int*)value;
			if (i != 0 && i != 1) {
				error_flag = 1;
				snprintf(error_str, sizeof error_str, "requires boolean value 0 or 1, your value was %d", i);
			}
			*(int*)p->opt_bool.p = !!i;
			break;
		case XEMUCFG_OPT_FLOAT:
			f = *(double*)value;
			if (f < p->opt_float.min) {
				f = p->opt_float.min;
				error_flag = 1;
			}
			if (f > p->opt_float.max) {
				f = p->opt_float.max;
				error_flag = 1;
			}
			if (error_flag)
				snprintf(error_str, sizeof error_str, "requires float value between %f and %f, your value of %f is invalid",
					p->opt_float.min, p->opt_float.max, *(double*)value
				);
			*p->opt_float.p = f;
			break;
		case XEMUCFG_OPT_PROC:
			p->opt_proc.p = value;
			break;
	}
	if (error_flag) {
		// Errors before finalization means, that the defined parameter values ARE invalid.
		// Which should not happen, and must be treated as fatal errors, ASAP!
		if (!configdb_finalized)
			FATAL("%s Invalid default value at defining option: %s", CONFIGDB_ERROR_MSG, error_str);
		return error_str;
	} else
		return NULL;
}


int xemucfg_str2int ( const char *s, int *result )
{
	int base;
	// Own logic to try to identify base-16 and base-2
	// The base=0 for strtol() trick is not so nice, as it does not recognize
	// "fancy" prefixes, but it also recognizes octal with starting of "0"
	// which can be VERY confusing and I want to avoid!
	if (!strncasecmp(s, "0x", 2)) {
		base = 16;
		s += 2;
	} else if (s[0] == '$') {
		base = 16;
		s++;
	} else if (!strncasecmp(s, "0b", 2)) {
		base = 2;
		s += 2;
	} else if (s[0] == '%') {
		base = 2;
		s++;
	} else if (s[0] != '\0' && !strcasecmp(s + strlen(s) - 1, "h")) {
		base = 16;
	} else if (!strncasecmp(s, "0o", 2)) {	// heh, maybe somebody use octal, but please, no simple "0" prefix, that stupid! use "0o"
		base = 8;
		s += 2;
	} else
		base = 10;
	char *endptr;
	errno = 0;
	// yes-yes, maybe your OS, your computer etc has "long int" the same size as "int" but it's not universal
	// and for some reason there is no function strtoi() for int, just for longs ... Dunno why ...
	long int res = strtol(s, &endptr, base);
	if (((res == LONG_MIN || res == LONG_MAX) && errno == ERANGE) || res < INT_MIN || res > INT_MAX || endptr == s || (*endptr != '\0' && strcasecmp(endptr ,"h")))
		return 1;
	*result = (int)res;
	return 0;
}


int xemucfg_str2double ( const char *s, double *result )
{
	char *endptr;
	errno = 0;
	double res = strtod(s, &endptr);
	// Still we would like to detect problems, and want result which would fit into "int" as well, thush the INT_MIN and INT_MAX here
	if (((res == HUGE_VAL || res == -HUGE_VAL || res == 0) && errno == ERANGE) || res <= (double)INT_MIN || res >= (double)INT_MAX || endptr == s || *endptr != '\0')
		return 1;
	*result = res;
	return 0;
}


int xemucfg_str2bool ( const char *s, int *result )
{
	if (!strcasecmp(s, "yes") || !strcasecmp(s, "on") || !strcmp(s, "1") || !strcasecmp(s, "true")) {
		*result = 1;
		return 0;
	}
	if (!strcasecmp(s, "no") || !strcasecmp(s, "off") || !strcmp(s, "0") || !strcasecmp(s, "false")) {
		*result = 0;
		return 0;
	}
	return 1;
}


static const char *set_option_value_from_string ( struct xemutools_config_st *p, const char *value, const char *optname_fullspec )
{
	if ((p->flags & XEMUCFG_FLAG_DUMMY))
		return NULL;
	int i;
	double f;
	switch (p->type) {
		case XEMUCFG_OPT_STR:
			return set_option_value(p, (void*)value);
		case XEMUCFG_OPT_NUM:
			if (xemucfg_str2int(value, &i))
				return "integer value has invalid syntax";
			return set_option_value(p, &i);
		case XEMUCFG_OPT_FLOAT:
			if (xemucfg_str2double(value, &f))
				return "float value has invalid syntax";
			return set_option_value(p, &f);
		case XEMUCFG_OPT_BOOL:
			if (xemucfg_str2bool(value, &i))
				return "boolean value must be yes/on/1/true OR no/off/0/false";
			return set_option_value(p, &i);
		case XEMUCFG_OPT_PROC:
			// TODO: book all "proc" events (value and optname_full - this second one needed for @... prefixes)
			// so config save can save those!
			//return (*(xemucfg_parser_callback_func_t)(p->opt_proc.p))(p, optname_fullspec, value);
			return p->opt_proc.p(p, optname_fullspec, value);
	}
	return "unknown code-path";
}


static void define_core_options ( void );


// This internal function may leaves some fields unfilled!
// The various xemcfg_define_..._option() functions must set those up!
static void define_new_option ( const char *optname, enum xemutools_option_type type, const char *help, void *storage )
{
	static int core_definitions = 0;
	if (!core_definitions) {
		// Define some options BEFORE the requested one, if this is the first requested.
		// Yay, define_core_options() will call this routine, be careful :-O
		core_definitions = 1;
		define_core_options();
	}
	if (configdb_finalized)
		FATAL("%s cannot define new option '%s', DB is already finalized!", CONFIGDB_ERROR_MSG, optname);
	if (optname == NULL)
		FATAL("%s optname = NULL for define_new_option()", CONFIGDB_ERROR_MSG);
	if (strlen(optname) > OPT_NAME_MAX_LENGTH)
		FATAL("%s optname is too long string for define_new_option() at defining option %s", CONFIGDB_ERROR_MSG, optname);
	if (storage == NULL)
		FATAL("%s storage = NULL for define_new_option() at defining option %s", CONFIGDB_ERROR_MSG, optname);
	config_current = xemu_malloc(sizeof(struct xemutools_config_st));
	if (!config_head) {
		config_head = config_current;
		config_current->next = NULL;
	} else {
		struct xemutools_config_st *p = config_head, *p_prev = NULL;
		for (;;) {
			// we want to manage alphabet sorted list just for nice help output, and for checking re-definition assertion in one step as well
			int ret = strcasecmp(optname, p->name);
			if (!ret)
				FATAL("%s trying to re-define option '%s'", CONFIGDB_ERROR_MSG, optname);
			if (ret < 0) {	// we want the first entry already later in alphabet than current one, insert new entry before that!
				config_current->next = p;
				if (p_prev)
					p_prev->next = config_current;
				else
					config_head = config_current;
				break;
			}
			if (p->next) {
				p_prev = p;
				p = p->next;
			} else {
				p->next = config_current;
				config_current->next = NULL;
				break;
			}

		}
	}
	static const char no_help[] = "(no help)";
	config_current->name = optname;
	config_current->type = type;
	config_current->opt_ANY.p = storage;
	config_current->help = (help && *help) ? help : no_help;
	config_current->flags = 0;
	if (type == XEMUCFG_OPT_STR)
		*config_current->opt_str.pp = NULL;
}



void xemucfg_define_bool_option   ( const char *optname, const int defval, const char *help, int *storage )
{
	define_new_option(optname, XEMUCFG_OPT_BOOL, help, storage);
	// return value (error message) is ignored since in define stage (configdb_finalized is zero) it will
	// be fatal error anyway, triggered by set_option_value()
	(void)set_option_value(config_current, &defval);
	config_current->opt_bool.defval = *config_current->opt_bool.p;
}

void xemucfg_define_switch_option ( const char *optname, const char *help, int *storage )
{	// Really, this function defines a BOOL typed option, same as "bool" however the default value is hardcoded to be zero.
	xemucfg_define_bool_option(optname, 0, help, storage);
}

static void xemucfg_define_dummy_option  ( const char *optname, const char *help, int flags )
{
	static int dummy = 0;
	define_new_option(optname, XEMUCFG_OPT_BOOL, help, &dummy);
	config_current->flags = flags | XEMUCFG_FLAG_DUMMY;
	config_current->opt_bool.defval = 0;
}

void xemucfg_define_str_option    ( const char *optname, const char *defval, const char *help, char **storage ) {
	define_new_option(optname, XEMUCFG_OPT_STR, help, storage);
	(void)set_option_value(config_current, defval);
	config_current->opt_str.defval = defval;
}

void xemucfg_define_num_option    ( const char *optname, const int defval, const char *help, int *storage, int min, int max )
{
	if (min >= max)
		FATAL("%s cannot define new option '%s', given numeric range is irreal: %d ... %d", CONFIGDB_ERROR_MSG, optname, min, max);
	define_new_option(optname, XEMUCFG_OPT_NUM, help, storage);
	config_current->opt_num.min = min;
	config_current->opt_num.max = max;
	(void)set_option_value(config_current, &defval);
	config_current->opt_num.defval = *config_current->opt_num.p;
}

void xemucfg_define_float_option  ( const char *optname, const double defval, const char *help, double *storage, double min, double max )
{
	if (min >= max)
		FATAL("%s cannot define new option '%s', given float range is irreal: %f ... %f", CONFIGDB_ERROR_MSG, optname, min, max);
	define_new_option(optname, XEMUCFG_OPT_FLOAT, help, storage);
	config_current->opt_float.min = min;
	config_current->opt_float.max = max;
	(void)set_option_value(config_current, &defval);
	config_current->opt_float.defval = *config_current->opt_float.p;
}

void xemucfg_define_proc_option   ( const char *optname, xemucfg_parser_callback_func_t cb, const char *help )
{
	define_new_option(optname, XEMUCFG_OPT_PROC, help, cb);
}



void xemucfg_define_bool_option_multi   ( const struct xemutools_configdef_bool_st   p[] ) {
	for (int i = 0; p[i].optname; i++)
		xemucfg_define_bool_option  (p[i].optname, p[i].defval, p[i].help, p[i].store_here);
}
void xemucfg_define_switch_option_multi ( const struct xemutools_configdef_switch_st p[] ) {
	for (int i = 0; p[i].optname; i++)
		xemucfg_define_switch_option(p[i].optname, p[i].help, p[i].store_here);
}
void xemucfg_define_str_option_multi    ( const struct xemutools_configdef_str_st    p[] ) {
	for (int i = 0; p[i].optname; i++)
		xemucfg_define_str_option   (p[i].optname, p[i].defval, p[i].help, p[i].store_here);
}
void xemucfg_define_num_option_multi    ( const struct xemutools_configdef_num_st    p[] ) {
	for (int i = 0; p[i].optname; i++)
		xemucfg_define_num_option   (p[i].optname, p[i].defval, p[i].help, p[i].store_here, p[i].min, p[i].max);
}
void xemucfg_define_float_option_multi  ( const struct xemutools_configdef_float_st  p[] ) {
	for (int i = 0; p[i].optname; i++)
		xemucfg_define_float_option (p[i].optname, p[i].defval, p[i].help, p[i].store_here, p[i].min, p[i].max);
}
void xemucfg_define_proc_option_multi   ( const struct xemutools_configdef_proc_st   p[] ) {
	for (int i = 0; p[i].optname; i++)
		xemucfg_define_proc_option  (p[i].optname, p[i].cb, p[i].help);
}


static struct xemutools_config_st *search_option ( const char *name )
{
	struct xemutools_config_st *p = config_head;
	char *s = strchr(name, '@');
	int l = s ? s - name : strlen(name);
	while (p)
		if (!strncasecmp(name, p->name, l) && p->name[l] == 0 && (name[l] == 0 || name[l] == '@'))
			break;
		else
			p = p->next;
	return (p && s && p->type != XEMUCFG_OPT_PROC) ? NULL : p;
}


static void dump_help ( void )
{
	printf("Available command line options:" NL NL);
	for (struct xemutools_config_st *p = config_head; p != NULL; p = p->next) {
		if ((p->flags & XEMUCFG_FLAG_FILE_ONLY))
			continue;
		const char *t = "";
		if ((p->flags & XEMUCFG_FLAG_FIRST_ONLY))
			t = "(1st-arg!)  ";
		/*else if ((p->flags & XEMUCFG_FLAG_DUMMY))
			t = "            ";*/
		else
			switch (p->type) {
				case XEMUCFG_OPT_BOOL:	t = "(bool)      "; break;
				case XEMUCFG_OPT_NUM:	t = "(int-num)   "; break;
				case XEMUCFG_OPT_FLOAT:	t = "(float-num) "; break;
				case XEMUCFG_OPT_STR:	t = "(str)       "; break;
				case XEMUCFG_OPT_PROC:	t = "(spec)      "; break;
			}
		printf("-%-16s  %s%s" NL, p->name, t, p->help);
	}
	printf(NL "Bool(-ean) options can be written without parameter if it means (1/on/yes/true)." NL);
}


static char *get_config_string_representation ( const char *initial_part )
{
	char *out = xemu_strdup(initial_part ? initial_part : "");
	for (struct xemutools_config_st *p = config_head; p != NULL; p = p->next) {
		if ((p->flags & (XEMUCFG_FLAG_CLI_ONLY | XEMUCFG_FLAG_DUMMY)))
			continue;
		char buffer[256 + PATH_MAX];
		int r = 0;	// sigh, gcc is stupid, it must be set, even if it see all cases are handled later ...
		// We want to dump configDB in a way, that:
		// * includes the help
		// * values left at their default values are just there as comment
		switch (p->type) {
			case XEMUCFG_OPT_STR:
				r = snprintf(buffer, sizeof buffer, NL "## %s" NL "## (string param)" NL "%s%s = \"%s\"" NL,
					p->help,
					is_str_opt_default(p) ? "#" : "",
					p->name,
					*p->opt_str.pp ? *p->opt_str.pp : ""
				);
				break;
			case XEMUCFG_OPT_BOOL:
				r = snprintf(buffer, sizeof buffer, NL "## %s" NL "## (boolean param)" NL "%s%s = %s" NL,
					p->help,
					*p->opt_bool.p == p->opt_bool.defval ? "#" : "",
					p->name,
					*p->opt_bool.p ? "true" : "false"
				);
				break;
			case XEMUCFG_OPT_NUM:
				r = snprintf(buffer, sizeof buffer, NL "## %s" NL "## (integer param)" NL "%s%s = %d" NL,
					p->help,
					*p->opt_num.p == p->opt_num.defval ? "#" : "",
					p->name,
					*p->opt_num.p
				);
				break;
			case XEMUCFG_OPT_FLOAT:
				r = snprintf(buffer, sizeof buffer, NL "## %s" NL "## (real-num param)" NL "%s%s = %f" NL,
					p->help,
					*p->opt_float.p == p->opt_float.defval ? "#" : "",
					p->name,
					*p->opt_float.p
				);
				break;
			case XEMUCFG_OPT_PROC:
				r = snprintf(buffer, sizeof buffer, NL "## %s" NL "## (SPECIAL)" NL "#%s" NL,
					p->help,
					p->name
				);
				break;
		}
		if (r >= sizeof(buffer) - 1) {
			free(out);
			FATAL("%s Too large result for dumping config option '%s'!", CONFIGDB_ERROR_MSG, p->name);
		}
		out = xemu_realloc(out, strlen(out) + strlen(buffer) + 1);
		strcpy(out + strlen(out), buffer);
	}
	if (strlen(out) > CONFIG_FILE_MAX_SIZE) {
		free(out);
		FATAL("%s Too large config file generated in %s", CONFIGDB_ERROR_MSG,  __func__);
	}
	return out;
}


int xemucfg_save_config_file ( const char *filename, const char *initial_part, const char *cry )
{
	char *out = get_config_string_representation(initial_part);
	int ret = out[0] ? xemu_save_file(filename, out, strlen(out), cry) : 0;
	free(out);
	return ret;
}


static char *get_config_template_string_representation ( void )
{
	char templ[4096];
	sprintf(templ,
		"# Config template for XEMU/%s %s (%s)" NL
		"# ----" NL
		"# DO NOT EDIT THIS FILE - THIS WILL BE OVERWRITTEN" NL
		"# Instead copy this file to a custom name and edit that, if needed." NL
		"# This file is never read back, only written out as template / reference." NL
		"# If you put a file in pref-dir with similar name as this file, just" NL
		"# changed 'template' to 'default', it will be automatically read by Xemu." NL
		"# ----" NL
		"# Rules: basically option = value syntax." NL
		"# String values optionally can be encolsed by '\"', and this is the only way" NL
		"# to have empty string. Option values expecting file names can start with" NL
		"# letters '@' or '#'. In case of '@' the rest of the filename/path is" NL
		"# interpreted as relative to the preferences directory. In case of '#', the" NL
		"# rest of the filename/path will be searched at some common places (including" NL
		"# the preferences directory, but also in the same directory as the binary is, or" NL
		"# in case of UNIX-like OS, even the common data directory)" NL
		"# ----" NL
		"# SDL preference directory for this installation: %s" NL
		"# Binary base directory when generating this file: %s" NL
#ifndef XEMU_ARCH_WIN
		"# Also common search directories:" NL
		"# " UNIX_DATADIR_0 NL
		"# " UNIX_DATADIR_1 NL
		"# " UNIX_DATADIR_2 NL
		"# " UNIX_DATADIR_3 NL
#endif
		"# ----" NL NL NL,
		/* args */
		xemu_app_name,
		XEMU_BUILDINFO_CDATE,
		XEMU_ARCH_NAME,
		sdl_pref_dir,
		sdl_base_dir
	);
	return get_config_string_representation(templ);
}


static inline void finalize_configdb ( void )
{
	configdb_finalized = 1;
}


int xemucfg_parse_config_file ( const char *filename_in, const char *open_fail_msg )
{
	finalize_configdb();
	char *p, cfgmem[CONFIG_FILE_MAX_SIZE + 1];
	int  lineno = xemu_load_file(filename_in, cfgmem, 0, CONFIG_FILE_MAX_SIZE, open_fail_msg);
	if (lineno < 0)
		return 1;
	cfgmem[lineno] = 0;	// terminate string
	if (strlen(cfgmem) != lineno) {
		ERROR_WINDOW("Bad config file (%s),\ncontains '\\0' character (not a text file?)", xemu_load_filepath);
		return 1;
	}
	p = cfgmem;
	lineno = 1;	// line number counter in read config file from now
	do {
		char *p1, *pn;
		// Skip white-spaces at the beginning of the line
		while (*p == ' ' || *p == '\t')
			p++;
		// Search for end of line (relaxed check, too much mixed line-endings I've seen already within ONE config file failed with simple fgets() etc method ...)
		p1 = strstr(p, "\r\n");
		if (p1)	pn = p1 + 2;
		else {
			p1 = strstr(p, "\n\r");
			if (p1)	pn = p1 + 2;
			else {
				p1 = strchr(p, '\n');
				if (p1)	pn = p1 + 1;
				else {
					p1 = strchr(p, '\r');
					pn = p1 ? p1 + 1 : NULL;
				}
			}
		}
		if (p1)	*p1 = 0;	// terminate line string at EOL marker
		p1 = strchr(p, '#');
		if (p1)	*p1 = 0;	// comment - if any - is also excluded
		// Chop white-spaces off from the end of the line
		p1 = p + strlen(p);
		while (p1 > p && (p1[-1] == '\t' || p1[-1] == ' '))
			*(--p1) = 0;
		// If line is not empty, separate key/val (if there is) and see what it means
		if (*p) {
			struct xemutools_config_st *o;
			const char *s;
			p1 = p;
			while (*p1 && *p1 != '\t' && *p1 != ' ' && *p1 != '=')
				p1++;
			if (*p1)  {
				*(p1++) = 0;
				while (*p1 == '\t' || *p1 == ' ' || *p1 == '=')
					p1++;
				if (!*p1)
					p1 = NULL;
			} else
				p1 = NULL;
			DEBUG("Line#%d = \"%s\",\"%s\"" NL, lineno, p, p1 ? p1 : "<no-specified>");
			o = search_option(p);
			if ((o->flags & XEMUCFG_FLAG_CLI_ONLY)) {
				ERROR_WINDOW("Config file (%s) error at line %d:\nkeyword '%s' is a command-line only option, cannot be used in config files", xemu_load_filepath, lineno, p);
				return 1;
			}
			if (!o) {
				ERROR_WINDOW("Config file (%s) error at line %d:\nkeyword '%s' is unknown.", xemu_load_filepath, lineno, p);
				return 1;
			}
			if (!p1) {
				ERROR_WINDOW("Config file (%s) error at line %d:\nkeyword '%s' requires a value.", xemu_load_filepath, lineno, p);
				return 1;
			}
			s = set_option_value_from_string(o, p1, p);
			if (s) {
				ERROR_WINDOW("Config file (%s) error at line %d:\nkeyword '%s': %s", xemu_load_filepath, lineno, p, s);
				return 1;
			}
		}
		// Prepare for next line
		p = pn;	// start of next line (or EOF if NULL)
		lineno++;
	} while (p);
	return 0;
}


#define OPT_ERROR_CMDLINE(...) do { ERROR_WINDOW("Command line error: " __VA_ARGS__); return 1; } while(0)


static int parse_commandline ( int argc, char **argv )
{
	finalize_configdb();
	while (argc) {
		if (argv[0][0] != '-')
			OPT_ERROR_CMDLINE("Invalid option '%s', must start with '-'", *argv);
		if (argv[0][1] == '-')
			OPT_ERROR_CMDLINE("Invalid option '%s', options start with '-' and not '--'", *argv);
		struct xemutools_config_st *p = search_option(*argv + 1);
		if (!p)
			OPT_ERROR_CMDLINE("Unknown option '%s'", *argv);
		if ((p->flags & XEMUCFG_FLAG_FIRST_ONLY))
			OPT_ERROR_CMDLINE("'%s' must be the first option", *argv);
		argc--;
		argv++;
		if (p->type == XEMUCFG_OPT_BOOL && (
			(argc && argv[0][0] == '-') ||			// if next position in CLI seems to be an option (starting with '-')
			(!argc)						// ... or, we are at the end of CLI
		)) {
			set_option_value(p, (void*)&ONE_INT);		// THEN, it's a boolean parameter without value meaning 'setting to one' (like "-fullscreen" versus "-fullscreen 1")
		} else {						// ELSE, we *NEED* some parameter for the option!
			if (!argc)
				OPT_ERROR_CMDLINE("Option '%s' requires a parameter, but end of command line detected.", argv[-1]);
			const char *s = set_option_value_from_string(p, *argv, argv[-1] + 1);
			if (s) {
				OPT_ERROR_CMDLINE("Option '%s' error:\n%s", p->name, s);
			}
			argc--;
			argv++;
		}
	}
	DEBUGPRINT("CFG: CLI parsing is done without error." NL);
	return 0;
}


static const char *custom_config_file_reader ( struct xemutools_config_st *opt, const char *optname, const char *optvalue )
{
	xemucfg_parse_config_file(optvalue, "Cannot open/read selected config file");
	return NULL;	// hmm, call of xemucfg_parse_config_file() catched errors anyway (hopefully!)
}


static const char skipconfigfile_option_name[] = "skipconfigfile";


static void define_core_options ( void )
{
	// Here must come some self-defining options (if we have any)
	// we want to define anyway, regardless of what an Xemu emulator target defines or not
	xemucfg_define_proc_option("config", custom_config_file_reader, "Use a given config file");
	config_current->flags = XEMUCFG_FLAG_CLI_ONLY;
	// These are for completness only as they are not handled "the normal way", but
	// still nice if help can shows all the options.
	xemucfg_define_dummy_option("help", "Shows this help screen", XEMUCFG_FLAG_CLI_ONLY | XEMUCFG_FLAG_FIRST_ONLY);
	xemucfg_define_dummy_option("version", "Dump version info only and exit", XEMUCFG_FLAG_CLI_ONLY | XEMUCFG_FLAG_FIRST_ONLY);
	xemucfg_define_dummy_option(skipconfigfile_option_name, "Skip loading the default config file", XEMUCFG_FLAG_CLI_ONLY | XEMUCFG_FLAG_FIRST_ONLY);
}


int xemucfg_parse_all ( int argc, char **argv )
{
	// Skip program name
	argc--;
	argv++;
#ifdef XEMU_ARCH_MAC
	// Oh no, another MacOS miss-feature :-O it seems Finder passes a strange parameter to EVERY app it starts!!
	// Skip that, otherwise the user will experience an error box about unknown option and Xemu exits ...
	if (argc && !strncmp(argv[0], "-psn_", 5) && !macos_gui_started) {
		DEBUGPRINT("MACOS: -psn MacOS-madness detected, throwing in some workaround ..." NL);
		argc--;
		argv++;
		macos_gui_started = 1;
		// FIXME: if we know it's a Mac GUI-started app (we have this -psn...)
		// wouldn't it better to skip ALL of the command line parsing?
		// Is there a way in Mac to start program from GUI (finder, or wtf) but
		// still having CLI options what can make it legit not to do so?
	}
#endif
	// Check some hard-coded options (help, version ...)
	// This is done by looking only for the FIRST command line parameter (if there's any ...)
	int skip_config_file = 0;
	if (argc) {
		const char *p = argv[0];
		while (*p == '-' || *p == '/' || *p == '\\')	// skip various option markers, at this point, user may not have idea what is the right syntax yet
			p++;
		if (!strcasecmp(p, "h") || !strcasecmp(p, "help") || !strcasecmp(p, "?")) {
			dump_help();
			return 1;
		} else if (!strcasecmp(p, "v") || !strcasecmp(p, "ver")  || !strcasecmp(p, "version")) {
			DEBUGPRINT("Exiting, because only version information was requested." NL);
			return 1;	// version info. Simply return, as it's already printed out ;)
		} else if (!strcasecmp(p, skipconfigfile_option_name)) {
			// Special option to skip config file parsing. Can be useful if there is an
			// error in the default config file which prevents user to be able to start Xemu.
			skip_config_file = 1;
			// Also "remove" this option from the scope, so cli parser won't see it :)
			argc--;
			argv++;
			// But continue ...
		}
	}
	// Get config template now!
	// We want to save this, but we must before parse, to have
	// the default values. We save it later though!
	char *config_template_string = get_config_template_string_representation();
	// Next, we want to parse default config file! Since we want the command line
	// override this optionally, we do command line parsing after this step only!
	// ... unless user hasn't disabled parsing it, above ...
	if (!skip_config_file) {
		char cfgfn[PATH_MAX];
		sprintf(cfgfn, CONFIG_FILE_USE_NAME, xemu_app_name);
		if (xemucfg_parse_config_file(cfgfn, NULL))
			DEBUGPRINT("CFG: Default config file %s cannot be used" NL, cfgfn);
		else
			DEBUGPRINT("CFG: Default config file %s has been used" NL, cfgfn);
	} else
		DEBUGPRINT("CFG: Default config file is SKIPPED by user request!" NL);
	// And now, we want to parse command line arguments, now it can override already defined values by config file
	int ret = parse_commandline(argc, argv);
	if (!ret) {
		// save the template (only is cli parsing was OK, otherwise Xemu will exit later, anyway)
		char fn[PATH_MAX+1];
		sprintf(fn, CONFIG_FILE_TEMPL_NAME, xemu_app_name);
		xemu_save_file(fn, config_template_string, strlen(config_template_string), "Cannot save config template");
	}
	free(config_template_string);
	return ret;
}


int xemucfg_integer_list_from_string ( const char *value, int *result, int maxitems, const char *delims )
{
	int num = 0;
	if (!value)
		return num;
	char buffer[strlen(value) + 1], *p;
	strcpy(buffer, value);
	p = strtok(buffer, delims);
	while (p) {
		if (num == maxitems)
			return -1;
		result[num++] = atoi(p);
		p = strtok(NULL, delims);
	}
	return num;
}

// XEMU_CONFIGDB_SUPPORT
#endif
