/* Test-case for a primitive PC emulator inside the Xemu project,
   currently using Fake86's x86 CPU emulation.
   Part of the Xemu project, please visit: https://github.com/lgblgblgb/xemu
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

#include "xemu/emutools.h"
#include "xemu/emutools_files.h"
#include "xemu/emutools_hid.h"
#include "xemu/emutools_config.h"
#include "xemu/emutools_gui.h"



#include "memory.h"
#include "video.h"
#include "cpu.h"


#define REFRESH_RATE		60

//#include <ctype.h>
//#include <strings.h>

#define SCREEN_FORMAT           SDL_PIXELFORMAT_ARGB8888
#define USE_LOCKED_TEXTURE      1
#define RENDER_SCALE_QUALITY    0
#define SCREEN_WIDTH            640
#define SCREEN_HEIGHT           400

#define CPU_OPS_PER_SEC		4000000
//#define CPU_OPS_PER_SEC		REFRESH_RATE

#define CPU_OPS_PER_RUN CPU_OPS_PER_SEC / REFRESH_RATE


static const Uint8 init_pal[16 * 3] = {
	  0,   0,   0,
	  0,   0, 170,
	  0, 170,   0,
	  0, 170, 170,
	170,   0,   0,
	170,   0, 170,
	170,  85,   0,
	170, 170, 170,
	 85,  85,  85,
	 85,  85, 255,
	 85, 255,  85,
	 85, 255, 255,
	255,  85,  85,
	255,  85, 255,
	255, 255,  85,
	255, 255, 255
};


#define VIRTUAL_SHIFT_POS	0x03


/* Primo for real does not have the notion if "keyboard matrix", well or we
   can say it has 1*64 matrix (not like eg C64 with 8*8). Since the current
   Xemu HID structure is more about a "real" matrix with "sane" dimensions,
   I didn't want to hack it over, instead we use a more-or-less artificial
   matrix, and we map that to the Primo I/O port request on port reading.
   Since, HID assumes the high nibble of the "position" is the "row" and
   low nibble can be only 0-7 we have values like:
   $00 - $07, $10 - $17, $20 - $27, ...
   ALSO: Primo uses bit '1' for pressed, so we also invert value in
   the port read function above.
   FIXME: better map, missing keys, some differences between Primo models!
*/
static const struct KeyMappingDefault primo_key_map[] = {
	{ SDL_SCANCODE_Y,	0x00 },	// scan 0 Y
	{ SDL_SCANCODE_UP,	0x01 },	// scan 1 UP-ARROW
	{ SDL_SCANCODE_S,	0x02 },	// scan 2 S
	{ SDL_SCANCODE_LSHIFT,	0x03 },	{ SDL_SCANCODE_RSHIFT,  0x03 }, // scan 3 SHIFT
	{ SDL_SCANCODE_E,	0x04 },	// scan 4 E
	//{ SDL_SCANCODE_UPPER,	0x05 },	// scan 5 UPPER
	{ SDL_SCANCODE_W,	0x06 },	// scan 6 W
	{ SDL_SCANCODE_LCTRL,	0x07 },	// scan 7 CTR
	{ SDL_SCANCODE_D,	0x10 },	// scan 8 D
	{ SDL_SCANCODE_3,	0x11 },	// scan 9 3 #
	{ SDL_SCANCODE_X,	0x12 },	// scan 10 X
	{ SDL_SCANCODE_2,	0x13 },	// scan 11 2 "
	{ SDL_SCANCODE_Q,	0x14 },	// scan 12 Q
	{ SDL_SCANCODE_1,	0x15 },	// scan 13 1 !
	{ SDL_SCANCODE_A,	0x16 },	// scan 14 A
	{ SDL_SCANCODE_DOWN,	0x17 },	// scan 15 DOWN-ARROW
	{ SDL_SCANCODE_C,	0x20 },	// scan 16 C
	//{ SDL_SCANCODE_----,	0x21 },	// scan 17 ----
	{ SDL_SCANCODE_F,	0x22 },	// scan 18 F
	//{ SDL_SCANCODE_----,	0x23 },	// scan 19 ----
	{ SDL_SCANCODE_R,	0x24 },	// scan 20 R
	//{ SDL_SCANCODE_----,	0x25 },	// scan 21 ----
	{ SDL_SCANCODE_T,	0x26 },	// scan 22 T
	{ SDL_SCANCODE_7,	0x27 },	// scan 23 7 /
	{ SDL_SCANCODE_H,	0x30 },	// scan 24 H
	{ SDL_SCANCODE_SPACE,	0x31 },	// scan 25 SPACE
	{ SDL_SCANCODE_B,	0x32 },	// scan 26 B
	{ SDL_SCANCODE_6,	0x33 },	// scan 27 6 &
	{ SDL_SCANCODE_G,	0x34 },	// scan 28 G
	{ SDL_SCANCODE_5,	0x35 },	// scan 29 5 %
	{ SDL_SCANCODE_V,	0x36 },	// scan 30 V
	{ SDL_SCANCODE_4,	0x37 },	// scan 31 4 $
	{ SDL_SCANCODE_N,	0x40 },	// scan 32 N
	{ SDL_SCANCODE_8,	0x41 },	// scan 33 8 (
	{ SDL_SCANCODE_Z,	0x42 },	// scan 34 Z
	//{ SDL_SCANCODE_PLUS,	0x43 },	// scan 35 + ?
	{ SDL_SCANCODE_U,	0x44 },	// scan 36 U
	{ SDL_SCANCODE_0,	0x45 },	// scan 37 0
	{ SDL_SCANCODE_J,	0x46 },	// scan 38 J
	//{ SDL_SCANCODE_>,	0x47 },	// scan 39 > <
	{ SDL_SCANCODE_L,	0x50 },	// scan 40 L
	{ SDL_SCANCODE_MINUS,	0x51 },	// scan 41 - i
	{ SDL_SCANCODE_K,	0x52 },	// scan 42 K
	{ SDL_SCANCODE_PERIOD,	0x53 },	// scan 43 . :
	{ SDL_SCANCODE_M,	0x54 },	// scan 44 M
	{ SDL_SCANCODE_9,	0x55 },	// scan 45 9 ;
	{ SDL_SCANCODE_I,	0x56 },	// scan 46 I
	{ SDL_SCANCODE_COMMA,	0x57 },	// scan 47 ,
	//{ SDL_SCANCODE_U",	0x60 },	// scan 48 U"
	{ SDL_SCANCODE_APOSTROPHE,	0x61 },	// scan 49 ' #
	{ SDL_SCANCODE_P,	0x62 },	// scan 50 P
	//{ SDL_SCANCODE_u',	0x63 },	// scan 51 u' u"
	{ SDL_SCANCODE_O,	0x64 },	// scan 52 O
	{ SDL_SCANCODE_HOME,	0x65 },	// scan 53 CLS
	//{ SDL_SCANCODE_----,	0x66 },	// scan 54 ----
	{ SDL_SCANCODE_RETURN,	0x67 },	// scan 55 RETURN
	//{ SDL_SCANCODE_----,	0x70 },	// scan 56 ----
	{ SDL_SCANCODE_LEFT,	0x71 },	// scan 57 LEFT-ARROW
	//{ SDL_SCANCODE_E',	0x72 },	// scan 58 E'
	//{ SDL_SCANCODE_o',	0x73 },	// scan 59 o'
	//{ SDL_SCANCODE_A',	0x74 },	// scan 60 A'
	{ SDL_SCANCODE_RIGHT,	0x75 },	// scan 61 RIGHT-ARROW
	//{ SDL_SCANCODE_O:,	0x76 },	// scan 62 O:
	{ SDL_SCANCODE_ESCAPE,	0x77 },	// scan 63 BRK
	STD_XEMU_SPECIAL_KEYS,
	// **** this must be the last line: end of mapping table ****
	{ 0, -1 }
};



void clear_emu_events ( void )
{
	hid_reset_events(1);
}






void emu_quit_callback ( void )
{
	memory_save("debug.mem");
}






static void emulation_loop ( void )
{
	//DEBUGPRINT("Executing at %04X:%04X ops = %d" NL, X86_CS, X86_IP, CPU_OPS_PER_RUN);
	uint32_t ret = exec86(CPU_OPS_PER_RUN);
	//DEBUGPRINT("Executed %u ops" NL, ret);
	if (ret != CPU_OPS_PER_RUN)
		DEBUGPRINT("CPU discrepency, wanted %d ops, got %d" NL, CPU_OPS_PER_RUN, ret);

	video_render_text_screen();
	hid_handle_all_sdl_events();
	xemugui_iteration();
	//xemu_timekeeping_delay((Uint64)(1000000L * (Uint64)(all_cycles_spent - all_cycles_old) / (Uint64)cpu_clock));
	xemu_timekeeping_delay(1000000 / REFRESH_RATE);
}


void emu_dropfile_callback ( const char *fn )
{
}



// HID needs this to be defined, it's up to the emulator if it uses or not ...
int emu_callback_key ( int pos, SDL_Scancode key, int pressed, int handled )
{
#if 0
	if (!pressed && pos == -2 && key == 0 && handled == SDL_BUTTON_RIGHT) {
		DEBUGGUI("UI: handler has been called." NL);
		if (xemugui_popup(menu_main)) {
			DEBUGPRINT("UI: oops, POPUP does not worked :(" NL);
		}
	}
#endif
	//fprintf(stderr, "emu_callback_key has been fired! pos=%d, pressed=%d, handled=%d keyval=%d\n", pos, pressed, handled, key);
        return 0;
}


static const struct menu_st menu_main[] = {
#ifdef XEMU_ARCH_WIN
        { "System console",             XEMUGUI_MENUID_CALLABLE |
                                        XEMUGUI_MENUFLAG_QUERYBACK,     xemugui_cb_sysconsole, NULL },
#endif
        { "About",                      XEMUGUI_MENUID_CALLABLE,        xemugui_cb_about_window, NULL },
#ifdef HAVE_XEMU_EXEC_API
        { "Browse system folder",       XEMUGUI_MENUID_CALLABLE,        xemugui_cb_native_os_prefdir_browser, NULL },
#endif
        { "Quit",                       XEMUGUI_MENUID_CALLABLE,        xemugui_cb_call_quit_if_sure, NULL },
        { NULL }
};



#if 0
static void ui_enter ( void )
{
	DEBUGPRINT("UI: handler has been called." NL);
	if (xemugui_popup(menu_main)) {
		DEBUGPRINT("UI: oops, POPUP does not worked :(" NL);
	} else {
		DEBUGPRINT("UI: hmm, POPUP _seems_ to worked ..." NL);
	}
}
#endif


int main ( int argc, char **argv )
{
	int fullscreen, leave_syscon_open;
	char *selected_gui;
	xemu_pre_init(APP_ORG, TARGET_NAME, "The Unwanted PC emulator from LGB", argc, argv);
	xemucfg_define_switch_option("fullscreen", "Start in fullscreen mode", &fullscreen);
	xemucfg_define_switch_option("syscon", "Keep system console open (Windows-specific effect only)", &leave_syscon_open);
	xemucfg_define_str_option("gui", NULL, "Select GUI type for usage. Specify some insane str to get a list", &selected_gui);
	if (xemucfg_parse_all())
		return 1;
	/* Initiailize SDL - note, it must be before loading ROMs, as it depends on path info from SDL! */
	if (xemu_post_init(
		TARGET_DESC APP_DESC_APPEND,	// window title
		1,				// resizable window
		SCREEN_WIDTH, SCREEN_HEIGHT,	// texture sizes
		SCREEN_WIDTH, SCREEN_HEIGHT,	// logical size (width is doubled for somewhat correct aspect ratio)
		SCREEN_WIDTH, SCREEN_HEIGHT,	// window size (tripled in size, original would be too small)
		SCREEN_FORMAT,		// pixel format
		16,			//
		init_pal,		// -- "" --
		sdlpal,			// -- "" --
		RENDER_SCALE_QUALITY,	// render scaling quality
		USE_LOCKED_TEXTURE,	// 1 = locked texture access
		emu_quit_callback	// no emulator specific shutdown function
	))
		return 1;
	hid_init(
		primo_key_map,
		VIRTUAL_SHIFT_POS,
		SDL_ENABLE		// enable joystick HID events
	);
	osd_init_with_defaults();
	xemugui_init(selected_gui);
	// Initialize memory, which also calls bios init
	memory_init();
	video_reset();
	reset86();
	clear_emu_events();	// also resets the keyboard
	xemu_set_full_screen(fullscreen);
	if (!leave_syscon_open)
		sysconsole_close(NULL);
	xemu_timekeeping_start();	// we must call this once, right before the start of the emulation
	XEMU_MAIN_LOOP(emulation_loop, REFRESH_RATE, 1);
	return 0;
}
