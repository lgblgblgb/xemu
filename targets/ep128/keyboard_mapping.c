/* Minimalistic Enterprise-128 emulator with focus on "exotic" hardware
   Part of the Xemu project, please visit: https://github.com/lgblgblgb/xemu
   Copyright (C)2015-2016,2020 LGB (Gábor Lénárt) <lgblgblgb@gmail.com>

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

#include "xep128.h"
#include "keyboard_mapping.h"


/* _default_ scancode mapping. This is used to initialize configuration,
   but config file, command line options can override this. So, this table is
   _not_ used directly by the emulator! */


struct keyMappingDefault_st {
	const SDL_Scancode	code;
	const int		pos;
	const char		*description;
};

static const char *unknown_key_name = "SomeUnknownKey";
static struct keyMappingTable_st *keyMappingTable = NULL;
static int keyMappingTableSize = 0;

static const struct keyMappingDefault_st keyMappingDefaults[] = {
	{ SDL_SCANCODE_1,		0x31, "1"	},
	{ SDL_SCANCODE_2,		0x36, "2"	},
	{ SDL_SCANCODE_3,		0x35, "3"	},
	{ SDL_SCANCODE_4,		0x33, "4"	},
	{ SDL_SCANCODE_5,		0x34, "5"	},
	{ SDL_SCANCODE_6,		0x32, "6"	},
	{ SDL_SCANCODE_7,		0x30, "7"	},
	{ SDL_SCANCODE_8,		0x50, "8"	},
	{ SDL_SCANCODE_9,		0x52, "9"	},
	{ SDL_SCANCODE_0,		0x54, "0"	},
	{ SDL_SCANCODE_Q,		0x21, "Q"	},
	{ SDL_SCANCODE_W,		0x26, "W"	},
	{ SDL_SCANCODE_E,		0x25, "E"	},
	{ SDL_SCANCODE_R,		0x23, "R"	},
	{ SDL_SCANCODE_T,		0x24, "T"	},
	{ SDL_SCANCODE_Y,		0x22, "Y"	},
	{ SDL_SCANCODE_U,		0x20, "U"	},
	{ SDL_SCANCODE_I,		0x90, "I"	},
	{ SDL_SCANCODE_O,		0x92, "O"	},
	{ SDL_SCANCODE_P,		0x94, "P"	},
	{ SDL_SCANCODE_A,		0x16, "A"	},
	{ SDL_SCANCODE_S,		0x15, "S"	},
	{ SDL_SCANCODE_D,		0x13, "D"	},
	{ SDL_SCANCODE_F,		0x14, "F"	},
	{ SDL_SCANCODE_G,		0x12, "G"	},
	{ SDL_SCANCODE_H,		0x10, "H"	},
	{ SDL_SCANCODE_J,		0x60, "J"	},
	{ SDL_SCANCODE_K,		0x62, "K"	},
	{ SDL_SCANCODE_L,		0x64, "L"	},
	{ SDL_SCANCODE_RETURN,		0x76, "ENTER"	},
	{ SDL_SCANCODE_LSHIFT,		0x07, "L-SHIFT"	},
	{ SDL_SCANCODE_RSHIFT,		0x85, "R-SHIFT" },
	{ SDL_SCANCODE_CAPSLOCK,	0x11, "CAPS"	},
	{ SDL_SCANCODE_Z,		0x06, "Z"	},
	{ SDL_SCANCODE_X,		0x05, "X"	},
	{ SDL_SCANCODE_C,		0x03, "C"	},
	{ SDL_SCANCODE_V,		0x04, "V"	},
	{ SDL_SCANCODE_B,		0x02, "B"	},
	{ SDL_SCANCODE_N,		0x00, "N"	},
	{ SDL_SCANCODE_M,		0x80, "M"	},
	{ SDL_SCANCODE_LCTRL,		0x17, "CTRL" 	},
	{ SDL_SCANCODE_SPACE,		0x86, "SPACE"	},
	{ SDL_SCANCODE_SEMICOLON,	0x63, ";"	},
	{ SDL_SCANCODE_LEFTBRACKET,	0x95, "["	},
	{ SDL_SCANCODE_RIGHTBRACKET,	0x66, "]"	},
	{ SDL_SCANCODE_APOSTROPHE,	0x65, ":"	},	// for EP : we map PC '
	{ SDL_SCANCODE_MINUS,		0x53, "-"	},
	{ SDL_SCANCODE_BACKSLASH,	0x01, "\\"	},
	{ SDL_SCANCODE_TAB,		0x27, "TAB"	},
	{ SDL_SCANCODE_ESCAPE,		0x37, "ESC"	},
	{ SDL_SCANCODE_INSERT,		0x87, "INS"	},
	{ SDL_SCANCODE_BACKSPACE,	0x56, "ERASE"	},
	{ SDL_SCANCODE_DELETE,		0x81, "DEL"	},
	{ SDL_SCANCODE_LEFT,		0x75, "LEFT"	},
	{ SDL_SCANCODE_RIGHT,		0x72, "RIGHT"	},
	{ SDL_SCANCODE_UP,		0x73, "UP"	},
	{ SDL_SCANCODE_DOWN,		0x71, "DOWN"	},
	{ SDL_SCANCODE_SLASH,		0x83, "/"	},
	{ SDL_SCANCODE_PERIOD,		0x84, "."	},
	{ SDL_SCANCODE_COMMA,		0x82, ","	},
	{ SDL_SCANCODE_EQUALS,		0x93, "@"	},	// for EP @ we map PC =
	{ SDL_SCANCODE_F1,		0x47, "F1"	},
	{ SDL_SCANCODE_F2,		0x46, "F2"	},
	{ SDL_SCANCODE_F3,		0x42, "F3"	},
	{ SDL_SCANCODE_F4,		0x40, "F4"	},
	{ SDL_SCANCODE_F5,		0x44, "F5"	},
	{ SDL_SCANCODE_F6,		0x43, "F6"	},
	{ SDL_SCANCODE_F7,		0x45, "F7"	},
	{ SDL_SCANCODE_F8,		0x41, "F8"	},
//	{ SDL_SCANCODE_F9,		0x77, "F9"	},
	{ SDL_SCANCODE_HOME,		0x74, "HOLD"	},	// for EP HOLD we map PC HOME
	{ SDL_SCANCODE_END,		0x70, "STOP"	},	// for EP STOP we map PC END
	/* ---- Not real EP kbd matrix, used for extjoy emulation with numeric keypad ---- */
	{ SDL_SCANCODE_KP_5,		0xA0, "ExtJoy FIRE"	},	// for EP external joy FIRE  we map PC num keypad 5
	{ SDL_SCANCODE_KP_8,		0xA1, "ExtJoy UP"	},	// for EP external joy UP    we map PC num keypad 8
	{ SDL_SCANCODE_KP_2,		0xA2, "ExtJoy DOWN"	},	// for EP external joy DOWN  we map PC num keypad 2
	{ SDL_SCANCODE_KP_4,		0xA3, "ExtJoy LEFT"	},	// for EP external joy LEFT  we map PC num keypad 4
	{ SDL_SCANCODE_KP_6,		0xA4, "ExtJoy RIGHT"	},	// for EP external joy RIGHT we map PC num keypad 6
	/* ---- emu related "SYS" keys (like screenshot, exit, fullscreen ...) position codes are the identifier for the caller! Must be values, not used otherwise by the emulated computer! */
	{ SDL_SCANCODE_F11,		0xFF, "EMU fullscreen"	},	// ... on EP the lower nibble is the mask shift, so values X8-XF are not used!
	{ SDL_SCANCODE_F9,		0xFE, "EMU exit"	},
	{ SDL_SCANCODE_F10,		0xFD, "EMU screenshot"	},
	{ SDL_SCANCODE_PAUSE,		0xFC, "EMU reset"	},
	{ SDL_SCANCODE_PAGEDOWN,	0xFB, "EMU slower-cpu"	},
	{ SDL_SCANCODE_PAGEUP,		0xFA, "EMU faster-cpu"	},
	{ SDL_SCANCODE_GRAVE,		0xF9, "EMU osd-replay"	},
	{ SDL_SCANCODE_KP_MINUS,	0xF8, "EMU console"	},
	/* ---- end of table marker, must be the last entry ---- */
	{ 0, -1, NULL }
};




static void keymap_set_key ( SDL_Scancode code, int posep )
{
	int n = keyMappingTableSize;
	struct keyMappingTable_st *p = keyMappingTable;
	const struct keyMappingDefault_st *q = keyMappingDefaults;
	while (n && p->code != code) {
		n--;
		p++;
	}
	if (!n) {
		keyMappingTable = realloc(keyMappingTable, (keyMappingTableSize + 1) * sizeof(struct keyMappingTable_st));
		CHECK_MALLOC(keyMappingTable);
		p = keyMappingTable + (keyMappingTableSize++);
		p->code = code;
	}
	p->posep = posep;
	/* search for description */
	while (q->description) {
		if (q->pos == posep) {
			p->description = q->description;
			return;
		}
		q++;
	}
	p->description = unknown_key_name;
}



int keymap_set_key_by_name ( const char *name, int posep )
{
	SDL_Scancode code = SDL_GetScancodeFromName(name);
	if (code == SDL_SCANCODE_UNKNOWN)
		return 1;
	keymap_set_key(code, posep);
	return 0;
}



void keymap_preinit_config_internal ( void )
{
	const struct keyMappingDefault_st *p = keyMappingDefaults;
	while (p->pos != -1) {
		keymap_set_key(p->code, p->pos);
		p++;
	}
}



void keymap_dump_config ( FILE *fp )
{
	int n = keyMappingTableSize;
	struct keyMappingTable_st *p = keyMappingTable;
	fprintf(fp,
		"# Note: key names are SDL scan codes! Sometimes it's nothing to do with the letters" NL
		"# on your keyboard (eg some national layout, like Hungarian etc) but the \"physical\"" NL
		"# scan code assignment, eg the right neighbour of key \"L\" is \";\" even if your layout" NL
		"# means something different there!" NL NL
	);
	while (n--) {
		const char *name = SDL_GetScancodeName(p->code);
		if (!name) {
			fprintf(fp, "# WARNING: cannot get SDL key name for epkey@%02x with SDL scan code of %d!" NL, p->posep, p->code);
		} else {
			fprintf(fp, "epkey@%02x = %s\t# %s" NL, p->posep, name, p->description);
		}
		p++;
	}
}



const struct keyMappingTable_st *keymap_resolve_event ( SDL_Keysym sym )
{
	int n = keyMappingTableSize;
	struct keyMappingTable_st *p = keyMappingTable;
	DEBUG("KBD: SEARCH: scan=%d sym=%d (map size=%d)" NL, sym.scancode, sym.sym, n);
	while (n--) {
		if (p->code == sym.scancode) {
			DEBUG("KBD: FOUND: key position %02Xh (%s)" NL, p->posep, p->description);
			return p;
		}
		p++;
	}
	DEBUG("KBD: FOUND: none." NL);
	return NULL;
}

