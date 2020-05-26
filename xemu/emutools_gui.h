/* Part of the Xemu project, please visit: https://github.com/lgblgblgb/xemu
   Copyright (C)2016,2019-2020 LGB (Gábor Lénárt) <lgblgblgb@gmail.com>

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

#ifndef XEMU_XEMU_EMUTOOLS_GUI_H_INCLUDED
#define XEMU_XEMU_EMUTOOLS_GUI_H_INCLUDED

#define DEBUGGUI        DEBUGPRINT
//#define DEBUGGUI      DEBUG
//#define DEBUGGUI(...)

#define XEMUGUI_FSEL_DIRECTORY		0
#define XEMUGUI_FSEL_OPEN		1
#define XEMUGUI_FSEL_SAVE		2
#define XEMUGUI_FSEL_FLAG_STORE_DIR	0x100

#define XEMUGUI_MENUID_CALLABLE		0
#define XEMUGUI_MENUID_SUBMENU		1
#define XEMUGUI_MENUID_TITLE		2

#define XEMUGUI_MENUFLAG_INACTIVE	0x0100
#define XEMUGUI_MENUFLAG_BEGIN_RADIO	0x0200
#define XEMUGUI_MENUFLAG_END_RADIO	0x0400
#define XEMUGUI_MENUFLAG_ACTIVE_RADIO	0x0800
#define XEMUGUI_MENUFLAG_SEPARATOR	0x1000
#define XEMUGUI_MENUFLAG_CHECKED	0x2000
#define XEMUGUI_MENUFLAG_QUERYBACK	0x4000

#ifndef XEMUGUI_MAX_SUBMENUS
#define XEMUGUI_MAX_SUBMENUS		100
#endif
#ifndef XEMUGUI_MAX_ITEMS
#define XEMUGUI_MAX_ITEMS		900
#endif

struct menu_st {
	const char *name;
	int type;
	const void *handler;
	const void *user_data;
};

typedef void (*xemugui_callback_t)(const struct menu_st *desc, int *query);

extern int is_xemungui_ok;

extern int  xemugui_init		( const char *name );
extern void xemugui_shutdown		( void );
extern int  xemugui_iteration		( void );
extern int  xemugui_file_selector	( int dialog_mode, const char *dialog_title, char *default_dir, char *selected, int path_max_size );

extern int  xemugui_popup		( const struct menu_st desc[] );

extern void xemugui_cb_call_user_data		( const struct menu_st *m, int *query );
extern void xemugui_cb_call_user_data_if_sure	( const struct menu_st *m, int *query );
extern void xemugui_cb_quit			( const struct menu_st *m, int *query );
extern void xemugui_cb_call_quit_if_sure	( const struct menu_st *m, int *query );
extern void xemugui_cb_show_info_window_text	( const struct menu_st *m, int *query );
extern void xemugui_cb_about_window		( const struct menu_st *m, int *query );
#ifdef XEMU_ARCH_WIN
extern void xemugui_cb_sysconsole		( const struct menu_st *m, int *query );
#endif
extern void xemugui_cb_windowsize		( const struct menu_st *m, int *query );
extern void xemugui_cb_scanlines		( const struct menu_st *m, int *query );

#endif
