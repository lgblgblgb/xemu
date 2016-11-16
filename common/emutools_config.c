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

#include "emutools.h"
#include "emutools_config.h"
#include <string.h>
#include <errno.h>


static struct emutools_config_st *config_head = NULL;
static struct emutools_config_st *config_tail = NULL;



static inline int check_string_size ( const char *s )
{
	if (!s)
		return 0;
	return strlen(s) >= CONFIG_VALUE_MAX_LENGTH;
}



void emucfg_define_option ( const char *optname, enum emutools_option_type type, void *defval, const char *help )
{
	struct emutools_config_st *p = emu_malloc(sizeof(struct emutools_config_st));
	if (config_head) {
		config_tail->next = p;
		config_tail = p;
	} else
		config_head = config_tail = p;
	if (type == OPT_STR && check_string_size(defval))
		FATAL("Xemu internal config error: too long default option for option '%s'", optname);
	p->next = NULL;
	p->name = emu_strdup(optname);
	p->type = type;
	p->value = (defval && type == OPT_STR) ? emu_strdup(defval) : defval;
	p->help = help;
}


void emucfg_define_bool_option   ( const char *optname, int defval, const char *help ) {
	emucfg_define_option(optname, OPT_BOOL, (void*)(intptr_t)(defval ? 1 : 0), help);
}
void emucfg_define_str_option    ( const char *optname, const char *defval, const char *help ) {
	emucfg_define_option(optname, OPT_STR, (void*)defval, help);
}
void emucfg_define_num_option    ( const char *optname, int defval, const char *help ) {
	emucfg_define_option(optname, OPT_NUM, (void*)(intptr_t)defval, help);
}
void emucfg_define_proc_option   ( const char *optname, emucfg_parser_callback_func_t defval, const char *help ) {
	emucfg_define_option(optname, OPT_PROC, (void*)defval, help);
}
void emucfg_define_switch_option ( const char *optname, const char *help ) {
	emucfg_define_option(optname, OPT_NO, (void*)(intptr_t)0, help);
}



static struct emutools_config_st *search_option ( const char *name_in )
{
	struct emutools_config_st *p = config_head;
	char *name = emu_strdup(name_in);
	char *s = strchr(name, '@');
	if (s)
		*s = 0;
	while (p)
		if (!strcasecmp(name, p->name))
			break;
		else
			p = p->next;
	free(name);
	return (p && s && p->type != OPT_PROC) ? NULL : p;
}



static void dump_help ( void )
{
	struct emutools_config_st *p = config_head;
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



int emucfg_parse_config_file ( const char *filename, int open_can_fail )
{
	int lineno;
	char *p;
	char *cfgmem = emu_malloc(CONFIG_FILE_MAX_SIZE);
	FILE *f = fopen(filename, "rb");
	if (!f) {
		if (open_can_fail)
			return 1;
		FATAL("Config: cannot open config file (%s): %s", filename, strerror(errno));
	}
	lineno = fread(cfgmem, 1, CONFIG_FILE_MAX_SIZE, f);
	if (lineno < 0)
		FATAL("Config: error reading config file (%s): %s", filename, strerror(errno));
	fclose(f);
	if (lineno == CONFIG_FILE_MAX_SIZE)
		FATAL("Config: too long config file (%s), maximum allowed size is %d bytes.", filename, CONFIG_FILE_MAX_SIZE);
	cfgmem[lineno] = 0;	// terminate string
	if (strlen(cfgmem) != lineno)
		FATAL("Config: bad config file (%s), contains NULL character (not a text file)", filename);
	cfgmem = emu_realloc(cfgmem, lineno + 1);
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
			struct emutools_config_st *o;
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
				FATAL("Config file (%s) error at line %d: keyword '%s' is unknown.", filename, lineno, p);
			if (o->type != OPT_NO && !p1)
				FATAL("Config file (%s) error at line %d: keyword '%s' requires a value.", filename, lineno, p);
			switch (o->type) {
				case OPT_STR:
					if (o->value)
						free(o->value);
					if (check_string_size(p1))
						FATAL("Config file (%s) error at line %d: keyword '%s' has too long value", filename, lineno, p);
					o->value = emu_strdup(p1);
					break;
				case OPT_BOOL:
					s = set_boolean_value(p1, &o->value);
					if (s)
						FATAL("Config file (%s) error at line %d: keyword '%s' %s, but '%s' is detected.", filename, lineno, p, s, p1);
					break;
				case OPT_NUM:
					o->value = (void*)(intptr_t)atoi(p1);
					break;
				case OPT_NO:
					if (p1)
						FATAL("Config file (%s) error at line %d: keyword '%s' DOES NOT require any value, but '%s' is detected.", filename, lineno, p, p1);
					o->value = (void*)1;
					break;
				case OPT_PROC:
					s = (*(emucfg_parser_callback_func_t)(o->value))(o, p, p1);
					if (s)
						FATAL("Config file (%s) error at line %d: keyword's '%s' parameter '%s' is invalid: %s", filename, lineno, p, p1, s);
					break;
			}
		}
		// Prepare for next line
		p = pn;	// start of next line (or EOF if NULL)
		lineno++;
	} while (p);
	free(cfgmem);
	return 0;
}



int emucfg_parse_commandline ( int argc, char **argv, const char *only_this )
{
	argc--;
	argv++;
	while (argc) {
		struct emutools_config_st *o;
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
					o->value = emu_strdup(*argv);
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
					s = (*(emucfg_parser_callback_func_t)(o->value))(o, argv[-1] + 1, *argv);
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


static struct emutools_config_st *search_option_query ( const char *name, enum emutools_option_type type )
{
	struct emutools_config_st *p = search_option(name);
	if (!p)
		FATAL("Internal ConfigDB error: not defined option '%s' is queried inside Xemu!", name);
	if (p->type == type || (p->type == OPT_NO && type == OPT_BOOL))
		return p;
	FATAL("Internal ConfigDB error: queried option '%s' with different type as defined inside Xemu!", name);
}



const char *emucfg_get_str ( const char *optname )
{
	return (const char*)(search_option_query(optname, OPT_STR)->value);
}


int emucfg_get_num ( const char *optname )
{
	return (int)(intptr_t)(search_option_query(optname, OPT_NUM)->value);
}


int emucfg_get_bool ( const char *optname )
{
	return (int)(intptr_t)(search_option_query(optname, OPT_BOOL)->value);
}


int emucfg_integer_list_from_string ( const char *value, int *result, int maxitems, const char *delims )
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
