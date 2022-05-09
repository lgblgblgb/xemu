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


/* ------------------------- OSD GUI ---------------------- */


// FIXME TODO: Currently, OSD GUI is *extremely* primitive and hacky, using own SDL mechanisms to "steal"
// control from the normal ones while any wait and/or interaction is needed!
// Also we can't use the emulated machine's texture, as that texture may be locked or not, not safe to
// be used. Thus we do all the SDL level rendering here and using only the OSD texture, and we also
// need to handle all SDL events here during "UI-time" as well.


// Filled by xemuosdgui_init(), because the GUI subsystem has no direct access for OSD layer internals
static struct {
	//SDL_Texture	*tex;
	Uint32	*pixels;
	int	xsize, ysize;
	int	fontwidth, fontheight;
	int	xtextres, ytextres;
	int	xofs, yofs;
} osdgui;


#define OSDGUIKEY_QUIT		0
#define OSDGUIKEY_UP		1
#define OSDGUIKEY_DOWN		2
#define OSDGUIKEY_ENTER		13
#define OSDGUIKEY_ESC		27


static int _osdgui_sdl_loop ( int need_rendering )
{
	static const struct {
		const SDL_Scancode scan;
		const int func;
	} scan2osdfunc[] = {
		{ SDL_SCANCODE_RETURN, OSDGUIKEY_ENTER }, { SDL_SCANCODE_KP_ENTER, OSDGUIKEY_ENTER }, { SDL_SCANCODE_RETURN2, OSDGUIKEY_ENTER },
		{ SDL_SCANCODE_ESCAPE, OSDGUIKEY_ESC   }, { SDL_SCANCODE_0,        '0'             }, { SDL_SCANCODE_KP_0,    '0'             },
		{ SDL_SCANCODE_1,      '1'             }, { SDL_SCANCODE_KP_1,     '1'             }, { SDL_SCANCODE_2,       '2'             },
		{ SDL_SCANCODE_KP_2,   '2'             }, { SDL_SCANCODE_3,        '3'             }, { SDL_SCANCODE_KP_3,    '3'             },
		{ SDL_SCANCODE_4,      '4'             }, { SDL_SCANCODE_KP_4,     '4'             }, { SDL_SCANCODE_5,       '5'             },
		{ SDL_SCANCODE_KP_5,   '5'             }, { SDL_SCANCODE_6,        '6'             }, { SDL_SCANCODE_KP_6,    '6'             },
		{ SDL_SCANCODE_7,      '7'             }, { SDL_SCANCODE_KP_7,     '7'             }, { SDL_SCANCODE_8,       '8'             },
		{ SDL_SCANCODE_KP_8,   '8'             }, { SDL_SCANCODE_9,        '9'             }, { SDL_SCANCODE_KP_9,    '9'             },
		{ SDL_SCANCODE_UP,     OSDGUIKEY_UP    }, { SDL_SCANCODE_DOWN,     OSDGUIKEY_DOWN  },
		{ -1 , -1 }
	};
	SDL_PumpEvents();		// make sure we flush some events maybe pending
	SDL_FlushEvent(SDL_KEYDOWN);
	SDL_FlushEvent(SDL_WINDOWEVENT);
	for (;;) {
		if (need_rendering) {
			DEBUGPRINT("GUI: OSD: rendering ..." NL);
			osd_only_sdl_render_hack();	// render texture
			need_rendering = 0;
		}
		SDL_Event event;
		if (SDL_PollEvent(&event)) {
			switch (event.type) {
				case SDL_QUIT:
					SDL_PushEvent(&event);	// add event back, so main program will receive the quit request
					return OSDGUIKEY_QUIT;
				case SDL_KEYUP:
					break;
				case SDL_KEYDOWN:
					for (int a = 0; scan2osdfunc[a].func >= 0; a++)
						if (scan2osdfunc[a].scan == event.key.keysym.scancode)
							return scan2osdfunc[a].func;
					DEBUGPRINT("GUI: OSD: unhandled key %c (%d)" NL,
						event.key.keysym.scancode >= 32 && event.key.keysym.scancode < 127 ? event.key.keysym.scancode : '?',
						event.key.keysym.scancode
					);
					break;
				case SDL_WINDOWEVENT:
					if (event.window.event == SDL_WINDOWEVENT_EXPOSED)
						need_rendering = 1;
					break;
				default:
					DEBUGPRINT("GUI: OSD: unhandled event type %d" NL, (int)event.type);
					break;
			}
		} else {
			SDL_Delay(10);
			SDL_PumpEvents();
		}
	}
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


#define OSDGUI_QUESTIONS_Y	2
#define OSDGUI_QUESTIONS_X	1
#define OSDGUI_QUESTIONS_FG	5
#define OSDGUI_QUESTIONS_BG	3


static int _osdgui_msg_window ( const char *title, const char *msg, const SDL_MessageBoxButtonData *buttons, const int nbuttons )
{
	osd_clear_with_colour(5);
	_osdgui_format_text((const Uint8*)title, -1,  0, 1, 0);
	int y = OSDGUI_QUESTIONS_Y;
	int current = 0;
	int esc_buttonid = buttons[0].buttonid;
	for (int a = 0; a < nbuttons; a++) {
		char text[strlen(buttons[a].text) + 64];
		sprintf(
			text, "%d %s",
			a + 1,
			buttons[a].text
		);
		if ((buttons[a].flags & SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT))
			current = a;
		if ((buttons[a].flags & SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT))
			esc_buttonid = buttons[a].buttonid;
		_osdgui_format_text((const Uint8*)text, 0, y++, OSDGUI_QUESTIONS_FG, OSDGUI_QUESTIONS_BG);
	}
	y++;
	_osdgui_format_text((const Uint8*)msg,   -1, y, 1, 4);
	int result = 0;
	// the strip which covers the selection bar vertically, also gives x/y pixel position for the top-most one
	const SDL_Rect rect = {
		.x = (OSDGUI_QUESTIONS_X) * osdgui.fontwidth  + osdgui.xofs,
		.y = (OSDGUI_QUESTIONS_Y) * osdgui.fontheight + osdgui.yofs,
		.w = osdgui.fontwidth,
		.h = osdgui.fontheight * nbuttons
	};
	osd_set_colours(OSDGUI_QUESTIONS_FG, OSDGUI_QUESTIONS_BG);
	osd_write_char(rect.x, rect.y + (osdgui.fontheight * current), '>');
	osd_texture_update(NULL);	// update "pixels" buffer to the texture
	for (int need_rendering = 1;;) {
		const int ret = _osdgui_sdl_loop(need_rendering);
		need_rendering = 0;
		if ((ret == OSDGUIKEY_UP || ret == OSDGUIKEY_DOWN) && nbuttons > 1) {
			osd_write_char(rect.x, rect.y + (osdgui.fontheight * current), ' ');
			current += (ret == OSDGUIKEY_UP) ? -1 : 1;
			if (current < 0)
				current = nbuttons - 1;
			else if (current == nbuttons)
				current = 0;
			osd_write_char(rect.x, rect.y + (osdgui.fontheight * current), '>');
			osd_texture_update(&rect);
			need_rendering = 1;
			continue;
		}
		if (ret == OSDGUIKEY_ENTER) {
			result = buttons[current].buttonid;
			goto take;
		}
		if (ret == OSDGUIKEY_QUIT || ret == OSDGUIKEY_ESC) {
			result = esc_buttonid;
			goto take;
		}
		if (ret >= '1' && ret <= '9' && ret - '1' < nbuttons) {
			result = buttons[ret - '1'].buttonid;
			goto take;
		}
	}
take:
	osd_off();
	osd_set_colours(1, 0);	// restore original OSD colours in use for fg/bg
	return result;
}


static const SDL_MessageBoxButtonData dismiss_button = {
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
	return _osdgui_msg_window(title, msg, &dismiss_button, 1);
}


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
		buttons = &dismiss_button,
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
	return 0;
}


static int _osdgui_unimplemented ( const char *what )
{
	ERROR_WINDOW(
		"OSD-GUI does not yet implement %s :-(\n"
		"If you didn't forced OSD-GUI it can be the fact\n"
		"that you compiled Xemu without GTK3 support\n"
		"and your platform is not MacOS nor Windows either.",
		what
	);
	return 1;
}

static int xemuosdgui_popup ( const struct menu_st desc[] )
{
	return _osdgui_unimplemented("popup menu");
}

static int xemuosdgui_file_selector ( int dialog_mode, const char *dialog_title, char *default_dir, char *selected, int path_max_size )
{
	return _osdgui_unimplemented("file selector");
}


static const struct xemugui_descriptor_st xemuosdgui_descriptor = {
	.name		= "osd",
	.description	= "OSD based internal GUI, seriously work in progress!",
	.init		= xemuosdgui_init,
	.shutdown	= NULL,
	.iteration	= NULL,
	.file_selector	= xemuosdgui_file_selector,
	.popup		= xemuosdgui_popup,
	.info		= xemuosdgui_info,
};
