/* A work-in-progess MEGA65 (Commodore-65 clone origins) emulator
   Part of the Xemu project, please visit: https://github.com/lgblgblgb/xemu
   Copyright (C)2016-2023 LGB (Gábor Lénárt) <lgblgblgb@gmail.com>

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
#include "matrix_mode.h"

#include "xemu/emutools_hid.h"
#include "xemu/cpu65.h"
#include "hypervisor.h"
#include "vic4.h"
#include "mega65.h"
#include "io_mapper.h"
#include "memory_mapper.h"

#include <ctype.h>
#include <string.h>


//#define DEBUGMATRIX	DEBUGPRINT
//#define DEBUGMATRIX	DEBUG
#define DEBUGMATRIX(...)


int in_the_matrix = 0;


// TODO: many inventions here eventlually should be moved into some common place as
// probably other emulators ("generic OSD console API"), and OSD GUI want to use them as well!

// TODO: some code here (Xemu specific matrix commands ...) should share interfade with
// the uart_mon/umon, and in fact, that should be accessed from here, as on a real MEGA65!


#define MATRIX(...) do { \
	char _buf_for_msg_[4096]; \
	CHECK_SNPRINTF(snprintf(_buf_for_msg_, sizeof _buf_for_msg_, __VA_ARGS__), sizeof _buf_for_msg_); \
	matrix_write_string(_buf_for_msg_); \
} while(0)

#define CURSOR_CHAR	0xDB
#define CURSOR_COLOUR	4
#define CLI_START_LINE	4
#define NORMAL_COLOUR	1
#define BANNER_COLOUR	2

static const Uint8 console_colours[] = {
	0x00, 0x00, 0x00, 0x80,		// 0: for background shade of the console
	0x00, 0xFF, 0x00, 0xFF,		// 1: normal green colour of the console text
	0xFF, 0xFF, 0x00, 0xFF,		// 2: alternative yellow colour of the console text
	0x00, 0x00, 0x00, 0x00,		// 3: totally transparent stuff
	0xFF, 0x00, 0x00, 0xFF		// 4: red
};
static Uint32 colour_mappings[16];
static Uint8 current_colour;

static int backend_xsize, backend_ysize;
static Uint32 *backend_pixels;
static int chrscreen_xsize, chrscreen_ysize;
static Uint8 *vmem = NULL;
static int current_x = 0, current_y = 0;
static const char *prompt = NULL;
static int need_update = 0;
static int reserve_top_lines = 0;	// reserve this amount of top lines when scrolling
static int init_done = 0;
static Uint8 queued_input;


#define PARTIAL_OSD_TEXTURE_UPDATE


static void matrix_update ( void )
{
	if (!need_update)
		return;
	Uint8 *vp = vmem;
	need_update &= ~1;
	int updated = 0;
#ifdef PARTIAL_OSD_TEXTURE_UPDATE
	int region = 0, x_min[2] = { 1000, 1000 }, y_min[2] = { 1000, 1000 }, x_max[2] = { -1, -1 }, y_max[2] = { -1, -1 };
#endif
	for (int y = 0; y < chrscreen_ysize; y++) {
#ifdef PARTIAL_OSD_TEXTURE_UPDATE
		if (XEMU_UNLIKELY(y == reserve_top_lines))
			region = 1;
#endif
		for (int x = 0; x < chrscreen_xsize; x++, vp += 2)
			if (need_update || (vp[1] & 0x80)) {
				updated++;
				const Uint8 *font = &vga_font_8x8[vp[0] << 3];
				vp[1] &= 0x7F;
				Uint32 *pix = backend_pixels + (y * 8) * backend_xsize + (x * 8);
				for (int line = 0; line < 8; line++, font++, pix += backend_xsize - 8)
					for (Uint8 bp = 0, data = *font; bp < 8; bp++, data <<= 1)
						*pix++ = colour_mappings[(vp[1] >> ((data & 0x80) ? 0 : 4)) & 0xF];
#ifdef PARTIAL_OSD_TEXTURE_UPDATE
				if (x < x_min[region]) x_min[region] = x;
				if (x > x_max[region]) x_max[region] = x;
				if (y < y_min[region]) y_min[region] = y;
				if (y > y_max[region]) y_max[region] = y;
#endif
			}
	}
	need_update = 0;
	if (updated) {
#ifdef PARTIAL_OSD_TEXTURE_UPDATE
		int area = 0;
		for (int i = 0; i < 2; i++)
			if (x_max[i] >= 0) {
				const SDL_Rect rect = {
					.x = x_min[i] * 8,
					.y = y_min[i] * 8,
					.w = (x_max[i] - x_min[i] + 1) * 8,
					.h = (y_max[i] - y_min[i] + 1) * 8
				};
				DEBUGMATRIX("MATRIX: update rectangle region #%d is %dx%d character wide at %d,%d" NL, i, x_max[i] - x_min[i] + 1, y_max[i] - y_min[i] + 1, x_min[i], y_min[i]);
				DEBUGMATRIX("MATRIX: update rectangle region #%d in pixels is %dx%d pixels at %d,%d" NL, i, rect.w, rect.h, rect.x, rect.y);
				osd_texture_update(&rect);
				area += rect.w * rect.h / 64;
			}
#else
		int area = chrscreen_xsize * chrscreen_ysize;
		osd_texture_update(NULL);
#endif
		DEBUGMATRIX("MATRIX: updated %d characters, %d OSD chars (=%.03f%%)" NL, updated, area, (double)(area * 100) / (double)(chrscreen_xsize * chrscreen_ysize));
	}
}


static void write_char_raw ( const int x, const int y, const Uint8 ch, const Uint8 col )
{
	if (XEMU_UNLIKELY(x < 0 || y < 0 || x >= chrscreen_xsize || y >= chrscreen_ysize))
		return;
	Uint8 *v = vmem + (chrscreen_xsize * y + x) * 2;
	if (XEMU_LIKELY(v[0] != ch))
		v[0] = ch, v[1] = col | 0x80;
	else if (XEMU_UNLIKELY((v[1] ^ col) & 0x7F))
		v[1] = col | 0x80;
	need_update |= 1;
}


static void matrix_clear ( void )
{
	for (Uint8 *vp = vmem, *ve = vmem + chrscreen_xsize * chrscreen_ysize * 2; vp < ve; vp += 2)
		vp[0] = 0x20, vp[1] = current_colour;
	need_update |= 2;
}


static void matrix_write_char ( const Uint8 c )
{
	if (c == '\n' || c == '\r') {
		current_x = 0;
		current_y++;
	} else if (c == 8) {
		if (current_x > 0)
			current_x--;
	} else {
		write_char_raw(current_x, current_y, c, current_colour);
		current_x++;
		if (current_x >= chrscreen_xsize) {
			current_x = 0;
			current_y++;
		}
	}
	if (current_y >= chrscreen_ysize) {
		current_y = chrscreen_ysize - 1;
		DEBUGMATRIX("MATRIX: scrolling ... reserved=%d lines" NL, reserve_top_lines);
		memmove(
			vmem + 2 * chrscreen_xsize *  reserve_top_lines,
			vmem + 2 * chrscreen_xsize * (reserve_top_lines + 1),
			chrscreen_xsize * (chrscreen_ysize - 1 - reserve_top_lines) * 2
		);
		for (Uint8 x = 0, *vp = vmem + (chrscreen_ysize - 1) * chrscreen_xsize * 2; x < chrscreen_xsize; x++, vp += 2)
			vp[0] = 0x20, vp[1] = current_colour;
		need_update |= 2;
	}
}


static inline void matrix_write_string ( const char *s )
{
	while (*s)
		matrix_write_char(*s++);
}


static void dump_regs ( const char rot_fig )
{
	static const Uint8 io_mode_xlat[4] = { 2, 3, 0, 4 };
	const Uint8 pf = cpu65_get_pf();
	MATRIX("PC:%04X A:%02X X:%02X Y:%02X Z:%02X SP:%04X B:%02X %c%c%c%c%c%c%c%c IO:%d (%c) %c %s %s     ",
		cpu65.pc, cpu65.a, cpu65.x, cpu65.y, cpu65.z,
		cpu65.sphi + cpu65.s, cpu65.bphi >> 8,
		(pf & CPU65_PF_N) ? 'N' : 'n',
		(pf & CPU65_PF_V) ? 'V' : 'v',
		(pf & CPU65_PF_E) ? 'E' : 'e',
		'-',
		(pf & CPU65_PF_D) ? 'D' : 'd',
		(pf & CPU65_PF_I) ? 'I' : 'i',
		(pf & CPU65_PF_Z) ? 'Z' : 'z',
		(pf & CPU65_PF_C) ? 'C' : 'c',
		io_mode_xlat[vic_iomode],
		!!in_hypervisor ? 'H' : 'U',
		rot_fig,
		videostd_id ? "NTSC" : "PAL ",
		cpu_clock_speed_string
	);
}


/* COMMANDS */


static void cmd_off ( char *p )
{
	matrix_mode_toggle(0);
}


static void cmd_uname ( char *p )
{
	matrix_write_string(xemu_get_uname_string());
}


static void cmd_ver ( char *p )
{
	MATRIX("Xemu/%s %s %s %s %s %s\n", TARGET_DESC, XEMU_BUILDINFO_CDATE, XEMU_BUILDINFO_GIT, XEMU_BUILDINFO_ON, XEMU_BUILDINFO_AT, XEMU_BUILDINFO_CC);
	MATRIX("SDL base dir: %s\n", sdl_base_dir);
	MATRIX("SDL pref dir: %s\n", sdl_pref_dir);
	matrix_write_string("Command line: ");
	for (int i = 0; i < xemu_initial_argc; i++) {
		if (i)
			matrix_write_string(" ");
		matrix_write_string(xemu_initial_argv[i]);
	}
}


static void cmd_reg ( char *p )
{
	dump_regs(' ');
}


static Uint16 addr_hiword = 0, addr_loword = 0; // current dump/show/write address. hiword=$FFFF indicates _CPU_ address
static Uint8 data_byte;


static int mem_args ( const char *p, int need_data )
{
	unsigned int addr, data;
	int ret = sscanf((*p == '!' || *p == '?' || *p == '=') ? p + 1 : p, "%x %x", &addr , &data);
	if (ret < 1) {
		matrix_write_string("?MISSING ARG OR HEX SYNTAX ERROR");
		return 0;
	}
	if (need_data && ret == 1) {
		matrix_write_string("?MISSING SECOND ARG OR HEX SYNTAX ERROR");
		return 0;
	}
	if (!need_data && ret > 1) {
		matrix_write_string("?EXTRA UNKNOWN ARG");
		return 0;
	}
	// Address part
	if (*p == '!') {
		if (addr > 0xFFFF) {
			matrix_write_string("?CPU ADDRESS MUST BE 16-BIT");
			return 0;
		}
		addr_hiword = 0xFFFF;
	} else if (addr > 0xFFFF || *p == '=') {
		if (addr >= 0xFFFFFFFU) {
			matrix_write_string("?LINEAR ADDRESS MUST BE 28-BIT");
			return 0;
		}
		addr_hiword = (addr >> 16) & 0xFFF;
	}
	addr_loword = addr & 0xFFFF;
	if (*p == '?') {
		if (addr > 0xFFF) {
			matrix_write_string("?IO ADDRESS MUST BE 12-BIT");
			return 0;
		}
		// M65 I/O is @ $FFD'3FFF
		addr_loword = (addr_loword & 0xFFF) + 0x3000;
		addr_hiword = 0xFFD;
	}
	// Data part
	if (need_data) {
		if (data > 0xFF) {
			matrix_write_string("?DATA ARG MUST BE 8-BIT");
			return 0;
		}
		data_byte = data & 0xFF;
		return 2;
	}
	return 1;
}


static void cmd_log ( char *p )
{
	DEBUGPRINT("USERLOG: %s" NL, *p ? p : "<EMPTY-TEXT>");
}


static void cmd_write ( char *p )
{
	if (mem_args(p, 1) != 2)
		return;
	if (addr_hiword == 0xFFFF) {
		cpu65_write_callback(addr_loword, data_byte);
		return;
	}
	const Uint32 addr = ((Uint32)addr_hiword << 16) + (Uint32)addr_loword;
	memory_debug_write_phys_addr(addr, data_byte);
}


static void cmd_show ( char *p )
{
	if (mem_args(p, 0) != 1)
		return;
	if (addr_hiword == 0xFFFF) {
		MATRIX("[cpu:%04X] = %02X", addr_loword, cpu65_read_callback(addr_loword));
		return;
	}
	const Uint32 addr = ((Uint32)addr_hiword << 16) + (Uint32)addr_loword;
	MATRIX("[%03X:%04X] = %02X", addr_hiword, addr_loword, memory_debug_read_phys_addr(addr));
}


static void cmd_dump ( char *p )
{
	if (*p && mem_args(p, 0) != 1)
		return;
	char chardump[79];
	memset(chardump, 0, sizeof chardump);
	for (int lines = 0; lines < 16; lines++) {
		if (addr_hiword != 0xFFFF) {
			// Clamp to 28 bit address space, unless hiword is $FFFF meaning CPU view, thus having a special meaning
			addr_hiword &= 0xFFF;
			sprintf(chardump + 1, "%03X:%04X", addr_hiword, addr_loword);
		} else
			sprintf(chardump + 1, "cpu:%04X", addr_loword);
		for (int i = 0; i < 16; i++) {
			Uint8 data;
			if (addr_hiword == 0xFFFF)
				data = cpu65_read_callback(addr_loword++);
			else {
				data = memory_debug_read_phys_addr(((Uint32)addr_hiword << 16) + (Uint32)addr_loword);
				addr_loword++;
				if (!addr_loword)
					addr_hiword++;
			}
			sprintf(chardump + 12 + i * 3, "%02X", data);
			chardump[61 + i] =  data >= 32 ? data : '.';
		}
		for (int i = 0; i < sizeof(chardump) - 2; i++)
			if (!chardump[i])
				chardump[i] = 0x20;
		chardump[sizeof(chardump) - 2] = '\n';
		matrix_write_string(chardump);
	}
}


static void cmd_help ( char *p );


static const struct command_tab_st {
	const char *cmdname;
	void (*cb)(char*);
	const char *shortnames;
} command_tab[] = {
	{ "dump",	cmd_dump,	"d"	},
	{ "exit",	cmd_off,	"x"	},
	{ "help",	cmd_help,	"h?"	},
	{ "log",	cmd_log,	NULL	},
	{ "reg",	cmd_reg,	"r"	},
	{ "show",	cmd_show,	"s"	},
	{ "uname",	cmd_uname,	NULL	},
	{ "ver",	cmd_ver,	NULL	},
	{ "write",	cmd_write,	"w"	},
	{ .cmdname = NULL			},
};


static void cmd_help ( char *p )
{
	matrix_write_string("Available commands:");
	for (const struct command_tab_st *p = command_tab; p->cmdname; p++) {
		MATRIX(" %s", p->cmdname);
		if (p->shortnames)
			MATRIX("(%s)", p->shortnames);
	}
}


static void execute ( char *cmd )
{
	char *sp = strchr(cmd, ' ');
	if (sp)
		*sp++ = '\0';
	else
		sp = "";
	const int sname = !cmd[1] ? tolower(*cmd) : 1;
	for (const struct command_tab_st *p = command_tab; p->cmdname; p++)
		if ((p->shortnames && strchr(p->shortnames, sname)) || !strcasecmp(p->cmdname, cmd)) {
			p->cb(sp);
			return;
		}
	MATRIX("?SYNTAX ERROR IN \"%s\"", cmd);
}


static void input ( const char c )
{
	static int start_x;
	static Uint8 prev_char;
	if (!current_x) {
		matrix_write_string(prompt && *prompt ? prompt : "Xemu>");
		start_x = current_x;
		prev_char = 0;
		if (!c)
			return;
	}
	if (current_x < chrscreen_xsize - 1 && (c >= 33 || (c == 32 && current_x > start_x && prev_char != 32))) {
		matrix_write_char(c);
		prev_char = c;
	} else if (current_x > start_x && c == 8) {
		static const char backspace_seq_str[] = { 8, 32, 8, 0 };
		matrix_write_string(backspace_seq_str);
	} else if (c == '\r' || c == '\n') {
		const int len = current_x - start_x;
		char cmd[len + 1];
		for (int i = 0, j = (current_y * chrscreen_xsize + start_x) << 1; i < len; i++, j += 2)
			cmd[i] = vmem[j];
		cmd[len - (prev_char == 32)] = 0;
		matrix_write_char('\n');
		if (*cmd)
			execute(cmd);
		if (current_x)
			matrix_write_char('\n');
	}
}




static void matrix_updater_callback ( void )
{
	if (!in_the_matrix)
		return;		// should not happen, but ... (and can be a race condition with toggle function anyway!)
	write_char_raw(current_x, current_y, ' ', current_colour);	// delete cursor
	const int saved_x = current_x, saved_y = current_y;
	current_x = 0;
	current_y = 1;
	static const char rotator[4] = { '-', '\\', '|', '/' };
	static Uint32 counter = 0;
	dump_regs(rotator[(counter >> 3) & 3]);
	current_x = saved_x;
	current_y = saved_y;
	if (queued_input) {
		DEBUGMATRIX("MATRIX-INPUT: [%c] (%d)" NL, queued_input >= 32 ? queued_input : ' ', queued_input);
		input(queued_input);
		queued_input = 0;
	} else if (current_x == 0)
		input(0);	// just to have some prompt by default
	if (counter & 8)
		write_char_raw(current_x, current_y, CURSOR_CHAR, CURSOR_COLOUR);	// show cursor (gated with some bit of the "counter" to have blinking)
	counter++;
	matrix_update();
}

// TODO+FIXME: though the theory to hijack keyboard input looks nice, we have some problems:
// * hotkeys does not work anymore
// * if in mouse grab mode, you don't even have your mouse to be able to close window, AND/OR use the menu system


// Async stuff as callbacks. Only updates "queued_input"

static int kbd_cb_keyevent ( SDL_KeyboardEvent *ev )
{
	if (ev->state == SDL_PRESSED && ev->keysym.sym > 0 && ev->keysym.sym < 32 && !queued_input) {
		// Monitor alt-tab, as the main handler is defunct at this point, we "hijacked" it!
		// So we must give up the matrix mode themselves here
		if (ev->keysym.sym == 9 && (ev->keysym.mod & KMOD_RALT))
			matrix_mode_toggle(0);
		else
			queued_input = ev->keysym.sym;
	}
	return 0;	// do NOT execute further handlers!
}


static int kbd_cb_textevent ( SDL_TextInputEvent *ev )
{
	const uint8_t c = ev->text[0];
	if (c >= 32 && c < 128 && !queued_input)
		queued_input = c;
	return 0;	// do NOT execute further handlers!
}


void matrix_mode_toggle ( int status )
{
	if (!is_osd_enabled()) {
		ERROR_WINDOW("OSD is not enabled to be able to use Matrix mode.");
		return;
	}
	status = !!status;
	if (status == !!in_the_matrix)
		return;
	in_the_matrix = status;
	static int saved_allow_mouse_grab;
	if (in_the_matrix) {
		D6XX_registers[0x72] |= 0x40;	// be sure we're sync with the matrix bit!
		osd_hijack(matrix_updater_callback, &backend_xsize, &backend_ysize, &backend_pixels);
		if (!init_done) {
			init_done = 1;
			for (int i = 0; i < sizeof(console_colours) / 4; i++)
				colour_mappings[i] = SDL_MapRGBA(sdl_pix_fmt, console_colours[i * 4], console_colours[i * 4 + 1], console_colours[i * 4 + 2], console_colours[i * 4 + 3]);
			chrscreen_xsize = backend_xsize / 8;
			chrscreen_ysize = backend_ysize / 8;
			vmem = xemu_malloc(chrscreen_xsize * chrscreen_ysize * 2);
			current_colour = NORMAL_COLOUR;
			matrix_clear();
			current_colour = BANNER_COLOUR;
			static const char banner_msg[] = "*** Xemu's pre-matrix ... press right-ALT + TAB to exit ***";
			current_x = (chrscreen_xsize - strlen(banner_msg)) >> 1;
			matrix_write_string(banner_msg);
			current_colour = NORMAL_COLOUR;
			current_y = CLI_START_LINE;
			current_x = 0;
			reserve_top_lines = CLI_START_LINE;
			matrix_write_string("INFO: Remember, there is no spoon.\nINFO: Hot-keys do not work in matrix mode!\nINFO: Matrix mode bypasses emulation keyboard mappings.\n");
		}
		need_update = 2;
		matrix_update();
		DEBUGPRINT("MATRIX: ON (%dx%d OSD texture pixels, %dx%d character resolution)" NL,
			backend_xsize, backend_ysize, chrscreen_xsize, chrscreen_ysize
		);
		// "hijack" input for ourselves ...
	        hid_register_sdl_keyboard_event_callback(HID_CB_LEVEL_CONSOLE, kbd_cb_keyevent);
		hid_register_sdl_textinput_event_callback(HID_CB_LEVEL_CONSOLE, kbd_cb_textevent);
		queued_input = 0;
		// give up mouse grab if matrix mode is selected, since in matrix mode no hotkeys works,
		// thus user would also lose the chance to use the mouse at least to close, access menu, whatever ...
		if (is_mouse_grab()) {
			if (!current_x)	// skip warning in this case, since probably command line editing is in effect thus we would break that ...
				matrix_write_string("WARN: mouse grab/emu is released for matrix mode\n");
			set_mouse_grab(SDL_FALSE, 0);
		}
		saved_allow_mouse_grab = allow_mouse_grab;
		allow_mouse_grab = 0;
	} else {
		D6XX_registers[0x72] &= ~0x40;	// be sure we're sync with the matrix bit!
		osd_hijack(NULL, NULL, NULL, NULL);
		DEBUGPRINT("MATRIX: OFF" NL);
		// release our custom console events
	        hid_register_sdl_keyboard_event_callback(HID_CB_LEVEL_CONSOLE, NULL);
		hid_register_sdl_textinput_event_callback(HID_CB_LEVEL_CONSOLE, NULL);
		allow_mouse_grab = saved_allow_mouse_grab;
	}
	clear_emu_events();	// cure problems, that triggering/switching-on/off matrix screen cause that ALT key remains latched etc ...
}
