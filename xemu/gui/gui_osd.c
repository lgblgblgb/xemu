/* Part of the Xemu project, please visit: https://github.com/lgblgblgb/xemu
   Copyright (C)2016-2022,2025 LGB (Gábor Lénárt) <lgblgblgb@gmail.com>

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


/* ------------------------- OSD GUI ---------------------- */


// FIXME TODO: Currently, OSD GUI is *extremely* primitive and hacky, using own SDL mechanisms to "steal"
// control from the normal ones while any wait and/or interaction is needed!
// Also we can't use the emulated machine's texture, as that texture may be locked or not, not safe to
// be used. Thus we do all the SDL level rendering here and using only the OSD texture, and we also
// need to handle all SDL events here during "UI-time" as well.

#include <dirent.h>
#include <errno.h>
#include <unistd.h>

#define OSDGUI_MENU_MAXDEPTH	16
#define OSDGUI_MENU_MAIN_NAME	"*** MAIN ***"

// Needed by emcc?
#include <sys/types.h>
#include <sys/stat.h>


// Filled by xemuosdgui_init(), because the GUI subsystem has no direct access for OSD layer internals
static struct {
	//SDL_Texture	*tex;
	Uint32	*pixels;
	int	xsize, ysize;
	int	fontwidth, fontheight;
	int	xtextres, ytextres;
	int	xofs, yofs;
	const struct menu_st	*menupath[OSDGUI_MENU_MAXDEPTH];
	const char 		*menunames[OSDGUI_MENU_MAXDEPTH];
	int			menuselected[OSDGUI_MENU_MAXDEPTH];
	int 			menudepth;
} osdgui;

#define OSDGUIKEY_QUIT		1
#define OSDGUIKEY_UP		2
#define OSDGUIKEY_DOWN		3
#define OSDGUIKEY_LEFT		4
#define OSDGUIKEY_RIGHT		5
#define OSDGUIKEY_PAGEUP	6
#define OSDGUIKEY_PAGEDOWN	7
#define OSDGUIKEY_F1		8
#define OSDGUIKEY_MOUSEMOTION	9
#define OSDGUIKEY_MOUSEBUTTON	10
#define OSDGUIKEY_HOME		11
#define OSDGUIKEY_END		12
#define OSDGUIKEY_ENTER		13
#define OSDGUIKEY_ESC		14

#define OSDGUI_ITEMATTRIB_SEPARATOR	1
#define OSDGUI_ITEMATTRIB_SUBMENU	2
#define OSDGUI_ITEMATTRIB_CHECKED	4
#define OSDGUI_ITEMATTRIB_UNCHECKED	8

#define OSDGUI_ITEMBROWSER_ARROW_CHR		16
#define OSDGUI_ITEMBROWSER_ARROW_CHR2		17
#define OSDGUI_ITEMBROWSER_UNCHECKED_CHR	250
#define OSDGUI_ITEMBROWSER_CHECKED_CHR		251
#define OSDGUI_ITEMBROWSER_SUBMENU_CHR		31

#define OSDGUI_ITEMBROWSER_ALLOW_LEFT	1
#define	OSDGUI_ITEMBROWSER_ALLOW_RIGHT	2
#define	OSDGUI_ITEMBROWSER_ALLOW_F1	4

#define OSDGUI_QUESTIONS_Y		2
#define OSDGUI_MENU_Y			3

#define OSDGUI_SUPERTOP_COLOURPAIR	1,0
#define OSDGUI_MENUNAME_COLOURPAIR	1,5
#define OSDGUI_MSG_COLOURPAIR 		1,4
#define OSDGUI_PAGEINFO_COLOURPAIR	3,5
#define OSDGUI_CWD_COLOURPAIR		1,5
#define OSDGUI_UNSELECTED_COLOURPAIR	6,5
#define OSDGUI_SELECTED_COLOURPAIR	1,4

#define OSDGUI_SEPARATOR_COLOUR		6
#define OSDGUI_ITEMCLEAR_COLOUR		2

//#define DEBUGOSDGUI(...)	DEBUGPRINT(__VA_ARGS__)
//#define DEBUGOSDGUI(...)	DEBUG(__VA_ARGS__)
#define DEBUGOSDGUI(...)


static int _osdgui_mousepos_conversion ( SDL_Point *t, const int x_pix, const int y_pix, const int force, const char *desc )
{
	static int tx = -1, ty = -1;
	tx = (double)((double)((double)x_pix / (double)((double)sdl_default_win_x_size / osdgui.xsize) - osdgui.xofs) / osdgui.fontwidth);
	ty = (double)((double)((double)y_pix / (double)((double)sdl_default_win_y_size / osdgui.ysize) - osdgui.yofs) / osdgui.fontheight);
	if (tx > 0 && ty > 0 && tx < osdgui.xtextres && ty < osdgui.ytextres && (tx != t->x || ty != t->y || force)) {
		DEBUGOSDGUI("Mouse %s event: %d,%d -> %d,%d" NL, desc, x_pix, y_pix, tx, ty);
		t->x = tx;
		t->y = ty;
		return 0;
	}
	return 1;
}


static int _osdgui_sdl_loop ( int need_rendering, SDL_Point *mousepos )
{
	static const struct {
		const SDL_Scancode scan;
		const int func;
	} scan2osdfunc[] = {
		{ SDL_SCANCODE_RETURN,   OSDGUIKEY_ENTER  }, { SDL_SCANCODE_KP_ENTER, OSDGUIKEY_ENTER    }, { SDL_SCANCODE_RETURN2, OSDGUIKEY_ENTER },
		{ SDL_SCANCODE_ESCAPE,   OSDGUIKEY_ESC    }, { SDL_SCANCODE_UP,       OSDGUIKEY_UP       }, { SDL_SCANCODE_DOWN,    OSDGUIKEY_DOWN  },
		{ SDL_SCANCODE_PAGEUP,   OSDGUIKEY_PAGEUP }, { SDL_SCANCODE_PAGEDOWN, OSDGUIKEY_PAGEDOWN }, { SDL_SCANCODE_LEFT,    OSDGUIKEY_LEFT  },
		{ SDL_SCANCODE_RIGHT,    OSDGUIKEY_RIGHT  }, { SDL_SCANCODE_F1,       OSDGUIKEY_F1       }, { SDL_SCANCODE_HOME,    OSDGUIKEY_HOME  },
		{ SDL_SCANCODE_END,      OSDGUIKEY_END    },
		{ -1 , -1 }
	};
	SDL_PumpEvents();		// make sure we flush some events maybe pending
	SDL_FlushEvent(SDL_KEYDOWN);
	SDL_FlushEvent(SDL_WINDOWEVENT);
	for (;;) {
		if (need_rendering) {
			DEBUGOSDGUI("GUI: OSD: rendering ..." NL);
			osd_only_sdl_render_hack();	// render texture
			need_rendering = 0;
		}
		SDL_Event event;
		if (SDL_PollEvent(&event)) {
			switch (event.type) {
				case SDL_QUIT:
					SDL_PushEvent(&event);	// add event back, so main program will receive the quit request
					return OSDGUIKEY_QUIT;
				case SDL_MOUSEMOTION:
					if (mousepos && !_osdgui_mousepos_conversion(mousepos, event.motion.x, event.motion.y, 0, "motion"))
						return OSDGUIKEY_MOUSEMOTION;
					break;
				case SDL_MOUSEBUTTONDOWN:
					break;
				case SDL_MOUSEBUTTONUP:
					if (mousepos && !_osdgui_mousepos_conversion(mousepos, event.button.x, event.button.y, 1, "button"))
						return OSDGUIKEY_MOUSEBUTTON;
					break;
				case SDL_MOUSEWHEEL:
					if (mousepos) {
						const int relmot = event.wheel.y * (event.wheel.direction == SDL_MOUSEWHEEL_FLIPPED ? -1 : 1);
						if (relmot > 0)
							return OSDGUIKEY_PAGEUP;
						if (relmot < 0)
							return OSDGUIKEY_PAGEDOWN;
					}
					break;
				case SDL_KEYUP:
					break;
				case SDL_KEYDOWN:
#ifdef					XEMU_ARCH_ANDROID
					if (event.key.keysym.sym == SDLK_AC_BACK) {	// Android "BACK" button
						return OSDGUIKEY_LEFT;	// means "back" in the menu structure
					}
#endif
					for (int a = 0; scan2osdfunc[a].func >= 0; a++)
						if (scan2osdfunc[a].scan == event.key.keysym.scancode)
							return scan2osdfunc[a].func;
					DEBUGOSDGUI("GUI: OSD: unhandled key %c (%d)" NL,
						event.key.keysym.scancode >= 32 && event.key.keysym.scancode < 127 ? event.key.keysym.scancode : '?',
						event.key.keysym.scancode
					);
					break;
				case SDL_WINDOWEVENT:
					if (event.window.event == SDL_WINDOWEVENT_EXPOSED)
						need_rendering = 1;
					break;
				default:
					DEBUGOSDGUI("GUI: OSD: unhandled event type %d" NL, (int)event.type);
					break;
			}
		} else {
			SDL_Delay(10);
			SDL_PumpEvents();
		}
	}
}


static void _osdgui_list_drawing ( const char *textbuf[], const Uint32 attribs[], const int items, const int active_item, int y_pixpos )
{
	for (int i = 0; i < items; i++) {
		const char *p = textbuf[i];
		const int is_active = (i == active_item);
		if (is_active)
			osd_set_colours(OSDGUI_SELECTED_COLOURPAIR);
		else
			osd_set_colours(OSDGUI_UNSELECTED_COLOURPAIR);
		for (int x_pixpos = osdgui.xofs, x = 0; x < osdgui.xtextres; x_pixpos += osdgui.fontwidth, x++)
			if (!x)
				osd_write_char(x_pixpos, y_pixpos, is_active ? OSDGUI_ITEMBROWSER_ARROW_CHR : ' ');
			else if (x == 1 && attribs) {
				unsigned char attrib_char = ' ';
				if ((attribs[i] & OSDGUI_ITEMATTRIB_UNCHECKED))
					attrib_char = OSDGUI_ITEMBROWSER_UNCHECKED_CHR;
				if ((attribs[i] & OSDGUI_ITEMATTRIB_CHECKED))
					attrib_char = OSDGUI_ITEMBROWSER_CHECKED_CHR;
				if ((attribs[i] & OSDGUI_ITEMATTRIB_SUBMENU))
					attrib_char = OSDGUI_ITEMBROWSER_SUBMENU_CHR;
				osd_write_char(x_pixpos, y_pixpos, attrib_char);
			} else if (x == 2 && attribs)
				osd_write_char(x_pixpos, y_pixpos, ' ');
			else if (x == osdgui.xtextres - 1)
				osd_write_char(x_pixpos, y_pixpos, is_active ? OSDGUI_ITEMBROWSER_ARROW_CHR2 : ' ');
			else
				osd_write_char(x_pixpos, y_pixpos, *p ? *p++ : ' ');
		if (attribs && (attribs[i] & OSDGUI_ITEMATTRIB_SEPARATOR)) {
			const SDL_Rect rect = {
				.x = osdgui.xofs + osdgui.fontwidth, .y = y_pixpos + osdgui.fontheight - 1,
				.w = osdgui.xsize / 2, .h = 1
			};
			osd_clear_rect_with_colour(OSDGUI_SEPARATOR_COLOUR, &rect);
		}
		y_pixpos += osdgui.fontheight;
	}
}


static int _osdgui_do_browse_list_loop ( const char *textbuf[], const Uint32 attribs[], const int all_items, const int page_items, int active_item, const int y, int need_rendering, const int options, int *ev )
{
	const SDL_Rect rect = {
		.x = 0, .y = y * osdgui.fontheight + osdgui.yofs,
		.w = osdgui.xsize, .h = page_items * osdgui.fontheight
	};
	const int page_indicator_all = all_items / page_items + ((all_items % page_items) ? 1 : 0);
	int page_indicator_current = -1;
	if (active_item < 0)
		active_item = 0;
	else
		active_item %= all_items;
	for (;;) {
		if (need_rendering) {
			const int page_indicator_new = active_item / page_items + 1;
			if (page_indicator_all > 1 && page_indicator_current != page_indicator_new) {
				page_indicator_current = page_indicator_new;
				char msg[64];
				sprintf(msg, "(page %d of %d in %d items)",
					active_item / page_items + 1,
					page_indicator_all,
					all_items
				);
				osd_set_colours(OSDGUI_PAGEINFO_COLOURPAIR);
				const SDL_Rect rect2 = {
					.x = osdgui.xofs, .y = osdgui.yofs + (y - 1) * osdgui.fontheight,
					.w = osdgui.xtextres * osdgui.fontwidth, .h = osdgui.fontwidth
				};
				osd_write_string(rect2.x, rect2.y, msg);
				osd_texture_update(&rect2);

			}
			const int in_page_pos = active_item % page_items;
			const int page_top_index = active_item - in_page_pos;
			const int this_page_length = page_top_index + page_items >= all_items ? all_items - page_top_index : page_items;
			osd_clear_rect_with_colour(OSDGUI_ITEMCLEAR_COLOUR, &rect);
			_osdgui_list_drawing(textbuf + page_top_index, attribs ? attribs + page_top_index : NULL, this_page_length, in_page_pos, rect.y);
			osd_texture_update(&rect);
		}
		SDL_Point mousepos;
		*ev = _osdgui_sdl_loop(need_rendering, &mousepos);
		if (
			*ev == OSDGUIKEY_ENTER || *ev == OSDGUIKEY_ESC || *ev == OSDGUIKEY_QUIT ||
			(*ev == OSDGUIKEY_LEFT  && (options & OSDGUI_ITEMBROWSER_ALLOW_LEFT)) ||
			(*ev == OSDGUIKEY_RIGHT && (options & OSDGUI_ITEMBROWSER_ALLOW_RIGHT)) ||
			(*ev == OSDGUIKEY_F1    && (options & OSDGUI_ITEMBROWSER_ALLOW_F1))
		)
			break;
		if (*ev == OSDGUIKEY_MOUSEMOTION || *ev == OSDGUIKEY_MOUSEBUTTON) {
			if (mousepos.y >= y) {
				const int on = active_item - (active_item % page_items) + (mousepos.y - y);
				if (*ev == OSDGUIKEY_MOUSEBUTTON && on < all_items) {
					active_item = on;
					*ev = OSDGUIKEY_ENTER;
					break;
				}
				if (on < all_items && on != active_item) {
					active_item = on;
					need_rendering = 1;
					continue;
				}
			}
		}
		if (*ev == OSDGUIKEY_UP && all_items > 1) {
			active_item = active_item ? active_item - 1 : all_items - 1;
			need_rendering = 1;
			continue;
		}
		if (*ev == OSDGUIKEY_DOWN && all_items > 1) {
			active_item = active_item + 1 < all_items ? active_item + 1 : 0;
			need_rendering = 1;
			continue;
		}
		if (*ev == OSDGUIKEY_PAGEUP && all_items > page_items) {
			active_item = active_item >= page_items ? active_item - page_items : all_items - 1;
			need_rendering = 1;
			continue;

		}
		if (*ev == OSDGUIKEY_PAGEDOWN && all_items > page_items) {
			active_item = active_item + page_items < all_items ? active_item + page_items : 0;
			need_rendering = 1;
			continue;
		}
		if (*ev == OSDGUIKEY_HOME && all_items > 1) {
			active_item = 0;
			need_rendering = 1;
			continue;
		}
		if (*ev == OSDGUIKEY_END && all_items > 1) {
			active_item = all_items - 1;
			need_rendering = 1;
			continue;
		}
		need_rendering = 0;
	}
	DEBUGOSDGUI("GUI: OSD: selection %d" NL, active_item);
	return active_item;
}


static void _osdgui_format_text ( const Uint8 *msg, int x, int y, const Uint8 fg, const Uint8 bg )
{
	osd_set_colours(fg, bg);
	int force_preclear = 1;
	if (x < 0) {
		// Align center if x < 0, though only works if the text is shorter than one line
		x = (osdgui.xtextres - strlen((const char*)msg)) / 2;
		if (x < 0)
			x = 0;
	}
	SDL_Rect rect = {	// y-pos will be updated on-the-fly (pixel based, texture dependent!)
		.x = 0,
		.w = osdgui.xsize,
		.h = osdgui.fontheight
	};
	for (;;) {
		// skip whitespaces
		while (*msg <= 32 && *msg)
			msg++;
		// check end of string
		if (!*msg)
			break;
		// search the end of the word
		const Uint8 *e = msg;
		while (*e > 32)
			e++;
		const int wordlen = e - msg;
		if (wordlen > osdgui.xtextres) {
			// word wouldn't fit, even a whole line is too short for that
		} else {
			// word can fit
			if (x + wordlen > osdgui.xtextres) {
				// ... but not into THIS line! Thus, new line
				x = 0;
				y++;
			}
		}
		// time to print out the word
		while (msg != e) {
			if (y >= osdgui.ytextres) {
				DEBUGPRINT("GUI: OSD: y-overflow of text :-(" NL);
				return;
			}
			rect.y = y * osdgui.fontheight + osdgui.yofs;
			if (!x || force_preclear) {	// clear the line of the text
				osd_clear_rect_with_colour(bg, &rect);
				force_preclear = 0;	// used to be make sure we clear line even if "x" is not zero arg of this func
			}
			osd_write_char(x * osdgui.fontwidth  + osdgui.xofs, rect.y, (char)*msg++);
			x++;
			if (osdgui.xtextres == x) {
				x = 0;
				y++;
			}
		}
		// space, if not at x=0
		if (x) {
			x++;
			if (osdgui.xtextres == x) {
				x = 0;
				y++;
			}
		}
	}
}


static void _osdgui_prepare_return ( void )
{
	osd_off();
	osd_set_colours(1, 0);	// restore original OSD colours in use for fg/bg
}


static int _osdgui_msg_window ( const char *title, const char *msg, const SDL_MessageBoxButtonData *buttons, const int nbuttons )
{
	osd_clear_with_colour(5);
	_osdgui_format_text((const Uint8*)title, -1,  0, OSDGUI_SUPERTOP_COLOURPAIR);
	_osdgui_format_text((const Uint8*)msg, -1, OSDGUI_QUESTIONS_Y + nbuttons + 1, OSDGUI_MSG_COLOURPAIR);
	int current = 0;
	int esc_buttonid = buttons[0].buttonid;
	const char *chooseptrs[nbuttons];
	for (int a = 0; a < nbuttons; a++) {
		chooseptrs[a] = buttons[a].text;
		if ((buttons[a].flags & SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT))
			current = a;
		if ((buttons[a].flags & SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT))
			esc_buttonid = buttons[a].buttonid;
	}
	osd_texture_update(NULL);
	// Let's begin
	int result;
	int need_rendering = 1;
	for (;;) {
		int retev;
		current = _osdgui_do_browse_list_loop(chooseptrs, NULL, nbuttons, nbuttons, current, OSDGUI_QUESTIONS_Y, need_rendering, 0, &retev);
		if (retev == OSDGUIKEY_ENTER) {
			result = buttons[current].buttonid;
			break;
		}
		if (retev == OSDGUIKEY_QUIT || retev == OSDGUIKEY_ESC) {
			result = esc_buttonid;
			break;
		}
		need_rendering = 0;
	}
	_osdgui_prepare_return();
	return result;
}


static const SDL_MessageBoxButtonData _osdgui_dismiss_button = {
	.flags		= SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT | SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT,
	.buttonid	= 0,
	.text		= "OK/Dismiss"
};


static int xemuosdgui_info ( int sdl_class, const char *msg )
{
	const char *title;
	switch (sdl_class) {
		case SDL_MESSAGEBOX_INFORMATION:
			title = "Xemu information";
			break;
		case SDL_MESSAGEBOX_WARNING:
			title = "Xemu warning";
			break;
		case SDL_MESSAGEBOX_ERROR:
			title = "Xemu error";
			break;
		default:
			title = "Xemu (?)";
			break;
	}
	return _osdgui_msg_window(title, msg, &_osdgui_dismiss_button, 1);
}

// Android using OSD GUI can result in quite small and hard to click options for simple dialog boxes
// Porbably the SDL implemented original ones are better in this case.
#if !defined(XEMU_NO_SDL_DIALOG_OVERRIDE) && defined(XEMU_ARCH_ANDROID)
#warning "XEMU_NO_SDL_DIALOG_OVERRIDE for Android (do not override simple SDL boxes for Android in case of OSD GUI for now!)"
#define XEMU_NO_SDL_DIALOG_OVERRIDE
#endif

// FIXME: it's questionable that in case of OSD GUI, we really want to decide on macro XEMU_NO_SDL_DIALOG_OVERRIDE ...
#ifdef	XEMU_NO_SDL_DIALOG_OVERRIDE
#define SDL_ShowSimpleMessageBox_xemuguiosd	SDL_ShowSimpleMessageBox
#define SDL_ShowMessageBox_xemuguiosd		SDL_ShowMessageBox
#else
static int SDL_ShowSimpleMessageBox_xemuguiosd ( Uint32 flags, const char *title, const char *message, SDL_Window *window )
{
	xemuosdgui_info(flags, message);
	return 0;
}
static int SDL_ShowMessageBox_xemuguiosd ( const SDL_MessageBoxData* messageboxdata, int *buttonid )
{
	const SDL_MessageBoxButtonData *buttons;
	int numbuttons;
	if (!messageboxdata->buttons || messageboxdata->numbuttons < 1) {
		buttons = &_osdgui_dismiss_button,
		numbuttons = 1;
	} else {
		buttons = messageboxdata->buttons;
		numbuttons = messageboxdata->numbuttons;
	}
	const int ret = _osdgui_msg_window("Xemu question", messageboxdata->message, buttons, numbuttons);
	if (buttonid)
		*buttonid = ret;
	return 0;
}
#endif


static int xemuosdgui_init ( void )
{
	if (!is_osd_enabled()) {
		ERROR_WINDOW("OSD is not enabled to be able to use OSD-UI.");
		return 1;
	}
	osd_get_texture_info(/*&osdgui.tex*/ NULL, &osdgui.pixels, &osdgui.xsize, &osdgui.ysize, &osdgui.fontwidth, &osdgui.fontheight);
	osdgui.xtextres =  osdgui.xsize / osdgui.fontwidth ;
	osdgui.xofs     = (osdgui.xsize % osdgui.fontwidth ) / 2;
	osdgui.ytextres =  osdgui.ysize / osdgui.fontheight;
	osdgui.yofs     = (osdgui.ysize % osdgui.fontheight) / 2;
	DEBUGPRINT("GUI: OSD: initialized over %dx%d texture giving %dx%d character resolution." NL, osdgui.xsize, osdgui.ysize, osdgui.xtextres, osdgui.ytextres);
#ifndef XEMU_NO_SDL_DIALOG_OVERRIDE
	// override callback for SDL_ShowSimpleMessageBox_custom to be implemented by OSD-GUI from this point
	SDL_ShowSimpleMessageBox_custom = SDL_ShowSimpleMessageBox_xemuguiosd;
	SDL_ShowMessageBox_custom       = SDL_ShowMessageBox_xemuguiosd;
#endif
	osdgui.menudepth = 0;
	osdgui.menuselected[0] = 0;
	static const char root_menu_name[] = OSDGUI_MENU_MAIN_NAME;
	osdgui.menunames[0] = root_menu_name;
	return 0;
}


static void _osdgui_process_menu ( void )
{
restart:
	osd_clear_with_colour(5);
	_osdgui_format_text((const Uint8*)"Xemu menu", -1,  0, OSDGUI_SUPERTOP_COLOURPAIR);
	_osdgui_format_text((const Uint8*)osdgui.menunames[osdgui.menudepth], -1,  1, OSDGUI_MENUNAME_COLOURPAIR);
	osd_texture_update(NULL);
	const struct menu_st *desc = osdgui.menupath[osdgui.menudepth];
	int items_in = 0; // let's count the menu points first, as an upper limit, real menu points may be less (if there is hidden ones, etc)
	while (desc[items_in].name)
		items_in++;
	const char *chooseptrs[items_in];
	const struct menu_st *menuptrs[items_in];
	Uint32 attribs[items_in];
	int types[items_in];
	int items = 0;
	for (int i = 0; i < items_in; i++) {
		int type = desc[i].type;
		if ((type & 0xFF) == XEMUGUI_MENUID_CALLABLE && ((type & XEMUGUI_MENUFLAG_QUERYBACK)))
			((xemugui_callback_t)(desc[i].handler))(&desc[i], &type);
		if ((type & 0xFF) != XEMUGUI_MENUID_SUBMENU && (type & 0xFF) != XEMUGUI_MENUID_CALLABLE) {
			DEBUGPRINT("GUI: invalid menu item type: %d" NL, type & 0xFF);
			continue;
		}
		if ((type & XEMUGUI_MENUFLAG_HIDDEN))
			continue;
		chooseptrs[items] = desc[i].name;
		menuptrs[items] = &desc[i];
		types[items] = type;
		Uint32 attrib = 0;
		if ((type & XEMUGUI_MENUFLAG_SEPARATOR))
			attrib |= OSDGUI_ITEMATTRIB_SEPARATOR;
		if ((type & 0xFF) == XEMUGUI_MENUID_SUBMENU)
			attrib |= OSDGUI_ITEMATTRIB_SUBMENU;
		if ((type & XEMUGUI_MENUFLAG_CHECKED))
			attrib |= OSDGUI_ITEMATTRIB_CHECKED;
		if ((type & XEMUGUI_MENUFLAG_UNCHECKED))
			attrib |= OSDGUI_ITEMATTRIB_UNCHECKED;
		attribs[items] = attrib;
		DEBUGOSDGUI("Adding menu point #%d as %s with attribute %d" NL, items, desc[i].name, attrib);
		items++;
	}
	if (!items) {	// do not allow empty menu ...
		if (!osdgui.menudepth)
			FATAL("OSDGUI: empty root menu");
		osdgui.menudepth--;
		goto restart;
	}
	int space = osdgui.ytextres - OSDGUI_MENU_Y;
	if (space > items)
		space = items;
	//space = 3;
	for (int need_rendering = 1;;) {
		int retev;
		//const int options = (osdgui.menudepth ? OSDGUI_ITEMBROWSER_ALLOW_LEFT : 0) | OSDGUI_ITEMBROWSER_ALLOW_RIGHT | OSDGUI_ITEMBROWSER_ALLOW_F1;
		const int options = OSDGUI_ITEMBROWSER_ALLOW_LEFT | OSDGUI_ITEMBROWSER_ALLOW_RIGHT | OSDGUI_ITEMBROWSER_ALLOW_F1;
		int *selectedptr = &osdgui.menuselected[osdgui.menudepth];
		//static int _osdgui_do_browse_list_loop ( const char *textbuf[], const int all_items, const int page_items, int active_item, const int y, int need_rendering, int options, int *ev )
		*selectedptr = _osdgui_do_browse_list_loop(chooseptrs, attribs, items, space, *selectedptr, OSDGUI_MENU_Y, need_rendering, options, &retev);
		if (retev == OSDGUIKEY_ENTER || retev == OSDGUIKEY_RIGHT) {
			const struct menu_st *m = menuptrs[*selectedptr];
			const int type = types[*selectedptr] & 0xFF;
			if (type == XEMUGUI_MENUID_SUBMENU) {
				osdgui.menudepth++;
				if (osdgui.menudepth >= OSDGUI_MENU_MAXDEPTH)
					FATAL("OSDGUI: too deep menu structure!");
				*(selectedptr + 1) = 0;
				osdgui.menupath[osdgui.menudepth] = m->user_data;
				osdgui.menunames[osdgui.menudepth] = m->name;
				goto restart;
			}
			if (type == XEMUGUI_MENUID_CALLABLE) {
				_osdgui_prepare_return();
				((xemugui_callback_t)(m->handler))(m, NULL);
				return;
			}
		}
		if (retev == OSDGUIKEY_LEFT) {	// previous menu
			if (osdgui.menudepth) {
				osdgui.menudepth--;
				goto restart;
			}
#ifdef			XEMU_ARCH_ANDROID
			retev = OSDGUIKEY_ESC;	// trying to go to previous menu while in the main menu means leaving the menu
#endif
		}
		if (retev == OSDGUIKEY_F1) {	// go to main menu
			if (osdgui.menudepth) {
				osdgui.menudepth = 0;
				goto restart;
			} else {
				*selectedptr = 0;
				continue;
			}
		}
		if (retev == OSDGUIKEY_QUIT || retev == OSDGUIKEY_ESC) {
			_osdgui_prepare_return();
			return;
		}
		need_rendering = 0;
	}
}


static int xemuosdgui_popup ( const struct menu_st desc[] )
{
	osdgui.menupath[0] = desc;	// store root menu, as xemuosdgui_popup() is always called with that info
	// osdgui.menudepth = 0;	// use this not to remember the last menu we were in, but start from the root level
	_osdgui_process_menu();
	return 0;
}


static int _osdgui_qsort_helper ( const void *v1, const void *v2 )
{
	const char *p1 = *(const char**)v1;
	const char *p2 = *(const char**)v2;
	if (*p1 == '/' && *p2 != '/')
		return -1;
	if (*p1 != '/' && *p2 == '/')
		return 1;
	return strcasecmp(p1 + 1, p2 + 1);
}


// Provides a file selector UI on the current directory.
// Returns to a pointer of selected item (user must free that memory!), or NULL in case of aborted selection or error.
// This function can change current directory, the selected item is meant inside the cwd!
// Returns only with file selection, selected directory will be resolved inside with restarting the selection in that directory.
static char *_osdgui_fileselector ( const char *title )
{
	int selected = 0;
	char cwd[PATH_MAX+1];
	char *pos_to_this = NULL;
restart:
	if (getcwd(cwd, sizeof cwd) != cwd)
		strcpy(cwd, "??");
	osd_clear_with_colour(5);
	_osdgui_format_text((const Uint8*)"Xemu file selector", -1,  0, OSDGUI_SUPERTOP_COLOURPAIR);
	_osdgui_format_text((const Uint8*)title, -1,  1, OSDGUI_MSG_COLOURPAIR);
	osd_set_colours(OSDGUI_CWD_COLOURPAIR);
	for (int x = 0; x < osdgui.xtextres * 2 && cwd[x]; x++)	// only show max of two lines of the path in case of a long one ...
		osd_write_char(osdgui.xofs + (x % osdgui.xtextres) * osdgui.fontwidth, osdgui.yofs + (2 + x / osdgui.xtextres) * osdgui.fontwidth, cwd[x]);
	osd_texture_update(NULL);
	char *textdb = NULL;
	int textdb_size = 0;
	int textdb_alloc = 0;
	DIR *dir = opendir(".");
	if (!dir) {
		DEBUGPRINT("GUI: OSD: cannot open current directory?: %s" NL, strerror(errno));
		free(pos_to_this);
		return NULL;
	}
	int items = 0;
	for (;;) {
		errno = 0;
		const struct dirent *entry = readdir(dir);
		if (!entry) {
			if (errno) {
				DEBUGPRINT("GUI: OSD: cannot read directory \"%s\": %s" NL, cwd, strerror(errno));
				free(textdb);
				closedir(dir);
				free(pos_to_this);
				return NULL;
			}
			break;
		}
		const int len = strlen(entry->d_name);
		if (len > 256 || len < 1 || (len == 1 && entry->d_name[0] == '.'))
			continue;
		struct stat st;
		if (stat(entry->d_name, &st))
			continue;
		if (textdb_size + len + 2 > textdb_alloc) {
			textdb_alloc += 0x10000;
			textdb = xemu_realloc(textdb, textdb_alloc);
		}
		textdb[textdb_size] = (st.st_mode & S_IFMT) == S_IFDIR ? '/' : ' ';
		memcpy(textdb + textdb_size + 1, entry->d_name, len + 1);
		textdb_size += len + 2;
		items++;
	}
	textdb = xemu_realloc(textdb, textdb_size);
	closedir(dir);
	DEBUGOSDGUI("GUI: OSD: fileselector found %d file(s), using %d bytes of memory while scanning directory: %s" NL, items, textdb_size, cwd);
	const char **chooseptrs = xemu_malloc(items * sizeof(const char*));
	do {
		const char *p = textdb;
		for (int i = 0; i < items; i++, p += strlen(p) + 1)
			chooseptrs[i] = p;
	} while (0);
	qsort(chooseptrs, items, sizeof(const char*), _osdgui_qsort_helper);
	Uint32 *attribs = xemu_malloc(items * sizeof(Uint32));
	for (int i = 0; i < items; i++) {
		attribs[i] = *chooseptrs[i]++ == '/' ? OSDGUI_ITEMATTRIB_SUBMENU : 0;
		if (pos_to_this && !strcmp(pos_to_this, chooseptrs[i]))
			selected = i;
	}
	free(pos_to_this);
	pos_to_this = NULL;
	int retev;
	int need_rendering = 1;
	do {
		if (selected >= items)
			selected = 0;
		// _osdgui_do_browse_list_loop ( const char *textbuf[], const Uint32 attribs[], const int all_items, const int page_items, int active_item, const int y, int need_rendering, const int options, int *ev )
		selected = _osdgui_do_browse_list_loop(chooseptrs, attribs, items, osdgui.ytextres - 5, selected, 5, need_rendering, OSDGUI_ITEMBROWSER_ALLOW_LEFT | OSDGUI_ITEMBROWSER_ALLOW_RIGHT, &retev);
		need_rendering = 0;
		if (retev == OSDGUIKEY_RIGHT)
			retev = OSDGUIKEY_ENTER;
	} while (retev != OSDGUIKEY_ENTER && retev != OSDGUIKEY_ESC && retev != OSDGUIKEY_QUIT && retev != OSDGUIKEY_LEFT);
	char *selected_fn = NULL;
	int is_dir = (attribs[selected] & OSDGUI_ITEMATTRIB_SUBMENU);
	if (retev == OSDGUIKEY_ENTER)
		selected_fn = xemu_strdup(chooseptrs[selected]);
	if (retev == OSDGUIKEY_LEFT) {
		selected_fn = xemu_strdup("..");
		is_dir = OSDGUI_ITEMATTRIB_SUBMENU;
	}
	if (selected_fn && !strcmp(selected_fn, "..")) {
		// in case of selecting '..' (or using left key - back to the parent) we'd like to signal that it should
		// select our directory, thus it's easy to go back and forth within the filesystem. Ouch, hard to explain this in English for me :-O
		const char *p = strrchr(cwd, DIRSEP_CHR);
		if (p && p > cwd)
			pos_to_this = xemu_strdup(p + 1);
	}
	free(attribs);
	free(chooseptrs);
	free(textdb);
	if (!selected_fn) {
		free(pos_to_this);
		return NULL;
	}
	if (is_dir) {
		// Selected item is a directory, let's (try to) chdir into, and start again ...
		if (!chdir(selected_fn))	// if chdir was not succeeded, then do not reset "selected" item
			selected = 0;
		free(selected_fn);
		goto restart;
	}
	// Selected item is a file! Caller must free the memory associated with this pointer eventually.
	free(pos_to_this);
	return selected_fn;
}


void *_unused_voidptr;
#define IGNORE_RETVAL(x) _unused_voidptr = (void*)(uintptr_t)(x);


static int xemuosdgui_file_selector ( int dialog_mode, const char *dialog_title, char *default_dir, char *selected, int path_max_size )
{
	switch (dialog_mode & 3) {
		case XEMUGUI_FSEL_OPEN:
			break;
		case XEMUGUI_FSEL_SAVE:
			ERROR_WINDOW("File save dailog is not yet implemented by OSDGUI :(");
			return -1;
		default:
			FATAL("Invalid mode for UI file selector: %d", dialog_mode & 3);
	}
	static char *last_dir = NULL;
	char old_cwd[PATH_MAX+1];
	if (getcwd(old_cwd, sizeof old_cwd) != old_cwd)
		*old_cwd = '\0';
	if (last_dir)
		IGNORE_RETVAL(chdir(last_dir));
	if (default_dir && *default_dir)
		IGNORE_RETVAL(chdir(default_dir));
	char *selected_fn = _osdgui_fileselector(dialog_title);
	if (!selected_fn) {
		if (*old_cwd)
			IGNORE_RETVAL(chdir(old_cwd));
		_osdgui_prepare_return();
		return 1;
	}
	char new_cwd[PATH_MAX+1];
	if (getcwd(new_cwd, sizeof new_cwd) != new_cwd) {
		free(selected_fn);
		if (*old_cwd)
			IGNORE_RETVAL(chdir(old_cwd));
		_osdgui_prepare_return();
		return 1;
	}
	if (default_dir && (dialog_mode & XEMUGUI_FSEL_FLAG_STORE_DIR))
		snprintf(default_dir, path_max_size, "%s", new_cwd);
	xemu_restrdup(&last_dir, new_cwd);
	const int ret = snprintf(selected, path_max_size, "%s%c%s", new_cwd, DIRSEP_CHR, selected_fn);
	if (*old_cwd)
		IGNORE_RETVAL(chdir(old_cwd));
	free(selected_fn);
	_osdgui_prepare_return();
	if (ret <= 0 || ret >= path_max_size)
		return -1;
	return 0;
}


static const struct xemugui_descriptor_st xemuosdgui_descriptor = {
	.name		= "osd",
	.description	= "OSD based internal GUI",
	.init		= xemuosdgui_init,
	.shutdown	= NULL,
	.iteration	= NULL,
	.file_selector	= xemuosdgui_file_selector,
	.popup		= xemuosdgui_popup,
	.info		= xemuosdgui_info,
};
