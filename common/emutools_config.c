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

static struct emutools_config_st *config_head = NULL;
static struct emutools_config_st *config_tail = NULL;


void emucfg_define_option ( const char *optname, enum emutools_option_type type, void *defval, const char *help )
{
	struct emutools_config_st *p = emu_malloc(sizeof(struct emutools_config_st));
	if (config_head) {
		config_tail->next = p;
		config_tail = p;
	} else
		config_head = config_tail = p;
	p->next = NULL;
	p->name = emu_strdup(optname);
	p->type = type;
	p->value = (defval && type == OPT_STR) ? emu_strdup(defval) : defval;
	p->help = help;
}


static struct emutools_config_st *search_option ( const char *name )
{
	struct emutools_config_st *p = config_head;
	while (p)
		if (!strcasecmp(name, p->name))
			break;
		else
			p = p->next;
	return p;
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
		}
		printf("-%-16s  (%s) %s" NL, p->name, t, p->help ? p->help : "(no help is available)");
		p = p->next;
	}
}


#define OPT_ERROR(...) do { ERROR_WINDOW("Command line error: " __VA_ARGS__); return 1; } while(0)



int emucfg_parse_commandline ( int argc, char **argv )
{
	argc--;
	argv++;
	while (argc) {
		struct emutools_config_st *p;
		if (*argv[0] != '/' && *argv[0] != '-')
			OPT_ERROR("Invalid option '%s', must start with '-'", *argv);
		if (!strcasecmp(*argv + 1, "h") || !strcasecmp(*argv + 1, "help") || !strcasecmp(*argv + 1, "-help") || !strcasecmp(*argv + 1, "?")) {
			dump_help();
			return 1;
		}
		p = search_option(*argv + 1);
		if (!p)
			OPT_ERROR("Unknown option '%s'", *argv);
		argc--;
		argv++;
		if (p->type == OPT_NO)
			p->value = (void*)1;
		else {
			if (!argc)
				OPT_ERROR("Option '%s' requires a parameter, but end of command line detected.", argv[-1]);
			switch (p->type) {
				case OPT_STR:
					if (p->value)
						free(p->value);
					p->value = emu_strdup(*argv);
					break;
				case OPT_BOOL:
					if (!strcasecmp(*argv, "yes") || !strcasecmp(*argv, "on") || !strcasecmp(*argv, "1"))
						p->value = (void*)1;
					else if (!strcasecmp(*argv, "no") || !strcasecmp(*argv, "off") || !strcasecmp(*argv, "0"))
						p->value = (void*)0;
					else
						OPT_ERROR("Option '%s' needs a boolean parameter (0/1 or on/off or yes/no), but '%s' is detected.", argv[-1], *argv);
					break;
				case OPT_NUM:
					p->value = (void*)(intptr_t)atoi(*argv);
					break;
				case OPT_NO:
					break;	// make GCC happy to handle all cases ...
			}
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
	struct emutools_config_st *p = search_option_query(optname, OPT_STR);
	return (const char*)p->value;
}


int emucfg_get_num ( const char *optname )
{
	struct emutools_config_st *p = search_option_query(optname, OPT_NUM);
	return (int)(intptr_t)p->value;
}


int  emucfg_get_bool ( const char *optname )
{
	struct emutools_config_st *p = search_option_query(optname, OPT_BOOL);
	return (int)(intptr_t)p->value;
}
