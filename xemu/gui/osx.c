/* Part of the Xemu project, please visit: https://github.com/lgblgblgb/xemu
   Copyright (C)2020 Hernán Di Pietro <hernan.di.pietro@gmail.com>

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

#include <objc/objc-runtime.h>
#include <CoreGraphics/CGBase.h>
#include <CoreGraphics/CGGeometry.h>

typedef CGPoint NSPoint;

static id auto_release_pool;
static id application;

static const unsigned long NSFileHandlingPanelOKButton = 1;
static const unsigned long NSFileHandlingPanelCancelButton = 0;

// (!)
// New Apple SDKs objc_msgSend prototype changed to *force* callers
// to cast to proper types!. So this is ugly and verbose, but works.

static void _xemumacgui_menu_action_handler(id self, SEL selector, id sender)
{
	id menu_obj =  ((id (*) (id, SEL)) objc_msgSend)(sender, sel_registerName("representedObject"));
	const struct menu_st* menu_item = (const struct menu_st*) ((id (*) (id, SEL)) objc_msgSend)(menu_obj, sel_registerName("pointerValue"));
	if (menu_item && menu_item->type == XEMUGUI_MENUID_CALLABLE) {
		DEBUGPRINT("GUI: menu point \"%s\" has been activated." NL,  menu_item->name);
		((xemugui_callback_t)(menu_item->handler))(menu_item, NULL);
	}
}

static id _xemumacgui_r_menu_builder(const struct menu_st desc[])
{
	id ui_menu = ((id (*) (Class, SEL)) objc_msgSend)(objc_getClass("NSMenu"), sel_registerName("new"));
	((void (*) (id, SEL)) objc_msgSend)(ui_menu, sel_registerName("autorelease"));
	for (int i = 0; desc[i].name; i++) {
		if (!desc[i].handler || !desc[i].name) {
			DEBUGPRINT("GUI: invalid meny entry found, skipping it" NL);
			continue;
		}
		id menu_item = ((id (*) (Class, SEL)) objc_msgSend)(objc_getClass("NSMenuItem"), sel_registerName("alloc"));
		((void (*) (id, SEL)) objc_msgSend)(menu_item, sel_registerName("autorelease"));
		id str_name = ((id (*) (Class, SEL, const char*)) objc_msgSend)(objc_getClass("NSString"),
			sel_registerName("stringWithUTF8String:"), desc[i].name);
		id str_key =  ((id (*) (Class, SEL, const char*)) objc_msgSend)(objc_getClass("NSString"),
			sel_registerName("stringWithUTF8String:"), "");
		((void (*) (id, SEL, id, SEL, id))objc_msgSend)(menu_item, sel_registerName("initWithTitle:action:keyEquivalent:"),
			str_name, sel_registerName("menuActionHandler"), str_key);
		((void (*) (id, SEL, BOOL)) objc_msgSend)(menu_item, sel_registerName("setEnabled:"), YES);
		id menu_object = ((id (*) (Class, SEL, id)) objc_msgSend) (objc_getClass("NSValue"), sel_registerName("valueWithPointer:"),(id) &desc[i]);
		((void (*) (id, SEL, id))objc_msgSend)(menu_item, sel_registerName("setRepresentedObject:"), menu_object);
		((void (*) (id, SEL, id))objc_msgSend)(ui_menu, sel_registerName("addItem:"), menu_item);
		if (desc[i].type == XEMUGUI_MENUID_SUBMENU) {
			id sub_menu = _xemumacgui_r_menu_builder(desc[i].handler);
			((void (*) (id, SEL, id, id))objc_msgSend)(ui_menu, sel_registerName("setSubmenu:forItem:"), sub_menu, menu_item);
		}
	}
	return ui_menu;
}

static int xemuosxgui_init(void)
{
	DEBUGPRINT("GUI: macOS Cocoa initialization" NL);
	auto_release_pool = ((id (*) (Class, SEL)) objc_msgSend)(objc_getClass("NSAutoreleasePool"), sel_registerName("new"));
	application = ((id (*) (Class, SEL)) objc_msgSend)(objc_getClass("NSApplication"), sel_registerName("sharedApplication"));
	id app_delegate = ((id (*) (id, SEL)) objc_msgSend)(application, sel_registerName("delegate"));
	Class xemu_ui_delegate_class = ((Class (*) (id, SEL)) objc_msgSend)(app_delegate, sel_registerName("class"));
	class_addMethod(xemu_ui_delegate_class, sel_registerName("menuActionHandler"), (IMP)_xemumacgui_menu_action_handler, "v@:@");
	return 0;
}

static int xemuosxgui_popup(const struct menu_st desc[])
{
	// If the SDL window is not active, make this right-click to do application activation.
	if ( ! (((id(*)(id,SEL))objc_msgSend) (application, sel_registerName("mainWindow")))) {
		((void(*)(id,SEL,BOOL))objc_msgSend) (application, sel_registerName("activateIgnoringOtherApps:"), YES);
		return 0;
	}
	id ui_menu = _xemumacgui_r_menu_builder(desc);
	if (!ui_menu) {
		DEBUGPRINT("GUI: Error building menu");
		return 1;
	}
	NSPoint mouse_location = ((NSPoint (*) (Class, SEL)) objc_msgSend)
		(objc_getClass("NSEvent"), sel_registerName("mouseLocation"));
	((BOOL (*) (id, SEL, id, NSPoint, id)) objc_msgSend)
		(ui_menu, sel_registerName("popUpMenuPositioningItem:atLocation:inView:"), nil, mouse_location, nil);
	return 0;
}

static int xemuosxgui_file_selector(int dialog_mode, const char *dialog_title, char *default_dir, char *selected, int path_max_size )
{
	*selected = '\0';
	id open_panel = ((id (*) (Class, SEL)) objc_msgSend)(objc_getClass("NSOpenPanel"), sel_registerName("openPanel"));
	((void (*) (id, SEL)) objc_msgSend)(open_panel, sel_registerName("autorelease"));
	id main_window = ((id (*) (id, SEL)) objc_msgSend)(application, sel_registerName("mainWindow"));
	((void (*) (id, SEL, BOOL)) objc_msgSend)(open_panel, sel_registerName("setCanChooseDirectories:"), NO);
	((void (*) (id, SEL, BOOL)) objc_msgSend)(open_panel, sel_registerName("setAllowsMultipleSelection:"), NO);
	id dialog_title_str = ((id (*) (Class, SEL, const char*)) objc_msgSend)
		(objc_getClass("NSString"), sel_registerName("stringWithUTF8String:"), dialog_title);
	((void (*) (id, SEL, id)) objc_msgSend)(open_panel, sel_registerName("setMessage:"), dialog_title_str);
	if (default_dir) {
		id default_dir_str = ((id (*) (Class, SEL, const char*)) objc_msgSend)
			(objc_getClass("NSString"), sel_registerName("stringWithUTF8String:"), default_dir);
		id dir_url = ((id (*) (Class, SEL, id)) objc_msgSend)(objc_getClass("NSURL"), sel_registerName("fileURLWithPath:"), default_dir_str);
		((void (*) (id, SEL, id)) objc_msgSend) (open_panel, sel_registerName("directoryURL"), dir_url);
	}
	id panel_result = ((id (*) (id, SEL)) objc_msgSend)(open_panel, sel_registerName("runModal"));
	((void (*) (id, SEL)) objc_msgSend)(main_window, sel_registerName("makeKeyWindow")); // Ensure focus returns to Xemu window.
	if ((unsigned long)panel_result == NSFileHandlingPanelOKButton) {
		DEBUGPRINT("GUI: macOS panel OK button pressed" NL );
		id url_array = ((id (*) (id, SEL)) objc_msgSend)(open_panel, sel_registerName("URLs"));
		id filename_url = ((id (*) (id, SEL, int)) objc_msgSend)(url_array, sel_registerName("objectAtIndex:"), 0);
		const char* filename = (const char*)((id (*) (id, SEL)) objc_msgSend)(filename_url, sel_registerName("fileSystemRepresentation"));
		strcpy(selected, filename);
		store_dir_from_file_selection(default_dir, filename, dialog_mode);
		return 0;
	}
	return 1;
}

static const struct xemugui_descriptor_st xemuosxgui_descriptor = {
	"macos",					// name
	"macOS native API Xemu UI implementation",	// desc
	xemuosxgui_init,
	NULL,
	NULL,
	xemuosxgui_file_selector,
	xemuosxgui_popup
};
