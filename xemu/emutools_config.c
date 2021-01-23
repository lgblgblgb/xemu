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

#include "xemu/emutools.h"
#include "xemu/emutools_files.h"
#include "xemu/emutools_config.h"
#include <string.h>
#include <errno.h>


static struct xemutools_config_st *config_head = NULL;



static inline int check_string_size ( const char *s )
{
	if (!s)
		return 0;
	return strlen(s) >= CONFIG_VALUE_MAX_LENGTH;
}



void xemucfg_define_option ( const char *optname, enum xemutools_option_type type, void *defval, const char *help )
{
	struct xemutools_config_st *entry;
	if (type == OPT_STR && check_string_size(defval))
		FATAL("Xemu internal config error: too long default option for option '%s'", optname);
	entry = xemu_malloc(sizeof(struct xemutools_config_st));
	if (!config_head) {
		config_head = entry;
		entry->next = NULL;
	} else {
		struct xemutools_config_st *p = config_head, *p_prev = NULL;
		for (;;) {
			// we want to manage alphabet sorted list just for nice help output, and for checking re-definition assertion in one step as well
			int ret = strcasecmp(optname, p->name);
			if (!ret)
				FATAL("Xemu internal config error: trying to re-define option '%s'", optname);
			if (ret < 0) {	// we want the first entry already later in alphabet than current one, insert new entry before that!
				entry->next = p;
				if (p_prev)
					p_prev->next = entry;
				else
					config_head = entry;
				break;
			}
			if (p->next) {
				p_prev = p;
				p = p->next;
			} else {
				p->next = entry;
				entry->next = NULL;
				break;
			}

		}
	}
	entry->dereferenced = 0;
	entry->name = xemu_strdup(optname);
	entry->type = type;
	switch (type) {
		case OPT_FLOAT:
			entry->value = xemu_malloc(sizeof(double));
			*(double*)(entry->value) = *(double*)defval;
			break;
		case OPT_STR:
			entry->value = (defval != NULL) ? xemu_strdup(defval) : NULL;
			break;
		default:
			entry->value = defval;
			break;
	}
	//entry->value = (defval && type == OPT_STR) ? xemu_strdup(defval) : defval;
	entry->help = help;
}


void xemucfg_define_bool_option   ( const char *optname, int defval, const char *help ) {
	xemucfg_define_option(optname, OPT_BOOL, (void*)(intptr_t)(defval ? 1 : 0), help);
}
void xemucfg_define_bool_option_multi ( const struct xemutools_configdef_bool_st p[] ) {
	for (int i = 0; p[i].optname; i++)
		xemucfg_define_bool_option(p[i].optname, p[i].defval, p[i].help);
}
void xemucfg_define_str_option    ( const char *optname, const char *defval, const char *help ) {
	xemucfg_define_option(optname, OPT_STR, (void*)defval, help);
}
void xemucfg_define_str_option_multi ( const struct xemutools_configdef_str_st p[] ) {
	for (int i = 0; p[i].optname; i++)
		xemucfg_define_str_option(p[i].optname, p[i].defval, p[i].help);
}
void xemucfg_define_num_option    ( const char *optname, int defval, const char *help ) {
	xemucfg_define_option(optname, OPT_NUM, (void*)(intptr_t)defval, help);
}
void xemucfg_define_num_option_multi ( const struct xemutools_configdef_num_st p[] ) {
	for (int i = 0; p[i].optname; i++)
		xemucfg_define_num_option(p[i].optname, p[i].defval, p[i].help);
}
void xemucfg_define_float_option  ( const char *optname, double defval, const char *help ) {
	xemucfg_define_option(optname, OPT_FLOAT, &defval, help);
}
void xemucfg_define_float_option_multi ( const struct xemutools_configdef_float_st p[] ) {
	for (int i = 0; p[i].optname; i++)
		xemucfg_define_float_option(p[i].optname, p[i].defval, p[i].help);
}
void xemucfg_define_proc_option   ( const char *optname, xemucfg_parser_callback_func_t cb, const char *help ) {
	xemucfg_define_option(optname, OPT_PROC, (void*)cb, help);
}
void xemucfg_define_proc_option_multi ( const struct xemutools_configdef_proc_st p[] ) {
	for (int i = 0; p[i].optname; i++)
		xemucfg_define_proc_option(p[i].optname, p[i].cb, p[i].help);
}
void xemucfg_define_switch_option ( const char *optname, const char *help ) {
	xemucfg_define_option(optname, OPT_NO, (void*)(intptr_t)0, help);
}
void xemucfg_define_switch_option_multi ( const struct xemutools_configdef_switch_st p[] ) {
	for (int i = 0; p[i].optname; i++)
		xemucfg_define_switch_option(p[i].optname, p[i].help);
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
	return (p && s && p->type != OPT_PROC) ? NULL : p;
}



static void dump_help ( void )
{
	struct xemutools_config_st *p = config_head;
	printf("Available command line options:" NL NL);
	while (p) {
		const char *t = "";
		switch (p->type) {
			case OPT_NO:	t = "NO ARG";	break;
			case OPT_BOOL:	t = "bool";	break;
			case OPT_NUM:	t = "int-num";	break;
			case OPT_FLOAT:	t = "float-num";break;
			case OPT_STR:	t = "str";	break;
			case OPT_PROC:	t = "spec";	break;
		}
		printf("-%-16s  (%s) %s" NL, p->name, t, p->help ? p->help : "(no help is available)");
		p = p->next;
	}
}


#define OPT_ERROR_CMDLINE(...) do { ERROR_WINDOW("Command line error: " __VA_ARGS__); return 1; } while(0)


static const char *set_boolean_value ( const char *str, void **set_this )
{
	if (!strcasecmp(str, "yes") || !strcasecmp(str, "on") || !strcmp(str, "1") || !strcasecmp(str, "true")) {
		*set_this = (void*)1;
		return NULL;
	}
	if (!strcasecmp(str, "no") || !strcasecmp(str, "off") || !strcmp(str, "0") || !strcasecmp(str, "false")) {
		*set_this = (void*)0;
		return NULL;
	}
	return "needs a boolean parameter (0/1 or off/on or no/yes)";
}


static char *get_config_string_representation ( const char *initial_part )
{
	struct xemutools_config_st *cfg = config_head;
	char *out = xemu_strdup(initial_part ? initial_part : "");
	while (cfg) {
		char buffer[256 + PATH_MAX];
		int r = 0;	// sigh, gcc is stupid, it must be set, even if it see all cases are handled later ...
		switch (cfg->type) {
			case OPT_STR:
				r = snprintf(buffer, sizeof buffer, NL "## %s" NL "## (string param)" NL "%s%s = %s" NL,
					cfg->help,
					cfg->value ? "" : "#",
					cfg->name,
					cfg->value ? (const char *)cfg->value : ""
				);
				break;
			case OPT_NO:
				r = snprintf(buffer, sizeof buffer, NL "## %s" NL "## (no param at all, use the option only)" NL "%s%s" NL,
					cfg->help,
					cfg->value ? "" : "#",
					cfg->name
				);
				break;
			case OPT_BOOL:
				r = snprintf(buffer, sizeof buffer, NL "## %s" NL "## (boolean param)" NL "%s = %s" NL,
					cfg->help,
					cfg->name,
					(int)(intptr_t)cfg->value ? "yes" : "no"
				);
				break;
			case OPT_NUM:
				r = snprintf(buffer, sizeof buffer, NL "## %s" NL "## (integer param)" NL "%s = %d" NL,
					cfg->help,
					cfg->name,
					(int)(intptr_t)cfg->value
				);
				break;
			case OPT_FLOAT:
				r = snprintf(buffer, sizeof buffer, NL "## %s" NL "## (real-num param)" NL "%s = %f" NL,
					cfg->help,
					cfg->name,
					*(double*)cfg->value
				);
				break;
			case OPT_PROC:
				r = snprintf(buffer, sizeof buffer, NL "## %s" NL "## (SPECIAL)" NL "#%s" NL,
					cfg->help,
					cfg->name
				);
				break;
		}
		if (r >= sizeof(buffer) - 1) {
			free(out);
			FATAL("Too large result for dumping config option '%s'!" NL, cfg->name);
		}
		out = xemu_realloc(out, strlen(out) + strlen(buffer) + 1);
		strcpy(out + strlen(out), buffer);
		cfg = cfg->next;
	}
	if (strlen(out) > CONFIG_FILE_MAX_SIZE) {
		free(out);
		FATAL("Too large config file generated in %s", __func__);
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
		"# ----" NL
		"# Rules: basically option = value syntax." NL
		"# 'switch' options does not have value, so you mustn't write the '= value' part." NL
		"# Option values expecting file names can start with letters '@' or '#'." NL
		"# In case of '@' the rest of the filename/path is interpreted as relative to the" NL
		"# preferences directory. In case of '#', the rest of the filename/path will be searched" NL
		"# at some common places (including the preferences directory, but also in the same directory" NL
		"# as the binary is, or in case of UNIX-like OS, even the common data directory)" NL
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


int xemucfg_parse_config_file ( const char *filename_in, int open_can_fail )
{
	char *p, cfgmem[CONFIG_FILE_MAX_SIZE + 1];
	int  lineno = xemu_load_file(filename_in, cfgmem, 0, CONFIG_FILE_MAX_SIZE, open_can_fail ? NULL : "Cannot load specified configuration file.");
	if (lineno < 0) {
		if (open_can_fail)
			return 1;
		XEMUEXIT(1);
	}
	cfgmem[lineno] = 0;	// terminate string
	if (strlen(cfgmem) != lineno)
		FATAL("Config: bad config file (%s), contains NULL character (not a text file)", xemu_load_filepath);
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
			printf("Line#%d = \"%s\",\"%s\"" NL, lineno, p, p1 ? p1 : "<no-specified>");
			o = search_option(p);
			if (!o)
				FATAL("Config file (%s) error at line %d: keyword '%s' is unknown.", xemu_load_filepath, lineno, p);
			if (o->type != OPT_NO && !p1)
				FATAL("Config file (%s) error at line %d: keyword '%s' requires a value.", xemu_load_filepath, lineno, p);
			switch (o->type) {
				case OPT_STR:
					if (o->value)
						free(o->value);
					if (check_string_size(p1))
						FATAL("Config file (%s) error at line %d: keyword '%s' has too long value", xemu_load_filepath, lineno, p);
					o->value = xemu_strdup(p1);
					break;
				case OPT_BOOL:
					s = set_boolean_value(p1, &o->value);
					if (s)
						FATAL("Config file (%s) error at line %d: keyword '%s' %s, but '%s' is detected.", xemu_load_filepath, lineno, p, s, p1);
					break;
				case OPT_NUM:
					o->value = (void*)(intptr_t)atoi(p1);
					break;
				case OPT_FLOAT:
					*(double*)(o->value) = atof(p1);
					break;
				case OPT_NO:
					if (p1)
						FATAL("Config file (%s) error at line %d: keyword '%s' DOES NOT require any value, but '%s' is detected.", xemu_load_filepath, lineno, p, p1);
					o->value = (void*)1;
					break;
				case OPT_PROC:
					s = (*(xemucfg_parser_callback_func_t)(o->value))(o, p, p1);
					if (s)
						FATAL("Config file (%s) error at line %d: keyword '%s' is invalid: %s", xemu_load_filepath, lineno, p, s);
					break;
			}
		}
		// Prepare for next line
		p = pn;	// start of next line (or EOF if NULL)
		lineno++;
	} while (p);
	return 0;
}


static int xemucfg_parse_commandline ( int argc, char **argv, const char *only_this )
{
	// Prepare the config template, since we want before modifying the default values by CLI/config file
	// However we want to save it later, if parsing was OK at all, eg we don't want to write files on help
	// requests, etc!
	static int template_needs_to_be_saved = 1;
	static char *template_string = NULL;
	if (!template_string && template_needs_to_be_saved)
		template_string = get_config_template_string_representation();
	// Skip arg-0, which is program name ...
	argc--;
	argv++;
#ifdef XEMU_ARCH_MAC
	// Oh no, another MacOS miss-feature :-O it seems Finder passes a strange parameter to EVERY app it starts!!
	// Skip that, otherwise the user will experience an error box about unknown option and Xemu exits ...
	if (argc && !strncmp(argv[0], "-psn_", 5)) {
		argc--;
		argv++;
		macos_gui_started = 1;
	}
#endif
	while (argc) {
		struct xemutools_config_st *o;
		if (*argv[0] != '/' && *argv[0] != '-')
			OPT_ERROR_CMDLINE("Invalid option '%s', must start with '-'", *argv);
		if (!strcasecmp(*argv + 1, "h") || !strcasecmp(*argv + 1, "help") || !strcasecmp(*argv + 1, "-help") || !strcasecmp(*argv + 1, "?")) {
			if (!only_this || (only_this && !strcasecmp(only_this, "help"))) {
				dump_help();
				return 1;
			}
			argc--;
			argv++;
			continue;
		}
		o = search_option(*argv + 1);
		if (!o)
			OPT_ERROR_CMDLINE("Unknown option '%s'", *argv);
		argc--;
		argv++;
		if (o->type == OPT_NO) {
			if (only_this && strcasecmp(only_this, o->name))
				continue;
			o->value = (void*)1;
		} else {
			const char *s;
			if (!argc)
				OPT_ERROR_CMDLINE("Option '%s' requires a parameter, but end of command line detected.", argv[-1]);
			if (only_this && strcasecmp(only_this, o->name))
				goto do_not;
			switch (o->type) {
				case OPT_STR:
					if (o->value)
						free(o->value);
					if (check_string_size(*argv))
						OPT_ERROR_CMDLINE("Option '%s' has too long value.", argv[1]);
					o->value = xemu_strdup(*argv);
					break;
				case OPT_BOOL:
					s = set_boolean_value(*argv, &o->value);
					if (s)
						OPT_ERROR_CMDLINE("Option '%s' %s, but '%s' is detected.", argv[-1], s, *argv);
					break;
				case OPT_NUM:
					o->value = (void*)(intptr_t)atoi(*argv);
					break;
				case OPT_FLOAT:
					*(double*)(o->value) = atof(*argv);
					break;
				case OPT_PROC:
					s = (*(xemucfg_parser_callback_func_t)(o->value))(o, argv[-1] + 1, *argv);
					if (s)
						OPT_ERROR_CMDLINE("Option '%s' is invalid: %s", argv[-1], s);
					break;
				case OPT_NO:
					break;	// make GCC happy to handle all cases ...
			}
		do_not:
			argc--;
			argv++;
		}
	}
	DEBUGPRINT("CFG: CLI parsing done (%s)" NL, only_this ? only_this : "ALL options");
	if (template_string && template_needs_to_be_saved) {
		char fn[PATH_MAX+1];
		sprintf(fn, CONFIG_FILE_TEMPL_NAME, xemu_app_name);
		template_needs_to_be_saved = 0;
		xemu_save_file(fn, template_string, strlen(template_string), "Cannot save config template");
		free(template_string);
		template_string = NULL;
	}
	return 0;
}



int xemucfg_parse_all ( int argc, char **argv )
{
	char cfgfn[PATH_MAX];
	if (xemucfg_parse_commandline(argc, argv, "help"))
		return 1;
	sprintf(cfgfn, CONFIG_FILE_USE_NAME, xemu_app_name);
	if (xemucfg_parse_config_file(cfgfn, 1))
		DEBUGPRINT("CFG: Default config file %s cannot be used" NL, cfgfn);
	else
		DEBUGPRINT("CFG: Default config file %s has been used" NL, cfgfn);
	int ret = xemucfg_parse_commandline(argc, argv, NULL);
	if (!ret)
		atexit(xemucfg_print_not_dereferenced_items);
	return ret;
}


int xemucfg_has_option ( const char *name )
{
	return (search_option(name) != NULL);
}


static int get_not_dereferenced_items ( int to_print )
{
	struct xemutools_config_st *p = config_head;
	int cnt = 0;
	if (to_print)
		DEBUGPRINT("CFG: not dereferenced config items:");
	while (p) {
		if (!p->dereferenced) {
			cnt++;
			if (to_print)
				DEBUGPRINT(" %s", p->name);
		}/* else
			DEBUGPRINT("Nice, %s has been dereferenced %d times" NL, p->name, p->dereferenced);*/
		p = p->next;
	}
	if (to_print)
		DEBUGPRINT(" (%d items)" NL, cnt);
	return cnt;
}


void xemucfg_print_not_dereferenced_items ( void )
{
	if (get_not_dereferenced_items(0))
		get_not_dereferenced_items(1);
}


static struct xemutools_config_st *search_option_query ( const char *name, enum xemutools_option_type type )
{
	struct xemutools_config_st *p = search_option(name);
	if (!p)
		FATAL("Internal ConfigDB error: not defined option '%s' is queried inside Xemu!", name);
	if (p->type == type || (p->type == OPT_NO && type == OPT_BOOL)) {
		p->dereferenced++;
		return p;
	}
	FATAL("Internal ConfigDB error: queried option '%s' with different type as defined inside Xemu!", name);
}


const char *xemucfg_get_str ( const char *optname ) {
	return (const char*)(search_option_query(optname, OPT_STR)->value);
}
int xemucfg_get_num ( const char *optname ) {
	return (int)(intptr_t)(search_option_query(optname, OPT_NUM)->value);
}
int xemucfg_get_ranged_num ( const char *optname, int min, int max ) {
	int ret = xemucfg_get_num(optname);
	if (ret < min) {
		WARNING_WINDOW("Bad value (%d) for option '%s': must not be smaller than %d.\nUsing the minimal value.", ret, optname, min);
		ret = min;
	}
	if (ret > max) {
		WARNING_WINDOW("Bad value (%d) for option '%s': must not be larger than %d.\nUsing the maximal value.", ret, optname, max);
		ret = max;
	}
	return ret;
}
int xemucfg_get_bool ( const char *optname ) {
	return BOOLEAN_VALUE((int)(intptr_t)(search_option_query(optname, OPT_BOOL)->value));
}
double xemucfg_get_float ( const char *optname ) {
	return *(double*)(search_option_query(optname, OPT_FLOAT)->value);
}
double xemucfg_get_ranged_float ( const char *optname, double min, double max ) {
	double ret = xemucfg_get_float(optname);
	if (ret < min) {
		WARNING_WINDOW("Bad value (%f) for option '%s': must not be smaller than %f.\nUsing the minimal value.", ret, optname, min);
		ret = min;
	}
	if (ret > max) {
		WARNING_WINDOW("Bad value (%f) for option '%s': must not be larger than %f.\nUsing the maximal value.", ret, optname, max);
		ret = max;
	}
	return ret;
}


int xemucfg_integer_list_from_string ( const char *value, int *result, int maxitems, const char *delims )
{
	char buffer[CONFIG_VALUE_MAX_LENGTH], *p;
	int num = 0;
	if (!value)
		return num;
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
