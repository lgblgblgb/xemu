/* Xemu - Somewhat lame emulation (running on Linux/Unix/Windows/OSX, utilizing
   SDL2) of some 8 bit machines, including the Commodore LCD and Commodore 65
   and the MEGA65 as well.
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
	entry->name = xemu_strdup(optname);
	entry->type = type;
	entry->value = (defval && type == OPT_STR) ? xemu_strdup(defval) : defval;
	entry->help = help;
}


void xemucfg_define_bool_option   ( const char *optname, int defval, const char *help ) {
	xemucfg_define_option(optname, OPT_BOOL, (void*)(intptr_t)(defval ? 1 : 0), help);
}
void xemucfg_define_str_option    ( const char *optname, const char *defval, const char *help ) {
	xemucfg_define_option(optname, OPT_STR, (void*)defval, help);
}
void xemucfg_define_num_option    ( const char *optname, int defval, const char *help ) {
	xemucfg_define_option(optname, OPT_NUM, (void*)(intptr_t)defval, help);
}
void xemucfg_define_proc_option   ( const char *optname, xemucfg_parser_callback_func_t defval, const char *help ) {
	xemucfg_define_option(optname, OPT_PROC, (void*)defval, help);
}
void xemucfg_define_switch_option ( const char *optname, const char *help ) {
	xemucfg_define_option(optname, OPT_NO, (void*)(intptr_t)0, help);
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
			case OPT_NO: t = "NO ARG"; break;
			case OPT_BOOL: t = "bool"; break;
			case OPT_NUM: t = "num"; break;
			case OPT_STR: t = "str"; break;
			case OPT_PROC: t = "spec"; break;
		}
		printf("-%-16s  (%s) %s" NL, p->name, t, p->help ? p->help : "(no help is available)");
		p = p->next;
	}
}


#define OPT_ERROR_CMDLINE(...) do { ERROR_WINDOW("Command line error: " __VA_ARGS__); return 1; } while(0)


static const char *set_boolean_value ( const char *str, void **set_this )
{
	if (!strcasecmp(str, "yes") || !strcasecmp(str, "on") || !strcmp(str, "1")) {
		*set_this = (void*)1;
		return NULL;
	}
	if (!strcasecmp(str, "no") || !strcasecmp(str, "off") || !strcmp(str, "0")) {
		*set_this = (void*)0;
		return NULL;
	}
	return "needs a boolean parameter (0/1 or off/on or no/yes)";
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
				case OPT_NO:
					if (p1)
						FATAL("Config file (%s) error at line %d: keyword '%s' DOES NOT require any value, but '%s' is detected.", xemu_load_filepath, lineno, p, p1);
					o->value = (void*)1;
					break;
				case OPT_PROC:
					s = (*(xemucfg_parser_callback_func_t)(o->value))(o, p, p1);
					if (s)
						FATAL("Config file (%s) error at line %d: keyword's '%s' parameter '%s' is invalid: %s", xemu_load_filepath, lineno, p, p1, s);
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
				case OPT_PROC:
					s = (*(xemucfg_parser_callback_func_t)(o->value))(o, argv[-1] + 1, *argv);
					if (s)
						OPT_ERROR_CMDLINE("Option's '%s' parameter '%s' is invalid: %s", argv[-1], *argv, s);
					break;
				case OPT_NO:
					break;	// make GCC happy to handle all cases ...
			}
		do_not:
			argc--;
			argv++;
		}
	}
	return 0;
}



int xemucfg_parse_all ( int argc, char **argv )
{
	char cfgfn[PATH_MAX];
	if (xemucfg_parse_commandline(argc, argv, "help"))
		return 1;
	sprintf(cfgfn, "@%s-default.cfg", xemu_app_name);
	if (xemucfg_parse_config_file(cfgfn, 1))
		DEBUGPRINT("CFG: Default config file %s cannot be used" NL, cfgfn);
	else
		DEBUGPRINT("CFG: Default config file %s has been used" NL, cfgfn);
	return xemucfg_parse_commandline(argc, argv, NULL);
}


int xemucfg_has_option ( const char *name )
{
	return (search_option(name) != NULL);
}

static struct xemutools_config_st *search_option_query ( const char *name, enum xemutools_option_type type )
{
	struct xemutools_config_st *p = search_option(name);
	if (!p)
		FATAL("Internal ConfigDB error: not defined option '%s' is queried inside Xemu!", name);
	if (p->type == type || (p->type == OPT_NO && type == OPT_BOOL))
		return p;
	FATAL("Internal ConfigDB error: queried option '%s' with different type as defined inside Xemu!", name);
}


const char *xemucfg_get_str ( const char *optname )
{
	return (const char*)(search_option_query(optname, OPT_STR)->value);
}


int xemucfg_get_num ( const char *optname )
{
	return (int)(intptr_t)(search_option_query(optname, OPT_NUM)->value);
}


int xemucfg_get_bool ( const char *optname )
{
	return BOOLEAN_VALUE((int)(intptr_t)(search_option_query(optname, OPT_BOOL)->value));
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
