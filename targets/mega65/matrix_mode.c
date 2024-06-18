/* A work-in-progess MEGA65 (Commodore-65 clone origins) emulator
   Part of the Xemu project, please visit: https://github.com/lgblgblgb/xemu
   Copyright (C)2016-2024 LGB (Gábor Lénárt) <lgblgblgb@gmail.com>

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
#include "xemu/cpu65_disasm.h"
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

// TODO: some code here (Xemu specific matrix commands ...) should share interface with
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

#define FULL_SCREEN_BYTE_SIZE	(chrscreen_ysize * chrscreen_xsize * 2)
#define TOP_AREA_BYTE_SIZE	(CLI_START_LINE * chrscreen_xsize * 2)
#define CLI_AREA_BYTE_SIZE	((chrscreen_ysize - CLI_START_LINE) * chrscreen_xsize * 2)
#define CLI_AREA_BYTE_OFFSET	TOP_AREA_BYTE_SIZE

static Uint8 console_colours[] = {
	0x00, 0x00, 0x00, 0x80,		// 0: for background shade of the console
	0x00, 0xFF, 0x00, 0xFF,		// 1: normal green colour of the console text
	0xFF, 0xFF, 0x00, 0xFF,		// 2: alternative yellow colour of the console text
	0x00, 0x00, 0x00, 0x00,		// 3: totally transparent stuff
	0xFF, 0x00, 0x00, 0xFF		// 4: red
};
static Uint32 colour_mappings[16];
static Uint8 current_colour;

#define PROMPT		"Xemu>"

#define CMD_HISTORY_SIZE	64
static char *history[CMD_HISTORY_SIZE];

static char *cmdbuf;

static int backend_xsize, backend_ysize;
static Uint32 *backend_pixels;
static int chrscreen_xsize, chrscreen_ysize;
static Uint8 *vmem = NULL, *vmem_prev = NULL, *vmem_backup = NULL;
static int current_x = 0, current_y = 0;
static int need_update = 0;
static int init_done = 0;
static Uint8 queued_input;
static unsigned int matrix_counter = 0;
static int live_update_enabled = 1;
static int blink_phase = 0;		// used with flashing the cursor, etc
static int current_viewport = 0;
static int write_special_chars_mode = 0;


#define PARTIAL_OSD_TEXTURE_UPDATE


static void init_colour_mappings ( void )
{
	for (unsigned int i = 0; i < sizeof(console_colours) / 4; i++)
		colour_mappings[i] = SDL_MapRGBA(sdl_pix_fmt, console_colours[i * 4], console_colours[i * 4 + 1], console_colours[i * 4 + 2], console_colours[i * 4 + 3]);
}


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
		if (XEMU_UNLIKELY(y == CLI_START_LINE))
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
	} else if (c == '{' && write_special_chars_mode) {
		if (write_special_chars_mode == 2)
			current_colour = BANNER_COLOUR;
		return;
	} else if (c == '}' && write_special_chars_mode) {
		if (write_special_chars_mode == 2)
			current_colour = NORMAL_COLOUR;
		return;
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
		DEBUGMATRIX("MATRIX: scrolling ... reserved=%d lines" NL, CLI_START_LINE);
		memmove(		// scroll the the previous viewport
			vmem_prev,
			vmem_prev + chrscreen_xsize * 2,
			CLI_AREA_BYTE_SIZE - (2 * chrscreen_xsize)
		);
		memcpy(			// copy a single line which "migrates" from the current to the previous viewport
			vmem_prev + CLI_AREA_BYTE_SIZE - (2 * chrscreen_xsize),	// to the _END_ of "vmem_prev"
			vmem + CLI_AREA_BYTE_OFFSET,
			chrscreen_xsize * 2
		);
		memmove(		// scroll the current viewport
			vmem + CLI_AREA_BYTE_OFFSET,
			vmem + CLI_AREA_BYTE_OFFSET + 2 * chrscreen_xsize,
			CLI_AREA_BYTE_SIZE - 2 * chrscreen_xsize
		);
		for (Uint8 x = 0, *vp = vmem + (chrscreen_ysize - 1) * chrscreen_xsize * 2; x < chrscreen_xsize; x++, vp += 2)
			vp[0] = 0x20, vp[1] = current_colour;
		need_update |= 2;
	}
}


static void set_viewport ( const int vp )
{
	if (vp == current_viewport)
		return;
	current_viewport = vp;
	if (vp) {
		memcpy(vmem_backup,                 vmem + CLI_AREA_BYTE_OFFSET, CLI_AREA_BYTE_SIZE);	// Backup the actual page so we can restore it later
		memcpy(vmem + CLI_AREA_BYTE_OFFSET, vmem_prev,                   CLI_AREA_BYTE_SIZE);
	} else {
		memcpy(vmem + CLI_AREA_BYTE_OFFSET, vmem_backup,                 CLI_AREA_BYTE_SIZE);
	}
	need_update |= 2;
}


static inline void matrix_write_string ( const char *s )
{
	while (*s)
		matrix_write_char(*s++);
}


static void dump_regs ( const char rot_fig )
{
	const Uint8 pf = cpu65_get_pf();
	MATRIX("{PC}:%04X {A}:%02X {X}:%02X {Y}:%02X {Z}:%02X {SP}:%04X {B}:%02X %c%c%c%c%c%c%c%c {IO}:%X (%c) %c %s %s%c",
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
		iomode_hexdigitids[io_mode],
		in_hypervisor ? 'H' : 'U',
		rot_fig,
		videostd_id ? "NTSC" : "PAL ",
		cpu_clock_speed_string_p,
		(D6XX_registers[0x7D] & 16) ? '!' : ' '
	);
}


static void dump_map ( void )
{
	char desc[10];
	for (unsigned int i = 0; i < 16; i++) {
		memory_cpu_addr_to_desc(i << 12, desc, sizeof desc);
		MATRIX("{%X:}%7s%c", i, desc, (i & 7) == 7 ? ' ' : '|');
	}
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
	MATRIX(
		"Xemu/%s %s %s %s %s %s\n"
		"SDL base dir: %s\n"
		"SDL pref dir: %s\n"
		"Command line:",
		TARGET_DESC, XEMU_BUILDINFO_CDATE, XEMU_BUILDINFO_GIT, XEMU_BUILDINFO_ON, XEMU_BUILDINFO_AT, XEMU_BUILDINFO_CC,
		sdl_base_dir,
		sdl_pref_dir
	);
	for (int i = 0; i < xemu_initial_argc; i++) {
		matrix_write_char(' ');
		matrix_write_string(xemu_initial_argv[i]);
	}
}


static void cmd_reg ( char *p )
{
	write_special_chars_mode = 1;
	dump_regs(' ');
	write_special_chars_mode = 0;
}


static void cmd_map ( char *arg )
{
	write_special_chars_mode = 1;
	dump_map();
	write_special_chars_mode = 0;
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
		debug_write_cpu_byte(addr_loword, data_byte);
		return;
	}
	const Uint32 addr = ((Uint32)addr_hiword << 16) + (Uint32)addr_loword;
	debug_write_linear_byte(addr, data_byte);
}


static void cmd_show ( char *p )
{
	if (mem_args(p, 0) != 1)
		return;
	if (addr_hiword == 0xFFFF) {
		MATRIX("[cpu:%04X] = %02X", addr_loword, debug_read_cpu_byte(addr_loword));
		return;
	}
	const Uint32 addr = ((Uint32)addr_hiword << 16) + (Uint32)addr_loword;
	MATRIX("[%03X:%04X] = %02X", addr_hiword, addr_loword, debug_read_linear_byte(addr));
}


static void dump_mem_lines ( int lines, const char sepchr )
{
	char chardump[79];
	memset(chardump, 0, sizeof chardump);
	while (lines-- > 0) {
		if (addr_hiword != 0xFFFF) {
			// Clamp to 28 bit address space, unless hiword is $FFFF meaning CPU view, thus having a special meaning
			addr_hiword &= 0xFFF;
			sprintf(chardump + 1, "%03X%c%04X", addr_hiword, sepchr, addr_loword);
		} else
			sprintf(chardump + 1, "cpu%c%04X", sepchr, addr_loword);
		for (int i = 0; i < 16; i++) {
			Uint8 data;
			if (addr_hiword == 0xFFFF)
				data = debug_read_cpu_byte(addr_loword++);
			else {
				data = debug_read_linear_byte(((Uint32)addr_hiword << 16) + (Uint32)addr_loword);
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
		chardump[0] = 0xFF;	// marker for "live update" stuff (rendered as space anyway!)
		chardump[sizeof(chardump) - 2] = '\n';
		matrix_write_string(chardump);
	}
}


static Uint8 d_bytes[10];


static Uint8 reader_for_disasm_phys ( const unsigned int addr, const unsigned int ofs )
{
	const Uint8 b = debug_read_linear_byte((addr + ofs) & 0xFFFFFFU);
	d_bytes[ofs] = b;
	return b;
}


static Uint8 reader_for_disasm_cpu ( const unsigned int addr, const unsigned int ofs )
{
	const Uint8 b = debug_read_cpu_byte((addr + ofs) & 0xFFFFU);
	d_bytes[ofs] = b;
	return b;
}


static void cmd_asm ( char *p )
{
	if (*p && mem_args(p, 0) != 1)
		return;
	const char *opname;
	char arg[64];
	for (unsigned int l = 0; l < 16; l++) {
		unsigned int len;
		if (addr_hiword == 0xFFFF) {
			len = cpu65_disasm(reader_for_disasm_cpu, addr_loword, 0xFFFFU, &opname, arg);
			MATRIX(" cpu:%04X  ", addr_loword);
		} else {
			len = cpu65_disasm(reader_for_disasm_phys, (addr_hiword << 16) + addr_loword, 0xFFFFFFU, &opname, arg);
			MATRIX(" %03X:%04X  ", addr_hiword, addr_loword);
		}
		for (unsigned int a = 0; a < 5; a++)
			if (a < len)
				MATRIX("%02X ", d_bytes[a]);
			else
				MATRIX("   ");
		MATRIX("%s%s %s\n", opname, strlen(opname) != 4 ? " " : "", arg);
		addr_loword += len;
	}
}


static void cmd_dump ( char *p )
{
	if (*p && mem_args(p, 0) != 1)
		return;
	dump_mem_lines(16, ':');
}


static void live_dump_update ( void )
{
	const Uint16 backup_hiword = addr_hiword;
	const Uint16 backup_loword = addr_loword;
	char t[9];
	for (int y = CLI_START_LINE; y < chrscreen_ysize; y++) {
		const Uint8 *s = vmem + (y * chrscreen_xsize * 2);
		if (*s == 0xFF) {
			for (unsigned int i = 0; i < sizeof(t); i++, s += 2)
				t[i] = (char)s[2];
			t[sizeof(t) - 1] = 0;
			t[3] = 0;	// separator between "bank" and "offset", we have "t" as bank now, and "t+4" as the offset
			int hi, lo;
			if (!strcmp(t, "cpu"))
				hi = 0xFFFF;
			else if (sscanf(t, "%X", &hi) != 1)
				continue;
			if (sscanf(t + 4, "%X", &lo) != 1)
				continue;
			// Note: we call this function from the main matrix updater, which stores/restores X/Y screen position,
			// so it's OK to modify that here!
			current_x = 0;
			current_y = y;
			// And for these: backed up/restored by us, in this very function
			addr_hiword = hi;
			addr_loword = lo;
			//DEBUGPRINT("Found update in row %d, text is \"%s\" bank = %X offset = %X" NL, y, t, hi, lo);
			dump_mem_lines(1, blink_phase ? ':' : ' ');	// blinking ':' to indicate live update
		}
	}
	addr_hiword = backup_hiword;
	addr_loword = backup_loword;
	if (live_update_enabled < 0 && blink_phase)
		live_update_enabled = 0;
}


static void cmd_live ( char *p )
{
	live_update_enabled = (live_update_enabled <= 0) ? 1 : -1;
	MATRIX("Live memory dump updates are turned %s\n", (live_update_enabled > 0) ? "ON" : "OFF");
}


static void cmd_reset ( char *p )
{
	reset_mega65();
}


static void cmd_shade ( char *p )
{
	unsigned int shade;
	if (sscanf(p, "%u", &shade) == 1 && shade <= 100) {
		console_colours[3] = shade * 255 / 100;
		need_update = 1 | 2;
		init_colour_mappings();
	} else
		MATRIX("?BAD VALUE");
}


static void cmd_help ( char *p );


static const struct command_tab_st {
	const char *cmdname;
	void (*cb)(char*);
	const char *shortnames;
} command_tab[] = {
	{ "dump",	cmd_dump,	"d"	},
	{ "asm",	cmd_asm,	"a"	},
	{ "exit",	cmd_off,	"x"	},
	{ "help",	cmd_help,	"h?"	},
	{ "live",	cmd_live,	NULL	},
	{ "log",	cmd_log,	NULL	},
	{ "reg",	cmd_reg,	"r"	},
	{ "reset",	cmd_reset,	NULL	},
	{ "show",	cmd_show,	"s"	},
	{ "uname",	cmd_uname,	NULL	},
	{ "ver",	cmd_ver,	NULL	},
	{ "write",	cmd_write,	"w"	},
	{ "map",	cmd_map,	"m"	},
	{ "shade",	cmd_shade,	NULL	},
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
	static int history_browse_current = 0;
	if (!current_x) {
		matrix_write_string(PROMPT);
		start_x = current_x;
		if (!c)
			return;
	}
	if (c == 3) {		// "previous viewport" key
		set_viewport(1);
		return;
	}
	set_viewport(0);	// any keystorkes causes to go back to the current viewport
	if (c == 4)		// "current viewport" key
		return;
	if (c == 1 || c == 2) {	// 1=up arrow (older entry), 2=down arrow (newer entry)
		const int dir = (c == 1) ? 1 : -1;
		if (
			(dir == -1 && history_browse_current > 0                    && history[history_browse_current - 1]) ||
			(dir ==  1 && history_browse_current < CMD_HISTORY_SIZE - 1 && history[history_browse_current + 1])
		) {
			cmdbuf[current_x - start_x] = 0;
			if (!history_browse_current) {
				free(history[0]);
				history[0] = xemu_strdup(cmdbuf);
			}
			history_browse_current += dir;
			strcpy(cmdbuf, history[history_browse_current]);
			current_x = start_x;
			for (int i = 0; i < chrscreen_xsize - start_x - 1; i++)
				matrix_write_char(i < strlen(cmdbuf) ? cmdbuf[i] : ' ');
			current_x = start_x + strlen(cmdbuf);
		}
		return;
	}
	if (current_x < chrscreen_xsize - 1 && (c >= 33 || (c == 32 && current_x > start_x && cmdbuf[current_x - start_x - 1] != 32))) {
		cmdbuf[current_x - start_x] = c;
		matrix_write_char(c);
		cmdbuf[current_x - start_x] = 0;
		//DEBUGMATRIX("MATRIX: input change: \"%s\"" NL, cmdbuf);
	} else if (current_x > start_x && c == 8) {
		static const char backspace_seq_str[] = { 8, 32, 8, 0 };
		matrix_write_string(backspace_seq_str);
		cmdbuf[current_x - start_x] = 0;
		//DEBUGPRINT("MATRIX: input change: \"%s\"" NL, cmdbuf);
	} else if (c == 13) {
		matrix_write_char('\n');
		if (cmdbuf[0]) {
			if (cmdbuf[strlen(cmdbuf) - 1] == 32)
				cmdbuf[strlen(cmdbuf) - 1] = 0;
			free(history[CMD_HISTORY_SIZE - 1]);	// oldest entry will be dropped: if it's NULL, no problem, free() can accept NULL ptr
			free(history[0]);
			history[0] = xemu_strdup(cmdbuf);
			memmove(history + 1, history, (CMD_HISTORY_SIZE - 1) * sizeof(char*));
			history[0] = xemu_strdup("");
			history_browse_current = 0;
			//DEBUGMATRIX("MATRIX: executing: \"%s\"" NL, cmdbuf);
			execute(cmdbuf);
			cmdbuf[0] = 0;
		}
		if (current_x)
			matrix_write_char('\n');
	}
}




static void matrix_updater_callback ( void )
{
	if (!in_the_matrix)
		return;		// should not happen, but ... (and can be a race condition with toggle function anyway!)
	blink_phase = (matrix_counter & 8);
	write_char_raw(current_x, current_y, ' ', current_colour);	// delete cursor
	const int saved_x = current_x, saved_y = current_y;
	current_x = 0;
	current_y = 1;
	write_special_chars_mode = 2;
	static const char rotator[4] = { '-', '\\', '|', '/' };
	dump_regs(rotator[(matrix_counter >> 3) & 3]);
	// make sure the rest of the line is cleared (the regs line length can vary in size!), also position for the next line for the map dump
	while (current_x)
		matrix_write_char(' ');
	dump_map();
	current_colour = NORMAL_COLOUR;
	write_special_chars_mode = 0;
	if (live_update_enabled)
		live_dump_update();
	current_x = saved_x;
	current_y = saved_y;
	if (queued_input) {
		DEBUGMATRIX("MATRIX-INPUT: [%c] (%d)" NL, queued_input >= 32 ? queued_input : ' ', queued_input);
		input(queued_input);
		queued_input = 0;
	} else if (current_x == 0)
		input(0);	// just to have some prompt by default
	if (blink_phase && !current_viewport)
		write_char_raw(current_x, current_y, CURSOR_CHAR, CURSOR_COLOUR);	// show cursor
	matrix_counter++;
	matrix_update();
}

// TODO+FIXME: though the theory to hijack keyboard input looks nice, we have some problems:
// * hotkeys does not work anymore
// * if in mouse grab mode, you don't even have your mouse to be able to close window, AND/OR use the menu system


// Async stuff as callbacks. Only updates "queued_input"

static int kbd_cb_keyevent ( SDL_KeyboardEvent *ev )
{
	if (ev->state == SDL_PRESSED) {
		int sym;
		switch (ev->keysym.sym) {
			case SDLK_UP:
				sym = 1; break;
			case SDLK_DOWN:
				sym = 2; break;
			case SDLK_PAGEUP:
				sym = 3; break;
			case SDLK_PAGEDOWN:
				sym = 4; break;
			case SDLK_RETURN:
			case SDLK_KP_ENTER:
				sym = 13; break;
			case SDLK_BACKSPACE:
			case SDLK_DELETE:
			case SDLK_KP_BACKSPACE:
				sym = 8; break;
			default:
				sym = ev->keysym.sym; break;
		}
		if (sym > 0 && sym < 32 && !queued_input) {
			// Monitor ctrl-tab, as the main handler is defunct at this point, we "hijacked" it!
			// So we must give up the matrix mode themselves here
			if (sym == SDLK_TAB && (ev->keysym.mod & (KMOD_LCTRL | KMOD_RCTRL)))
				matrix_mode_toggle(0);
			else
				queued_input = sym;
		}
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
			init_colour_mappings();
			chrscreen_xsize = backend_xsize / 8;
			chrscreen_ysize = backend_ysize / 8;
			vmem = xemu_malloc(FULL_SCREEN_BYTE_SIZE);
			vmem_prev = xemu_malloc(CLI_AREA_BYTE_SIZE);
			vmem_backup = xemu_malloc(CLI_AREA_BYTE_SIZE);
			cmdbuf = xemu_malloc(chrscreen_xsize + 1);	// we don't want commands longer than a line!
			cmdbuf[0] = 0;
			for (int i = 0; i < CMD_HISTORY_SIZE; i++)
				history[i] = NULL;
			current_colour = NORMAL_COLOUR;
			matrix_clear();
			memcpy(vmem_prev, vmem + CLI_AREA_BYTE_OFFSET, CLI_AREA_BYTE_SIZE);
			current_colour = BANNER_COLOUR;
			static const char banner_msg[] = "*** Xemu's pre-matrix ... press left-CTRL + TAB to exit / re-enter ***";
			current_x = (chrscreen_xsize - strlen(banner_msg)) >> 1;
			matrix_write_string(banner_msg);
			current_colour = NORMAL_COLOUR;
			current_y = CLI_START_LINE;
			current_x = 0;
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
		set_viewport(0);		// switch back to viewport 0, to avoid confusion after enabling matrix again and cannot see the cursor/prompt to type
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
