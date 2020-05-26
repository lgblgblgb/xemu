/* Part of the Xemu project, please visit: https://github.com/lgblgblgb/xemu
   Copyright (C)2016,2019 LGB (Gábor Lénárt) <lgblgblgb@gmail.com>

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
			"%s\n"
			"Compiled by: %s at %s\n"
			"Built with: %s for %s\n"
			"\n"
			"Copyright (C)" COPYRIGHT_YEARS " Gábor Lénárt (aka LGB) lgb@lgb.hu http://lgb.hu/\n"
			"This software is part of the Xemu project: https://github.com/lgblgblgb/xemu\n"
			"\n"
			"This software is a GNU/GPL version 2 (or later) software.\n"
			"<http://gnu.org/licenses/gpl.html>\n"
			"This is free software; you are free to change and redistribute it.\n"
			"There is NO WARRANTY, to the extent permitted by law."
			,
			TARGET_DESC, XEMU_BUILDINFO_CDATE,
			XEMU_BUILDINFO_GIT,
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
	if (query) {
		if (sysconsole_is_open)
			*query |= XEMUGUI_MENUFLAG_CHECKED;
	} else
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


void xemugui_cb_scanlines ( const struct menu_st *m, int *query )
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
	xemu_set_scanlines(mode_spec);
}

