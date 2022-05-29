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

#define PLUGINGUI_COMPATIBILITY_LEVEL	1

#define PLUGINGUI_SO_FN			"plugingui.so"

#define PLUGINGUI_INITFLAG_X11		1
#define PLUGINGUI_INITFLAG_WAYLAND	2

static int xemuplugingui_init ( void );
static void xemuplugingui_shutdown ( void );


static struct xemugui_descriptor_st xemuplugingui_descriptor = {
	.name		= "plugin",
	.description	= "External plugin based GUI",
	.init		= xemuplugingui_init,
	.shutdown	= xemuplugingui_shutdown,
	.iteration	= NULL,
	.file_selector	= NULL,
	.popup		= NULL,
	.info		= NULL,
};

static struct {
	void  *so;
	void (*shutdown_cb)(void);
	int  (*ShowSimpleMessageBox)(Uint32 flags, const char *title, const char *message, SDL_Window *window);
	int  (*ShowMessageBox)(const SDL_MessageBoxData* messageboxdata, int *buttonid);
} plugingui = {
	.so	= NULL
};


static void *_plugingui_dlsym ( const char *name )
{
	static const char prefix[] = "XemuPluginGuiAPI_";
	char sym[strlen(prefix) + strlen(name) + 1];
	strcpy(sym, prefix);
	strcat(sym, name);
	void *result = dlsym(plugingui.so, sym);
	if (!result)
		DEBUGPRINT("GUI: PLUGIN: cannot resolve \"%s\": %s" NL, sym, dlerror());
	return result;
}


#ifndef XEMU_NO_SDL_DIALOG_OVERRIDE5
static int SDL_ShowSimpleMessageBox_xemuguiplugin ( Uint32 flags, const char *title, const char *message, SDL_Window *window )
{
	return plugingui.ShowSimpleMessageBox ?
		plugingui.ShowSimpleMessageBox(flags, title, message, window) :
		SDL_ShowSimpleMessageBox(flags, title, message, window);
}

static int SDL_ShowMessageBox_xemuguiplugin ( const SDL_MessageBoxData* messageboxdata, int *buttonid )
{
	return plugingui.ShowMessageBox ?
		plugingui.ShowMessageBox(messageboxdata, buttonid) :
		SDL_ShowMessageBox(messageboxdata, buttonid);
}
#endif


static inline void _plugingui_dlclose ( void )
{
	if (plugingui.so) {
		dlclose(plugingui.so);
		plugingui.so = NULL;
	}
}


static int xemuplugingui_init ( void )
{
	static const char plugin_fn[] = PLUGINGUI_SO_FN;
	char fn[strlen(sdl_pref_dir) + strlen(plugin_fn) + 1];
	strcpy(fn, sdl_pref_dir);
	strcat(fn, plugin_fn);
	DEBUGPRINT("GUI: PLUGIN: trying to load plugin: %s" NL, fn);
	_plugingui_dlclose();
	plugingui.so = dlopen(fn, RTLD_NOW);
	if (!plugingui.so) {
		DEBUGPRINT("GUI: PLUGIN: cannot load: %s" NL, dlerror());
		return 1;
	}
	int (*init_cb)(const int) = _plugingui_dlsym("init");
	if (!init_cb) {
		DEBUGPRINT("GUI: PLUGIN: init entry point could not be resolved :(" NL);
		_plugingui_dlclose();
		return 1;
	}
	const int init_result = init_cb(
		(sdl_on_x11	? PLUGINGUI_INITFLAG_X11     : 0) |
		(sdl_on_wayland	? PLUGINGUI_INITFLAG_WAYLAND : 0)
	);
	if (init_result != PLUGINGUI_COMPATIBILITY_LEVEL) {
		if (init_result <= 0)
			DEBUGPRINT("GUI: PLUGIN: init call failure: plugin returned with non-positive (%d) value" NL, init_result);
		else
			DEBUGPRINT("GUI: PLUGIN: init call failure: plugin compatibilty level mismatch (got: %d, needed: %d)" NL, init_result, PLUGINGUI_COMPATIBILITY_LEVEL);
		_plugingui_dlclose();
		return 1;
	}
	plugingui.shutdown_cb			= _plugingui_dlsym("shutdown");
#ifndef XEMU_NO_SDL_DIALOG_OVERRIDE5
	plugingui.ShowSimpleMessageBox		= _plugingui_dlsym("SDL_ShowSimpleMessageBox");
	SDL_ShowSimpleMessageBox_custom		= SDL_ShowSimpleMessageBox_xemuguiplugin;
	plugingui.ShowMessageBox		= _plugingui_dlsym("SDL_ShowMessageBox");
	SDL_ShowMessageBox_custom		= SDL_ShowMessageBox_xemuguiplugin;
#else
	plugingui.ShowMessageBox		= NULL;
	plugingui.ShowSimpleMessageBox		= NULL;
#endif
	xemuplugingui_descriptor.iteration	= _plugingui_dlsym("iteration");
	xemuplugingui_descriptor.file_selector	= _plugingui_dlsym("file_selector");
	xemuplugingui_descriptor.popup		= _plugingui_dlsym("popup");
	xemuplugingui_descriptor.info		= _plugingui_dlsym("info");
	return 0;
}


static void xemuplugingui_shutdown ( void )
{
	if (plugingui.shutdown_cb)
		plugingui.shutdown_cb();
	_plugingui_dlclose();
}
