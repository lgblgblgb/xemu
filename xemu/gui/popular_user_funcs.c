/* Part of the Xemu project, please visit: https://github.com/lgblgblgb/xemu
   ~/xemu/gui/popular_user_funcs.c: popular/common functions for Xemu's UI abstraction
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



#define UI_CB_QUIT()	exit(0)
#define UI_CB_QUIT_()	do {	\
	SDL_Event ev;		\
	ev.type = SDL_QUIT;	\
	SDL_PushEvent(&ev);	\
} while(0)


void xemugui_cb_call_user_data ( const struct menu_st *m, int *query )
{
	if (!query)
		((void(*)(void))(m->user_data))();
}

void xemugui_cb_call_user_data_if_sure ( const struct menu_st *m, int *query )
{
	if (!query && ARE_YOU_SURE(NULL, 0))
		((void(*)(void))(m->user_data))();
}

void xemugui_cb_quit ( const struct menu_st *m, int *query )
{
	if (!query)
		UI_CB_QUIT();
}

void xemugui_cb_show_info_window_text ( const struct menu_st *m, int *query )
{
	if (!query)
		INFO_WINDOW("%s", (const char*)m->user_data);
}

void xemugui_cb_about_window ( const struct menu_st *m, int *query )
{
	if (!query) {
		INFO_WINDOW(
			"Xemu/%s %s\n"
			"%s (%s)\n"
			"Compiled by: %s at %s\n"
			"Built with: %s for %s\n"
			"\n"
			"Copyright (C)" COPYRIGHT_YEARS " Gábor Lénárt (aka LGB) lgb@lgb.hu\nhttp://lgb.hu/\n"
			"This software is part of the Xemu project:\nhttps://github.com/lgblgblgb/xemu\n"
			"\n"
			"This software is a GNU/GPL version 2 (or later) software.\n"
			"<http://gnu.org/licenses/gpl.html>\n"
			"This is free software; you are free to change and redistribute it.\n"
			"There is NO WARRANTY, to the extent permitted by law."
			,
			TARGET_DESC, XEMU_BUILDINFO_CDATE,
			XEMU_BUILDINFO_GIT,
			xemu_is_official_build() ? "official-build" : "custom-build",
			XEMU_BUILDINFO_ON, XEMU_BUILDINFO_AT,
			XEMU_BUILDINFO_CC, XEMU_ARCH_NAME
		);
	}
}

void xemugui_cb_call_quit_if_sure ( const struct menu_st *m, int *query )
{
	if (!query && ARE_YOU_SURE(str_are_you_sure_to_exit, i_am_sure_override | ARE_YOU_SURE_DEFAULT_YES))
		UI_CB_QUIT();
}


#ifdef XEMU_ARCH_WIN
void xemugui_cb_sysconsole ( const struct menu_st *m, int *query )
{
	if (query)
		*query |= (sysconsole_is_open ? XEMUGUI_MENUFLAG_CHECKED : XEMUGUI_MENUFLAG_UNCHECKED);
	else
		sysconsole_toggle(-1);
}
#endif


void xemugui_cb_windowsize ( const struct menu_st *m, int *query )
{
	int mode_spec = (int)(uintptr_t)m->user_data;
#if 0
	static int last_mode = 0;
	if (query) {
		if (mode_spec == last_mode)
			*query |= XEMUGUI_MENUFLAG_ACTIVE_RADIO;
		return;
	}
	last_mode =  mode_spec;
#endif
	xemu_set_screen_mode(mode_spec);
}

#ifdef HAVE_XEMU_EXEC_API
#include "xemu/emutools_files.h"
void xemugui_cb_native_os_prefdir_browser ( const struct menu_st *m, int *query )
{
	if (!query)
		xemuexec_open_native_file_browser(sdl_pref_dir);
}
static void _open_url ( const char *url_in, const char *par_list[] )
{
	if (ARE_YOU_SURE("Can I open a web browser instance/window/tab to be able to serve your request?", i_am_sure_override | ARE_YOU_SURE_DEFAULT_YES)) {
		char buffer[2048];
		char *b = buffer + sprintf(buffer, "%s", url_in);
		for (int i = 0; par_list && par_list[i]; i++)
			if (!(i & 1))
				b += sprintf(b, "%c%s", !i ? '?' : '&', par_list[i]);
			else {
				*b++ = '=';
				const char *u = par_list[i];
				while (*u)
					if ((*u >= 'a' && *u <= 'z') || (*u >= '0' && *u <= '9') || (*u >= 'A' && *u <= 'Z'))
						*b++ = *u++;
					else
						b += sprintf(b, "%%%02X", (unsigned char)(*u++));
			}
		*b = '\0';
		DEBUGPRINT("BROWSER: requesting web resource to open: %s" NL, buffer);
		xemuexec_open_native_file_browser(buffer);
	}
}
void xemugui_cb_web_url ( const struct menu_st *m, int *query )
{
	if (!query)
		_open_url((char*)m->user_data, NULL);
}
#include <time.h>
#include "xemu/online_resources.h"
void xemugui_cb_web_help_main ( const struct menu_st *m, int *query )
{
	if (query)
		return;
	char par[512];
	sprintf(par, "o=%d\001v=%s\001b=%s\001t=%s\001T=%s\001p=%s\001u=" PRINTF_LLD "\001x=%s\002chk",	// normal param list MUST end with \002 for future extension!
		xemu_is_official_build(),		// o=%d (official build?)
		XEMU_BUILDINFO_CDATE,			// v=%s (version data - formed from commit date actually)
		XEMU_BUILDINFO_GIT,			// b=%s (build info)
		TARGET_NAME,				// t=%s (target name)
		TARGET_DESC,				// T=%s (target description)
		XEMU_ARCH_NAME,				// p=%s (platform name)
		(long long int)time(NULL),		// u=%d (uts of the current time)
		m->user_data != NULL ? (const char*)(m->user_data) : "null"	// x=%s (user defined command)
	);
	const char *par_list[] = { XEMU_ONLINE_HELP_GET_VAR, par, NULL };
	_open_url(XEMU_ONLINE_HELP_HANDLER_URL, par_list);
}
#endif

#include "xemu/emutools_hid.h"
void xemugui_cb_osd_key_debugger ( const struct menu_st *m, int *query )
{
	XEMUGUI_RETURN_CHECKED_ON_QUERY(query, hid_show_osd_keys);
	hid_show_osd_keys = !hid_show_osd_keys;
	OSD(-1, -1, "OSD key debugger has been %sABLED", hid_show_osd_keys ? "EN" : "DIS");
}

void xemugui_cb_set_mouse_grab ( const struct menu_st *m, int *query )
{
	XEMUGUI_RETURN_CHECKED_ON_QUERY(query, allow_mouse_grab);
	allow_mouse_grab = !allow_mouse_grab;
	static int first_warning = 1;
	if (allow_mouse_grab && first_warning) {
		first_warning = 0;
		INFO_WINDOW("Mouse grab mode has been enabled.\nLeft click into the emulator window to initiate.\nPress both SHIFTs together to cancel.");
	}
}

void xemugui_cb_set_integer_to_one ( const struct menu_st *m, int *query )
{
	if (!query)
		*(int*)(m->user_data) = 1;
}
