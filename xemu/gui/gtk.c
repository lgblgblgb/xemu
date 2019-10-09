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


//#define RECREATE_POPUP

static int gtk_active = 0;
static GtkWidget *gtk_popup = NULL;
static int gtk_popup_is_open = 0;


static int xemugtkgui_iteration ( void )
{
	if (XEMU_UNLIKELY(gtk_active && is_xemugui_ok)) {
		int n = 0;
		while (gtk_events_pending()) {
			gtk_main_iteration();
			n++;
		}
		if (n > 0)
			DEBUGGUI("GUI: GTK used %d iterations." NL, n);
		if (n == 0 && gtk_active == 2) {
			gtk_active = 0;
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
	gtk_popup_is_open = 0;
	gtk_active = 0;
	if (!gtk_init_check(NULL, NULL)) {
		DEBUGGUI("GUI: GTK3 cannot be initialized, no GUI is available ..." NL);
		ERROR_WINDOW("Cannot initialize GTK");
		return 1;
	}
	is_xemugui_ok = 1;
	gtk_active = 2;
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
	gtk_active = 1;
	GtkFileChooserAction action = GTK_FILE_CHOOSER_ACTION_OPEN;
	GtkWidget *dialog = gtk_file_chooser_dialog_new(dialog_title,
		NULL, // parent window!
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
	xemu_drop_events();
	gtk_active = 2;
	return res != GTK_RESPONSE_ACCEPT;
}


typedef void (*callback_t)(const struct menu_st *);

static void callback ( const struct menu_st *item )
{
	gtk_widget_destroy(gtk_popup);
	gtk_popup = NULL;
	DEBUGGUI("GUI: menu point \"%s\" has been activated." NL, item->name);
	((callback_t)(item->handler))(item);
}



static GtkWidget *create_menu ( const struct menu_st desc[] )
{
	GtkWidget *menu = gtk_menu_new();
	int i = 0;
	while (desc[i].name) {
		GtkWidget *item = gtk_menu_item_new_with_label(desc[i].name);
		if (!item)
			return NULL;
		gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
		if (desc[i].type == SUBMENU) {
			GtkWidget *submenu = create_menu(desc[i].handler);	// who does not like recursion, seriously? :-)
			if (!submenu)
				return NULL;
			gtk_menu_item_set_submenu(GTK_MENU_ITEM(item), submenu);
		} else
			g_signal_connect_swapped(
				item,
				"activate",
				G_CALLBACK(callback),	// G_CALLBACK(desc[i].handler),
				(gpointer)&desc[i]	// (gpointer)&desc[i]
			);
		gtk_widget_show(item);
		i++;
	}
	gtk_widget_show(menu);
	return menu;
}




static void disappear ( const char *signal_name )
{
	// Basically we don't want to waste CPU time in GTK for the iterator (ie event loop) if you
	// don't need it. So when pop-up menu deactivated, this callback is called, which sets gtk_active to 2.
	// this is a signal for the iterator to stop itself if there was no events processed once at its run
	DEBUGGUI("GUI: requesting iterator stop on g-signal \"%s\"" NL, signal_name);
#ifdef RECREATE_POPUP
	gtk_widget_destroy(gtk_popup);
	gtk_popup = NULL;
	DEBUGGUI("GUI: gtk_popup has been destroyed" NL);
#else
	gtk_active = 2;
#endif
	gtk_popup_is_open = 0;
}


static int xemugtkgui_popup ( const struct menu_st desc[] )
{
	static const char disappear_signal[] = "deactivate";
	static int gtk_menu_problem = 0;
	if (gtk_popup_is_open) {
		DEBUGGUI("GUI: trying to enter popup mode, while we're already there" NL);
		return 0;
	}
	if (!is_xemugui_ok || gtk_menu_problem) {
		DEBUGGUI("GUI: MENU: GUI was not successfully initialized yet, or GTK menu creation problem occured back to the first attempt" NL);
		return 1;
	}
	gtk_active = 1;
	if (!gtk_popup) {
		gtk_popup = create_menu(desc);
		if (!gtk_popup) {
			gtk_menu_problem = 1;
			gtk_active = 0;
			ERROR_WINDOW("Could not build GTK pop-up menu :(");
			return 1;
		}
		// this signal will be fired, to request iterator there, since the menu should be run "in the background" unlike the file selector window ...
		g_signal_connect_swapped(gtk_popup, disappear_signal, G_CALLBACK(disappear), (gpointer)disappear_signal);
	} else
		DEBUGGUI("GUI: good, menu is already created :-)" NL);
	// FIXME: yes, I should use gtk_menu_popup_at_pointer() as this function is deprecated already!
	// however that function does not work since the event parameter being NULL causes not to display anything
	// I guess it's because there is no "parent" for the pop-up menu, as this is not a GTK app, just using the pop-up ...
#if 1
	gtk_menu_popup(
		GTK_MENU(gtk_popup),
		NULL, NULL, NULL,
		NULL, 0,    0
	);
#else
	gtk_menu_popup_at_pointer(GTK_MENU(gtk_popup), NULL);
#endif
	gtk_popup_is_open = 1;
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
