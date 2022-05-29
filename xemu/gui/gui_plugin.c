/* Part of the Xemu project, please visit: https://github.com/lgblgblgb/xemu
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


/* ------------------------- PLUGIN GUI ---------------------- */

#include <dlfcn.h>


#define PLUGINGUI_SO_FN			"plugingui.so"
#define PLUGINGUI_COMPATIBILITY_LEVEL	1
#define PLUGINGUI_SYM_PREFIX		"XemuPluginGuiAPI_"

#define PLUGINGUI_INITFLAG_X11		1
#define PLUGINGUI_INITFLAG_WAYLAND	2


static int  xemuplugingui_init     ( void );
static void xemuplugingui_shutdown ( void );
static int  xemuplugingui_info     ( int sdl_class, const char *msg );


static struct xemugui_descriptor_st xemuplugingui_descriptor = {
	.name		= "plugin",
	.description	= "External plugin based GUI",
	.init		= xemuplugingui_init,
	.shutdown	= xemuplugingui_shutdown,
	.iteration	= NULL,
	.file_selector	= NULL,
	.popup		= NULL,
	.info		= xemuplugingui_info
};

static struct {
	void  *so;
	void (*shutdown_cb)(void);
	int  (*ShowSimpleMessageBox)(Uint32 flags, const char *title, const char *message, SDL_Window *window);
	int  (*ShowMessageBox)(const SDL_MessageBoxData *messageboxdata, int *buttonid);
} plugingui = {
	.so	= NULL
};


static int SDL_ShowSimpleMessageBox_xemuguiplugin ( Uint32 flags, const char *title, const char *message, SDL_Window *window )
{
	return (plugingui.so && plugingui.ShowSimpleMessageBox) ?
		plugingui.ShowSimpleMessageBox(flags, title, message, window) :
		SDL_ShowSimpleMessageBox(flags, title, message, window);
}


static int SDL_ShowMessageBox_xemuguiplugin ( const SDL_MessageBoxData *messageboxdata, int *buttonid )
{
	return (plugingui.so && plugingui.ShowMessageBox) ?
		plugingui.ShowMessageBox(messageboxdata, buttonid) :
		SDL_ShowMessageBox(messageboxdata, buttonid);
}


static int xemuplugingui_info ( int sdl_class, const char *msg )
{
	const char *title;
	switch (sdl_class) {
		case SDL_MESSAGEBOX_INFORMATION:
			title = "Xemu";
			break;
		case SDL_MESSAGEBOX_WARNING:
			title = "Xemu warning";
			break;
		case SDL_MESSAGEBOX_ERROR:
			title = "Xemu error";
			break;
		default:
			title = "Xemu ???";
			break;
	}
	return SDL_ShowSimpleMessageBox_xemuguiplugin(sdl_class, title, msg, sdl_win);
}


static inline void _plugingui_dlclose ( void )
{
	if (plugingui.so) {
		dlclose(plugingui.so);
		plugingui.so = NULL;
		DEBUGPRINT("GUI: PLUGIN: plugin is unbound now" NL);
	}
}


static void *_plugingui_dlsym ( const char *name, const char *fatal_errstr )
{
	static const char prefix[] = PLUGINGUI_SYM_PREFIX;
	char sym[strlen(prefix) + strlen(name) + 1];
	strcpy(sym, prefix);
	strcat(sym, name);
	void *result = dlsym(plugingui.so, sym);
	if (!result) {
		DEBUGPRINT("GUI: PLUGIN: cannot resolve \"%s\" [%s]: %s" NL, sym, fatal_errstr ? "FATAL-ERROR" : "skipping-symbol", dlerror());
		if (fatal_errstr) {
			DEBUGPRINT("GUI: PLUGIN: fatal error, aborting plugin: %s" NL, fatal_errstr);
			_plugingui_dlclose();
		}
	}
	return result;
}


static int xemuplugingui_init ( void )
{
	static const char plugin_fn[] = PLUGINGUI_SO_FN;
	static const char missing_compulsory_export_errstr[] = "missing compulsory symbol in plugin";
	char fn[strlen(sdl_pref_dir) + strlen(plugin_fn) + 1];
	strcpy(fn, sdl_pref_dir);
	strcat(fn, plugin_fn);
	plugingui.ShowMessageBox	= NULL;
	plugingui.ShowSimpleMessageBox	= NULL;
	SDL_ShowSimpleMessageBox_custom = NULL;
	SDL_ShowMessageBox_custom	= NULL;
	_plugingui_dlclose();
	if (!xemu_os_file_exists(fn)) {
		DEBUGPRINT("GUI: PLUGIN: plugin does not exist, skipping it: %s" NL, fn);
		return 1;
	}
	DEBUGPRINT("GUI: PLUGIN: trying to load plugin: %s" NL, fn);
	plugingui.so = dlopen(fn, RTLD_NOW);
	if (!plugingui.so) {
		DEBUGPRINT("GUI: PLUGIN: cannot load: %s" NL, dlerror());
		return 1;
	}
	// Check compatibility level
	const int *compatibility_level_ptr = (int*)_plugingui_dlsym("compatibility_const", missing_compulsory_export_errstr);
	if (!compatibility_level_ptr)
		return 1;
	if (*compatibility_level_ptr != PLUGINGUI_COMPATIBILITY_LEVEL) {
		DEBUGPRINT("GUI: PLUGIN: compatibility check failure: plugin compatibilty level mismatch (got: %d, needed: %d)" NL, *compatibility_level_ptr, PLUGINGUI_COMPATIBILITY_LEVEL);
		_plugingui_dlclose();
		return 1;
	}
	// Initialize plugin
	int (*init_cb)(const int) = _plugingui_dlsym("init", missing_compulsory_export_errstr);
	if (!init_cb) {
		return 1;
	}
	const int init_result = init_cb(
		(sdl_on_x11	? PLUGINGUI_INITFLAG_X11     : 0) |
		(sdl_on_wayland	? PLUGINGUI_INITFLAG_WAYLAND : 0)
	);
	if (init_result) {
		DEBUGPRINT("GUI: PLUGIN: init call failure: plugin returned with non-zero (%d) value" NL, init_result);
		_plugingui_dlclose();
		return 1;
	}
	// Import API functions. NOTE: all of them can be non-exported, thus zero (dlsym will fail ...), this is by design,
	// it means, that the plugin does not provide that kind of capability.
	plugingui.shutdown_cb			= _plugingui_dlsym("shutdown", NULL);
#ifndef XEMU_NO_SDL_DIALOG_OVERRIDE5
	plugingui.ShowSimpleMessageBox		= _plugingui_dlsym("SDL_ShowSimpleMessageBox", NULL);
	SDL_ShowSimpleMessageBox_custom		= SDL_ShowSimpleMessageBox_xemuguiplugin;
	plugingui.ShowMessageBox		= _plugingui_dlsym("SDL_ShowMessageBox", NULL);
	SDL_ShowMessageBox_custom		= SDL_ShowMessageBox_xemuguiplugin;
#endif
	xemuplugingui_descriptor.iteration	= _plugingui_dlsym("iteration", NULL);
	xemuplugingui_descriptor.file_selector	= _plugingui_dlsym("file_selector", NULL);
	xemuplugingui_descriptor.popup		= _plugingui_dlsym("popup", NULL);
	DEBUGPRINT("GUI: PLUGIN: initialization is done." NL);
	return 0;
}


static void xemuplugingui_shutdown ( void )
{
	if (plugingui.so && plugingui.shutdown_cb)
		plugingui.shutdown_cb();
	_plugingui_dlclose();
}
