/* Part of the Xemu project, please visit: https://github.com/lgblgblgb/xemu
   ~/xemu/gui/gui_gtk.c: UI implementation for GTK+3 of Xemu's UI abstraction layer
   Copyright (C)2016-2022 LGB (Gábor Lénárt) <lgblgblgb@gmail.com>

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


/* ------------------ LINUX/UNIX STUFFS based on GTK --------------- */


/* Note: this whole stuff is very confusing. This is because GTK makes it very
   hard to be used as some 'aux lib' and not the main heart of the project.
   so many dirty things must be played, and even this way you'll get stupid
   GTK messages even during run-time, like dialog boxes without parent, sure
   I have NO parent, why it is so important to tell (as parent window is SDL
   not GTK ..) */

#include <gtk/gtk.h>

#define USE_OLD_GTK_POPUP

static int _gtkgui_active = 0;
static int _gtkgui_popup_is_open = 0;

static struct {
	int num_of_menus;
	GtkWidget *menus[XEMUGUI_MAX_SUBMENUS];
	int problem;
} xemugtkmenu;


#ifndef GUI_HAS_POPUP
#define GUI_HAS_POPUP
#endif

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

#ifndef XEMU_NO_SDL_DIALOG_OVERRIDE
static int SDL_ShowSimpleMessageBox_xemuguigtk ( Uint32 flags, const char *title, const char *message, SDL_Window *window );
static int SDL_ShowMessageBox_xemuguigtk ( const SDL_MessageBoxData* messageboxdata, int *buttonid );
#endif

static int xemugtkgui_init ( void )
{
#ifdef XEMU_ARCH_UNIX
	if (sdl_on_x11) {
		// Workaround: on Wayland, it's possible that SDL uses x11, but the GUI (with GTK) would use Wayland, mixing x11 and wayland within the same app, isn't a good idea
		// thus we try to force x11 for GTK (better say GDK as its backend) via an environment variable set here, if we detect SDL uses x11
		static const char gdk_backend_var_name[]  = "GDK_BACKEND";
		static const char gdk_backend_var_value[] = "x11";
		DEBUGPRINT("GTK: setting environment variable %s=%s to avoid possible GTK backend mismatch with SDL" NL, gdk_backend_var_name, gdk_backend_var_value);
		setenv(gdk_backend_var_name, gdk_backend_var_value, 1);
	}
#endif
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
	DEBUGPRINT("GUI: GTK3 initialized, %d iterations." NL, n);	// consume possible pending (if any?) GTK stuffs after initialization - maybe not needed at all?
	atexit(xemugtkgui_shutdown);
#ifndef XEMU_NO_SDL_DIALOG_OVERRIDE
	// override callback for SDL_ShowSimpleMessageBox_custom to be implemented by GTK, from this point
	SDL_ShowSimpleMessageBox_custom = SDL_ShowSimpleMessageBox_xemuguigtk;
	SDL_ShowMessageBox_custom = SDL_ShowMessageBox_xemuguigtk;
#endif
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


static int _gtkgui_bgtask_running_want = 0;
static int _gtkgui_bgtask_running_status = 0;
static int _gtkgui_bgtask_callback ( void *unused ) {
	// This is the whole point of this g_timeout_add callback:
	// to have something receives SDL (!!) events while GTK is blocking,
	// thus like X11 Window System would not yell about our main SDL app being dead ...
	//xemu_drop_events();
	SDL_Event event;
	while (SDL_PollEvent(&event))
		;
	if (_gtkgui_bgtask_running_want) {
		_gtkgui_bgtask_running_status = 1;
		DEBUGGUI("GUI: bgtask is active" NL);
		return TRUE;
	} else {
		_gtkgui_bgtask_running_status = 0;
		DEBUGGUI("GUI: bgtask is inactive" NL);
		return FALSE;	// returning FALSE will cause GTK/GLIB to remove the callback of g_timeout_add()
	}
}
static void _gtkgui_bgtask_set ( void ) {
	if (_gtkgui_bgtask_running_want)
		return;
	_gtkgui_bgtask_running_want = 1;
	_gtkgui_bgtask_running_status = 1;
	g_timeout_add(100, _gtkgui_bgtask_callback, NULL);	// set callback, first arg: 1/1000th of seconds
}
static void _gtkgui_bgtask_clear ( void ) {
	if (!_gtkgui_bgtask_running_want)
		return;
	_gtkgui_bgtask_running_want = 0;
	while (_gtkgui_bgtask_running_status || gtk_events_pending()) {
		gtk_main_iteration();
		usleep(10000);
	}
	//xemu_drop_events();
}



static GtkFileChooserConfirmation xemugtkgui_confirm_overwrite ( GtkFileChooser *chooser, gpointer data ) {
	return GTK_FILE_CHOOSER_CONFIRMATION_CONFIRM;   // use the default dialog
}

// Currently it's a "blocking" implemtation, unlike to pop-up menu
static int xemugtkgui_file_selector ( int dialog_mode, const char *dialog_title, char *default_dir, char *selected, int path_max_size )
{
	if (!is_xemugui_ok)
		return  1;
	_gtkgui_active = 1;
	const char *button_text;
	GtkFileChooserAction action;
	switch (dialog_mode & 3) {
		case XEMUGUI_FSEL_OPEN:
			action = GTK_FILE_CHOOSER_ACTION_OPEN;
			button_text = "_Open";
			break;
		case XEMUGUI_FSEL_SAVE:
			action = GTK_FILE_CHOOSER_ACTION_SAVE;
			button_text = "_Save";
			break;
		default:
			FATAL("Invalid mode for UI selector: %d", dialog_mode & 3);
	}
	GtkWidget *dialog = gtk_file_chooser_dialog_new(
		dialog_title,
		NULL, // parent window! We have NO GTK parent window, and it seems GTK is lame, that dumps warning, but we can't avoid this ...
		action,
		"_Cancel",
		GTK_RESPONSE_CANCEL,
		button_text,
		GTK_RESPONSE_ACCEPT,
		NULL
	);
	if (default_dir)
		gtk_file_chooser_set_current_folder(GTK_FILE_CHOOSER(dialog), default_dir);
	*selected = '\0';
	if ((dialog_mode & 3) == XEMUGUI_FSEL_SAVE) {
		gtk_file_chooser_set_do_overwrite_confirmation(GTK_FILE_CHOOSER(dialog), TRUE);
		g_signal_connect(dialog, "confirm-overwrite", G_CALLBACK(xemugtkgui_confirm_overwrite), NULL);
	}
	_gtkgui_bgtask_set();
	gint res = gtk_dialog_run(GTK_DIALOG(dialog));
	_gtkgui_bgtask_clear();
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


static GtkWidget *_gtkgui_recursive_menu_builder ( const struct menu_st desc[], const char *parent_name)
{
	if (xemugtkmenu.num_of_menus >= XEMUGUI_MAX_SUBMENUS) {
		DEBUGPRINT("GUI: Too many submenus (max=%d)" NL, XEMUGUI_MAX_SUBMENUS);
		goto PROBLEM;
	}
	GtkWidget *menu = gtk_menu_new();
	xemugtkmenu.menus[xemugtkmenu.num_of_menus++] = menu;
	for (int a = 0; desc[a].name; a++) {
		int type = desc[a].type;
		// Some sanity checks:
		if (
			((type & 0xFF) != XEMUGUI_MENUID_SUBMENU && !desc[a].handler) ||
			((type & 0xFF) == XEMUGUI_MENUID_SUBMENU && (desc[a].handler  || !desc[a].user_data)) ||
			!desc[a].name
		) {
			DEBUGPRINT("GUI: invalid menu entry found, skipping it (item #%d of menu \"%s\")" NL, a, parent_name);
			continue;
		}
		GtkWidget *item = NULL;
		switch (type & 0xFF) {
			case XEMUGUI_MENUID_SUBMENU:
				item = gtk_menu_item_new_with_label(desc[a].name);
				if (!item)
					goto PROBLEM;
				gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
				// submenus use the user_data as the submenu menu_st struct pointer!
				GtkWidget *submenu = _gtkgui_recursive_menu_builder(desc[a].user_data, desc[a].name);	// who does not like recursion, seriously? :-)
				if (!submenu)
					goto PROBLEM;
				gtk_menu_item_set_submenu(GTK_MENU_ITEM(item), submenu);
				break;
			case XEMUGUI_MENUID_CALLABLE:
				if ((type & XEMUGUI_MENUFLAG_QUERYBACK)) {
					DEBUGGUI("GUI: query-back for \"%s\"" NL, desc[a].name);
					((xemugui_callback_t)(desc[a].handler))(&desc[a], &type);
				}
				if ((type & XEMUGUI_MENUFLAG_HIDDEN))
					continue;
				if ((type & (XEMUGUI_MENUFLAG_CHECKED | XEMUGUI_MENUFLAG_UNCHECKED))) {
					item = gtk_check_menu_item_new_with_label(desc[a].name);
					gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(item), (type & XEMUGUI_MENUFLAG_CHECKED));
				} else
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
			default:
				DEBUGPRINT("GUI: invalid menu item type: %d" NL, type & 0xFF);
				break;
		}
		if (item) {
			gtk_widget_show(item);
			if ((type & XEMUGUI_MENUFLAG_SEPARATOR)) {
				// if a menu item is flagged with separator, then a separator must be added to, after the given item
				item = gtk_separator_menu_item_new();
				if (!item)
					goto PROBLEM;
				gtk_widget_show(item);
				gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
			}
		}
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
	GtkWidget *menu = _gtkgui_recursive_menu_builder(desc, XEMUGUI_MAINMENU_NAME);
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
	if (!sdl_win)
		return NULL;
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
	DEBUGPRINT("GUI: gwin = %p" NL, gwin);
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


#if 0
static int  _xemugtkgui_info_window_response;
static void _xemugtkgui_info_window_response_cb (GtkDialog *dialog, gint response, gpointer user_data) {
	_xemugtkgui_info_window_response = response;
}
#endif
static int xemugtkgui_info_window ( GtkMessageType msg_class, const char *msg, const char *msg2 )
{
	GtkWidget* dialog = gtk_message_dialog_new(NULL, GTK_DIALOG_MODAL, msg_class, GTK_BUTTONS_OK, "%s", msg);
	if (!dialog)
		return 1;
	if (msg2 && *msg2)
		gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(dialog), "%s", msg2);
	_gtkgui_bgtask_set();
	gtk_dialog_run(GTK_DIALOG(dialog));
	_gtkgui_bgtask_clear();
#if 0
	g_signal_connect_swapped(dialog, "response", G_CALLBACK(_xemugtkgui_info_window_response_cb), NULL);
	_xemugtkgui_info_window_response = 0;
	gtk_widget_show(dialog);
	while (!_xemugtkgui_info_window_response) {
		while (gtk_events_pending())
			gtk_main_iteration();
		usleep(10000);
		xemu_drop_events();
	}
#endif
	gtk_widget_destroy(dialog);
	while (gtk_events_pending())
		gtk_main_iteration();
	return 0;
}
static int xemugtkgui_info ( int sdl_class, const char *msg )
{
	GtkMessageType msg_class;
	const char *title;
	switch (sdl_class) {
		case SDL_MESSAGEBOX_INFORMATION:
			title = "Xemu";
			msg_class = GTK_MESSAGE_INFO;
			break;
		case SDL_MESSAGEBOX_WARNING:
			title = "Xemu warning";
			msg_class = GTK_MESSAGE_WARNING;
			break;
		case SDL_MESSAGEBOX_ERROR:
			title = "Xemu error";
			msg_class = GTK_MESSAGE_ERROR;
			break;
		default:
			title = "Xemu ???";
			msg_class = GTK_MESSAGE_OTHER;
			break;
	}
	return xemugtkgui_info_window(msg_class, title, msg);
}

#ifndef XEMU_NO_SDL_DIALOG_OVERRIDE
static int SDL_ShowSimpleMessageBox_xemuguigtk ( Uint32 flags, const char *title, const char *message, SDL_Window *window )
{
	if (is_xemugui_ok) {
		if (!xemugtkgui_info(flags, message))
			return 0;
		else
			DEBUGPRINT("GUI: SDL_ShowSimpleMessageBox_xemuguigtk() has problems, reverting to SDL_ShowSimpleMessageBox()" NL);
	} else
		DEBUGPRINT("GUI: not initialized yet, SDL_ShowSimpleMessageBox_xemuguigtk() reverts to SDL_ShowSimpleMessageBox()" NL);
	if (!SDL_ShowSimpleMessageBox(flags, title, message, window))
		return 0;
	DEBUGPRINT("GUI: SDL_ShowSimpleMessageBox() error: %s" NL, SDL_GetError());
	return -1;
}
static int SDL_ShowMessageBox_xemuguigtk ( const SDL_MessageBoxData *box, int *buttonid )
{
	if (!is_xemugui_ok) {
		DEBUGPRINT("GUI: not initialized yet, SDL_ShowMessageBox_xemuguigtk() reverts to SDL_ShowMessageBox()" NL);
		goto sdl;
	}
	if (XEMU_UNLIKELY(box->numbuttons < 1))
		FATAL("Less than one button for %s?!", __func__);
	GtkWidget *dialog = gtk_message_dialog_new(NULL, GTK_DIALOG_MODAL, GTK_MESSAGE_QUESTION, GTK_BUTTONS_NONE, "%s", "Xemu question");
	gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(dialog), "%s", box->message);
	gtk_window_set_focus(GTK_WINDOW(dialog), NULL);
	int escape_default_value = -1;
	GtkWidget *return_default_button = NULL;
	for (int a = 0; a < box->numbuttons; a++) {
		GtkWidget *button = gtk_dialog_add_button(GTK_DIALOG(dialog), box->buttons[a].text, a);
		if ((box->buttons[a].flags & SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT) || escape_default_value < 0)
			escape_default_value = a;
		if ((box->buttons[a].flags & SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT) || !return_default_button)
			return_default_button = button;
	}
#if 0
	for (int a = 0; a < 10; a++) {
		char name[10];
		sprintf(name, "Element-%d", a);
		gtk_dialog_add_button(GTK_DIALOG(dialog), name, box->numbuttons + a);
	}
#endif
	gtk_widget_set_can_default(return_default_button, TRUE);
	gtk_window_set_default(GTK_WINDOW(dialog), return_default_button);
	_gtkgui_bgtask_set();
	gint res = gtk_dialog_run(GTK_DIALOG(dialog));
	_gtkgui_bgtask_clear();
	gtk_widget_destroy(dialog);
	//DEBUGPRINT("GUI: GTK RES = %d" NL, res);
	if (res < 0) {
		// FIXME: not all negative error codes mean escape the dialog box, maybe only -4? what about the others??
		res = escape_default_value;
	}
	while (gtk_events_pending())
		gtk_main_iteration();
	//xemu_drop_events();
	*buttonid = res;
	return 0;
sdl:
	return SDL_ShowMessageBox(box, buttonid);
}
#endif

static const struct xemugui_descriptor_st xemugtkgui_descriptor = {
	.name		= "gtk",
	.description	= "GTK3 based Xemu UI implementation",
	.init		= xemugtkgui_init,
	.shutdown	= xemugtkgui_shutdown,
	.iteration	= xemugtkgui_iteration,
	.file_selector	= xemugtkgui_file_selector,
	.popup		= xemugtkgui_popup,
	.info		= xemugtkgui_info
};
