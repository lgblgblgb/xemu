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


/* ---------------------------------------- LINUX/UNIX STUFFS based on GTK ---------------------------------------- */

#include <gtk/gtk.h>

#define USE_OLD_GTK_POPUP

static int _gtkgui_active = 0;
static int _gtkgui_popup_is_open = 0;

static struct {
	int num_of_menus;
	GtkWidget *menus[XEMUGUI_MAX_SUBMENUS];
	int problem;
} xemugtkmenu;


static int xemugtkgui_iteration ( void )
{
	if (XEMU_UNLIKELY(_gtkgui_active && is_xemugui_ok)) {
		int n = 0;
		while (gtk_events_pending()) {
			gtk_main_iteration();
			n++;
		}
		if (n > 0)
			DEBUGGUI("GUI: GTK used %d iterations." NL, n);
		if (n == 0 && _gtkgui_active == 2) {
			_gtkgui_active = 0;
			DEBUGGUI("GUI: stopping GTK3 iterator" NL);
		}
		return n;
	} else
		return 0;
}


static void xemugtkgui_shutdown ( void );

static int xemugtkgui_init ( void )
{
	is_xemugui_ok = 0;
	_gtkgui_popup_is_open = 0;
	_gtkgui_active = 0;
	if (!gtk_init_check(NULL, NULL)) {
		DEBUGGUI("GUI: GTK3 cannot be initialized, no GUI is available ..." NL);
		ERROR_WINDOW("Cannot initialize GTK");
		return 1;
	}
	is_xemugui_ok = 1;
	xemugtkmenu.num_of_menus = 0;
	_gtkgui_active = 2;
	int n = xemugtkgui_iteration();
	DEBUGGUI("GUI: GTK3 initialized, %d iterations." NL, n);	// consume possible pending (if any?) GTK stuffs after initialization - maybe not needed at all?
	atexit(xemugtkgui_shutdown);
	return 0;
}


static void xemugtkgui_shutdown ( void )
{
	if (!is_xemugui_ok)
		return;
	xemugtkgui_iteration();
	// gtk_main_quit();
	is_xemugui_ok = 0;
	DEBUGGUI("GUI: GTK3 end" NL);
}



// Currently it's a "blocking" implemtation, unlike to pop-up menu
static int xemugtkgui_file_selector ( int dialog_mode, const char *dialog_title, char *default_dir, char *selected, int path_max_size )
{
	if (!is_xemugui_ok)
		return  1;
	_gtkgui_active = 1;
	GtkFileChooserAction action = GTK_FILE_CHOOSER_ACTION_OPEN;
	GtkWidget *dialog = gtk_file_chooser_dialog_new(dialog_title,
		NULL, // parent window! We have NO GTK parent window, and it seems GTK is lame, that dumps warning, but we can't avoid this ...
		action,
		"_Cancel",
		GTK_RESPONSE_CANCEL,
		"_Open",
		GTK_RESPONSE_ACCEPT,
		NULL
	);
	if (default_dir)
		gtk_file_chooser_set_current_folder(GTK_FILE_CHOOSER(dialog), default_dir);
	*selected = '\0';
	gint res = gtk_dialog_run(GTK_DIALOG(dialog));
	if (res == GTK_RESPONSE_ACCEPT) {
		char *filename;
		filename = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));
		if (strlen(filename) < path_max_size) {
			strcpy(selected, filename);
			store_dir_from_file_selection(default_dir, filename, dialog_mode);
		} else
			res = GTK_RESPONSE_CANCEL;
		g_free(filename);
	}
	gtk_widget_destroy(dialog);
	while (gtk_events_pending())
		gtk_main_iteration();
	xemu_drop_events();
	_gtkgui_active = 2;
	return res != GTK_RESPONSE_ACCEPT;
}


static void _gtkgui_destroy_menu ( void )
{
	// FIXME: I'm still not sure, GTK would destroy all the children menu etc, or I need to play this game here.
	// If this is not needed, in fact, the whole xemugtkmenu is unneeded, and all operations on it throughout this file!!
	while (xemugtkmenu.num_of_menus > 0) {
		gtk_widget_destroy(xemugtkmenu.menus[--xemugtkmenu.num_of_menus]);
		DEBUGGUI("GUI: destroyed menu #%d at %p" NL, xemugtkmenu.num_of_menus, xemugtkmenu.menus[xemugtkmenu.num_of_menus]);
	}
	xemugtkmenu.num_of_menus = 0;
}



static void _gtkgui_callback ( const struct menu_st *item )
{
	_gtkgui_destroy_menu();
	//gtk_widget_destroy(_gtkgui_popup);
	//_gtkgui_popup = NULL;
	DEBUGGUI("GUI: menu point \"%s\" has been activated." NL, item->name);
	((xemugui_callback_t)(item->handler))(item, NULL);
}


static GtkWidget *_gtkgui_recursive_menu_builder ( const struct menu_st desc[] )
{
	if (xemugtkmenu.num_of_menus >= XEMUGUI_MAX_SUBMENUS) {
		ERROR_WINDOW("Too much submenus");
		goto PROBLEM;
	}
	GtkWidget *menu = gtk_menu_new();
	xemugtkmenu.menus[xemugtkmenu.num_of_menus++] = menu;
	for (int a = 0; desc[a].name; a++) {
		if (!desc[a].handler || !desc[a].name) {
			DEBUGPRINT("GUI: invalid meny entry found, skipping it" NL);
			continue;
		}
		GtkWidget *item = NULL;
		int type = desc[a].type;
		switch (type & 0xFF) {
			case XEMUGUI_MENUID_SUBMENU:
				item = gtk_menu_item_new_with_label(desc[a].name);
				if (!item)
					goto PROBLEM;
				gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
				GtkWidget *submenu = _gtkgui_recursive_menu_builder(desc[a].handler);	// who does not like recursion, seriously? :-)
				if (!submenu)
					goto PROBLEM;
				gtk_menu_item_set_submenu(GTK_MENU_ITEM(item), submenu);
				break;
			case XEMUGUI_MENUID_CALLABLE:
				if ((type & XEMUGUI_MENUFLAG_QUERYBACK)) {
					DEBUGGUI("GUI: query-back for \"%s\"" NL, desc[a].name);
					((xemugui_callback_t)(desc[a].handler))(&desc[a], &type);
				}
				item = gtk_menu_item_new_with_label(desc[a].name);
				if (!item)
					goto PROBLEM;
				gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
				g_signal_connect_swapped(
					item,
					"activate",
					G_CALLBACK(_gtkgui_callback),
					(gpointer)&desc[a]
				);
				break;
		}
		if (item)
			gtk_widget_show(item);
	}
	gtk_widget_show(menu);
	return menu;
PROBLEM:
	xemugtkmenu.problem = 1;
	return NULL;
}


static GtkWidget *_gtkgui_create_menu ( const struct menu_st desc[] )
{
	_gtkgui_destroy_menu();
	xemugtkmenu.problem = 0;
	GtkWidget *menu = _gtkgui_recursive_menu_builder(desc);
	if (!menu || xemugtkmenu.problem) {
		_gtkgui_destroy_menu();
		return NULL;
	} else
		return menu;
}


static void _gtkgui_disappear ( const char *signal_name )
{
	// Basically we don't want to waste CPU time in GTK for the iterator (ie event loop) if you
	// don't need it. So when pop-up menu deactivated, this callback is called, which sets _gtkgui_active to 2.
	// this is a signal for the iterator to stop itself if there was no events processed once at its run
	DEBUGGUI("GUI: requesting iterator stop on g-signal \"%s\"" NL, signal_name);
	// Unfortunately, we can't destroy widget here, since it makes callback never executed :( Oh main, GTK is hard :-O
	_gtkgui_active = 2;
	_gtkgui_popup_is_open = 0;
}


#ifndef USE_OLD_GTK_POPUP
#include <gdk/gdkx.h>
#include <SDL_syswm.h>
static GdkWindow *super_ugly_gtk_hack ( void )
{
	SDL_SysWMinfo info;
	SDL_VERSION(&info.version);
	SDL_GetWindowWMInfo(sdl_win, &info);
	if (info.subsystem != SDL_SYSWM_X11)
		ERROR_WINDOW("Sorry, it won't work, GTK GUI is supported only on top of X11, because of GTK3 makes it no possible to get uniform window-ID from non-GTK window.");
	//return gdk_x11_window_lookup_for_display(info.info.x11.display, info.info.x11.window);
	//GdkWindow *gwin = gdk_x11_window_lookup_for_display(gdk_display_get_default(), info.info.x11.window);
	GdkDisplay *gdisp = gdk_x11_lookup_xdisplay(info.info.x11.display);
	if (!gdisp) {
		DEBUGGUI("GUI: gdk_x11_lookup_xdisplay() failed :( reverting to gdk_display_get_default() ..." NL);
		gdisp = gdk_display_get_default();
	}
	GdkWindow *gwin = gdk_x11_window_foreign_new_for_display(
		//gdk_display_get_default(),
		//gdk_x11_lookup_xdisplay(info.info.x11.display),
		gdisp,
		info.info.x11.window
	);
	DEBUGGUI("GUI: gwin = %p" NL, gwin);
	return gwin;
}
#endif


static int xemugtkgui_popup ( const struct menu_st desc[] )
{
	static const char disappear_signal[] = "deactivate";
	if (_gtkgui_popup_is_open) {
		DEBUGGUI("GUI: trying to enter popup mode, while we're already there" NL);
		return 0;
	}
	if (!is_xemugui_ok /*|| gtk_menu_problem*/) {
		DEBUGGUI("GUI: MENU: GUI was not successfully initialized yet, or GTK menu creation problem occured back to the first attempt" NL);
		return 1;
	}
	_gtkgui_active = 1;
	GtkWidget *menu = _gtkgui_create_menu(desc);
	if (!menu) {
		_gtkgui_active = 0;
		ERROR_WINDOW("Could not build GTK pop-up menu :(");
		return 1;
	}
	// this signal will be fired, to request iterator there, since the menu should be run "in the background" unlike the file selector window ...
	g_signal_connect_swapped(menu, disappear_signal, G_CALLBACK(_gtkgui_disappear), (gpointer)disappear_signal);
	// FIXME: yes, I should use gtk_menu_popup_at_pointer() as this function is deprecated already!
	// however that function does not work since the event parameter being NULL causes not to display anything
	// I guess it's because there is no "parent" for the pop-up menu, as this is not a GTK app, just using the pop-up ...
#ifdef USE_OLD_GTK_POPUP
	gtk_menu_popup(
		GTK_MENU(menu),
		NULL, NULL, NULL,
		NULL, 0,    0
	);
#else
	GdkEventButton fake_event = {
		.type = GDK_BUTTON_PRESS,
		//.window = gdk_x11_window_foreign_new_for_display(
		//.window = gdk_x11_window_lookup_for_display(
		//	gdk_display_get_default(),
		//	NULL
		//)
		.window = super_ugly_gtk_hack()
	};
	gtk_menu_popup_at_pointer(GTK_MENU(menu), (GdkEvent*)&fake_event);
#endif
	_gtkgui_popup_is_open = 1;
	xemugtkgui_iteration();
	return 0;
}



static const struct xemugui_descriptor_st xemugtkgui_descriptor = {
	"gtk",						// name
	"GTK3 based Xemu UI implementation",		// desc
	xemugtkgui_init,
	xemugtkgui_shutdown,
	xemugtkgui_iteration,	
	xemugtkgui_file_selector,	
	xemugtkgui_popup
};
