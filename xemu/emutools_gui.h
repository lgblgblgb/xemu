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

#ifndef __COMMON_EMUTOOLS_GUI_H_INCLUDED
#define __COMMON_EMUTOOLS_GUI_H_INCLUDED

#define XEMUGUI_FSEL_DIRECTORY		0
#define XEMUGUI_FSEL_OPEN		1
#define XEMUGUI_FSEL_SAVE		2
#define XEMUGUI_FSEL_FLAG_STORE_DIR	0x100

struct menu_st {
	const char *name;
	enum {
		CALLABLE, SUBMENU, TITLE, SEPARATOR
	} type;
	const void *handler;
	const void *user_data;
};

extern int is_xemungui_ok;

extern int  xemugui_init		( const char *name );
extern void xemugui_shutdown		( void );
extern int  xemugui_iteration		( void );
extern int  xemugui_file_selector	( int dialog_mode, const char *dialog_title, char *default_dir, char *selected, int path_max_size );
extern int  xemugui_popup		( const struct menu_st desc[] );

#endif
