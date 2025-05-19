/* Minimalistic Enterprise-128 emulator with focus on "exotic" hardware
   Part of the Xemu project, please visit: https://github.com/lgblgblgb/xemu
   Copyright (C)2015-2016,2020,2025 LGB (Gábor Lénárt) <lgblgblgb@gmail.com>

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
#include "xemu/emutools_hid.h"
#include "enterprise128.h"
#include "dave.h"
#include "ui.h"

#include "input_devices.h"

static int move_dx, move_dy, nibble_counter;
int mouse_grab = 0;
static int wheel_dx, wheel_dy;
static Uint8 nibble;
static int mouse_button1, mouse_button2;
static int rts_old_level = -1;
static int mouse_pulse = 0;
//static int _mouse_wait_warn = 1;
extern Uint32 raster_time; // we use Nick's raster_time "timing source" because it's kinda free without introducing another timer source
static Uint32 watchdog_mouse;		// watchdog value (raster_time related) used by the mouse nibble-reset protocol
static Uint32 watchdog_xep128;		// watchdog value (raster_time related) used by Xep128 to decide between joy-1 and j-column mouse

int show_keys = 0;
static int control_port_emu_mode = -1;

const struct KeyMappingDefault ep128_key_map[] = {
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
//	{ SDL_SCANCODE_F11,		0xFF, "EMU fullscreen"	},	// ... on EP the lower nibble is the mask shift, so values X8-XF are not used!
//	{ SDL_SCANCODE_F9,		0xFE, "EMU exit"	},
//	{ SDL_SCANCODE_F10,		0xFD, "EMU screenshot"	},
//	{ SDL_SCANCODE_PAUSE,		0xFC, "EMU reset"	},
//	{ SDL_SCANCODE_PAGEDOWN,	0xFB, "EMU slower-cpu"	},
//	{ SDL_SCANCODE_PAGEUP,		0xFA, "EMU faster-cpu"	},
//	{ SDL_SCANCODE_GRAVE,		0xF9, "EMU osd-replay"	},
//	{ SDL_SCANCODE_KP_MINUS,	0xF8, "EMU console"	},
	/* ---- */
	STD_XEMU_SPECIAL_KEYS,
	/* ---- end of table marker, must be the last entry ---- */
	{ 0, -1, NULL }
};


#ifdef XEMU_OSK_SUPPORT
const struct osk_desc_st osk_desc[] = {
	{0, 5,17,"F1",   0x47},
	{0, 0,17,"F2",   0x46},
	{0, 0,17,"F3",   0x42},
	{0, 0,17,"F4",   0x40},
	{0, 0,17,"F5",   0x44},
	{0, 0,17,"F6",   0x43},
	{0, 0,17,"F7",   0x45},
	{0, 0,17,"F8",   0x41},
	{0, 4,10,"HOLD", 0x74},
	{0, 0,17,"STOP", 0x70},


	{OSK_DESC_NEW_LINE, 2,10,"ESC", 0x37},
	{0, 0,10,"1\n!",   0x31},
	{0, 0,10,"2\n\"",  0x36},
	{0, 0,10,"3\nPnd", 0x35},
	{0, 0,10,"4\n$",   0x33},
	{0, 0,10,"5\n%",   0x34},
	{0, 0,10,"6\n&",   0x32},
	{0, 0,10,"7\n'",   0x30},
	{0, 0,10,"8\n(",   0x50},
	{0, 0,10,"9\n)",   0x52},
	{0, 0,10,"0\n_",   0x54},
	{0, 0,10,"-\n=",   0x53},
	{0, 0,10,"^\n~",   -1  },	// TODO
	{0, 0,15,"ERASE",  0x56},
	{0, 0,15,"DEL",    0x81},
	{0, 0,10,"INS",    0x87},


	{OSK_DESC_NEW_LINE, 2,15,"TAB", 0x27},
	{0, 0,10,"Q", 0x21},
	{0, 0,10,"W", 0x26},
	{0, 0,10,"E", 0x25},
	{0, 0,10,"R", 0x23},
	{0, 0,10,"T", 0x24},
	{0, 0,10,"Y", 0x22},
	{0, 0,10,"U", 0x20},
	{0, 0,10,"I", 0x90},
	{0, 0,10,"O", 0x92},
	{0, 0,10,"P", 0x94},
	{0, 0,10,"@\n`", 0x93},
	{0, 0,10,"[\n{", 0x95},
	{0, 0,15,"ENTER", 0x76},


	{OSK_DESC_NEW_LINE | OSK_DESC_TOGGLE, 0,10,"CTRL", 0x17},
	{0, 0,10,"Lck", -1},	// TODO
	{0, 0,10,"A", 0x16},
	{0, 0,10,"S", 0x15},
	{0, 0,10,"D", 0x13},
	{0, 0,10,"F", 0x14},
	{0, 0,10,"G", 0x12},
	{0, 0,10,"H", 0x10},
	{0, 0,10,"J", 0x60},
	{0, 0,10,"K", 0x62},
	{0, 0,10,"L", 0x64},
	{0, 0,10,";\n+", 0x63},
	{0, 0,10,":\n*", 0x65},
	{0, 0,10,"]\n}", 0x66},
	{0, 17,10,"UP", 0x73},

	{OSK_DESC_NEW_LINE | OSK_DESC_TOGGLE | OSK_DESC_MOD_KEY, 0,15,"Shift", 0x07},
	{0, 0,10,"\\\n|", 0x04},
	{0, 0,10,"Z", 0x06},
	{0, 0,10,"X", 0x05},
	{0, 0,10,"C", 0x03},
	{0, 0,10,"V", 0x04},
	{0, 0,10,"B", 0x02},
	{0, 0,10,"N", 0x00},
	{0, 0,10,"M", 0x80},
	{0, 0,10,",\n<", 0x82},
	{0, 0,10,".\n>", 0x84},
	{0, 0,10,"/\n?", 0x83},
	{OSK_DESC_TOGGLE | OSK_DESC_MOD_KEY, 0,15,"Shift", 0x85},
	{0, 12,10,"LEFT", 0x75},
	{0, 0,10,"RIGHT", 0x72},


	{OSK_DESC_NEW_LINE, 38, 80, "SPACE", 0x86},
	{OSK_DESC_TOGGLE, 12,10,"ALT", -1}, 	// TODO
	{0, 17,10,"DOWN", 0x71},

	{.key_str = NULL}
};
#endif


/* The mouse buffer. nibble_counter shows which nibble is to read (thus "nibble_counter >> 1" is the byte pointer actually.
   mouse_protocol_nibbles limits the max nibbles to read, ie it's 4 (= 2 bytes) for boxsoft protocol for the default setting */
static Uint8 mouse_buffer[] = {
	0x00,	// BOXSOFT: delta X as signed, positive-left, updated from mouse_dx
	0x00,	// BOXSOFT: delta Y as signed, positive-up, updates from mouse_dy
	0x10,	// EXTENDED MSX: proto ID + extra buttons on lower nibble (I update those - lower nibble - on mouse SDL events)
	0x00,	// EXTENDED MSX: horizontal wheel (it's splitted for horizontal/Z, but according to entermice it's handled as a 8 bit singed now for a single wheel, so I do this as well)
	0x44,	// ENTERMICE: extra bytes to read (incl this: 4 now) + PS/2 Mouse ID [it's 4 now ...]
	0x14,	// ENTERMICE: hardware version major.minor
	0x19,	// ENTERMICE: firmware version major.minor
	0x5D	// ENTERMICE: Device ID, should be 5D for Entermice
};

#define WATCHDOG_USEC(n) (n / 64)

/* Values can be used in mouse modes, buttons[] array to map PC mouse buttons to EP related mouse buttons
   The first two are mapped then according to the button*_mask of the mode struct.
   The EX buttons instructs setting the button status in mouse buffer directly at byte 3, lower nibble
*/
#define BUTTON_MAIN	1
#define BUTTON_OTHER	2
#define BUTTON_EX3	3
#define BUTTON_EX4	4
#define BUTTON_EX5	5

/* Values can be used in *_mask options in mouse modes struct */
#define J_COLUMN	1
#define K_COLUMN	2

struct mouse_modes_st {
	const char *name;	// some name for the given mouse protocol/mode ...
	int buttons[5];		// button map: indexed by emulator given PC left/middle/right/x1/x2 (in this order), with values of the BUTTON_* macros above to map to protcol values
	int nibbles;		// nibbles in the protocol (before the constant zero answer or warping around if warp is non-zero)
	int wrap;		// wraps around nibble counter automatically or not (not = constant zero nibble answer, watchdog can only reset the counter)
	int watchdog;		// watchdog time-out, or -1, for not using watchdog, times are Xep128 specific use the WATCHDOG_USEC macro to convert from usec!
	int data_mask;		// bit mask for mouse data read (J/K/L = 1 / 2 / 4), you can use *_COLUMN macros
};

/* Definition of mouse modes follows :) */
static const struct mouse_modes_st mouse_modes[] = {
	{	// MODE - 0: NOT USED POSITION
	}, {	// MODE - 1:
		.name =		"BoxSoft J-col",
		.buttons =	{ BUTTON_MAIN, BUTTON_EX3, BUTTON_OTHER, BUTTON_EX4, BUTTON_EX5 },	// mapping for SDL left/middle/right/X1/X2 events to EP
		.nibbles =	4,
		.wrap =		0,
		.watchdog =	WATCHDOG_USEC(1500),
		.data_mask =	J_COLUMN
	}, {	// MODE - 2:
		.name =		"ExtMSX J-col",
		.buttons =	{ BUTTON_MAIN, BUTTON_EX3, BUTTON_OTHER, BUTTON_EX4, BUTTON_EX5 },	// mapping for SDL left/middle/right/X1/X2 events to EP
		.nibbles =	8,
		.wrap =		0,
		.watchdog =	WATCHDOG_USEC(1500),
		.data_mask =	J_COLUMN
	}, {	// MODE - 3:
		.name =		"EnterMice J-col",
		.buttons =	{ BUTTON_MAIN, BUTTON_EX3, BUTTON_OTHER, BUTTON_EX4, BUTTON_EX5 },	// mapping for SDL left/middle/right/X1/X2 events to EP
		.nibbles =	16,
		.wrap =		0,
		.watchdog =	WATCHDOG_USEC(1500),
		.data_mask =	J_COLUMN
	}, {	// MODE - 4:
		.name =		"BoxSoft K-col",
		.buttons =	{ BUTTON_MAIN, BUTTON_EX3, BUTTON_OTHER, BUTTON_EX4, BUTTON_EX5 },	// mapping for SDL left/middle/right/X1/X2 events to EP
		.nibbles =	4,
		.wrap =		0,
		.watchdog =	WATCHDOG_USEC(1500),
		.data_mask =	K_COLUMN
	}, {	// MODE - 5:
		.name =		"ExtMSX K-col",
		.buttons =	{ BUTTON_MAIN, BUTTON_EX3, BUTTON_OTHER, BUTTON_EX4, BUTTON_EX5 },	// mapping for SDL left/middle/right/X1/X2 events to EP
		.nibbles =	8,
		.wrap =		0,
		.watchdog =	WATCHDOG_USEC(1500),
		.data_mask =	K_COLUMN
	}, {	// MODE - 6:
		.name =		"EnterMice K-col",
		.buttons =	{ BUTTON_MAIN, BUTTON_EX3, BUTTON_OTHER, BUTTON_EX4, BUTTON_EX5 },	// mapping for SDL left/middle/right/X1/X2 events to EP
		.nibbles =	16,
		.wrap =		0,
		.watchdog =	WATCHDOG_USEC(1500),
		.data_mask =	K_COLUMN
	}
};

#define LAST_MOUSE_MODE ((sizeof(mouse_modes) / sizeof(const struct mouse_modes_st)) - 1)

static const struct mouse_modes_st *mode;	// current mode mode, pointer to the selected mouse_modes
int mouse_mode;					// current mode, with an integer

#define JOYSTICK_SCAN(num, dir) joystick_scan(num, dir)



int mouse_mode_description ( int cfg, char *buffer )
{
	if (cfg == 0)
		cfg = mouse_mode;
	if (cfg < 1 || cfg > LAST_MOUSE_MODE) {
		sprintf(buffer, "#%d *** Invalid mouse mode ***", cfg);
		return 1;
	} else {
		sprintf(
			buffer,
			"#%d (%s) nibbles=%d wrap=%d watchdog=%d mask=%d",
			cfg,
			mouse_modes[cfg].name,
			mouse_modes[cfg].nibbles,
			mouse_modes[cfg].wrap,
			mouse_modes[cfg].watchdog,
			mouse_modes[cfg].data_mask
		);
		return 0;
	}
}



void mouse_reset_button ( void )
{
	mouse_button1 = 0;
	mouse_button2 = 0;
	mouse_buffer[2] &= 0xF0;	// extra buttons for MSX extended protocol should be cleared as well
}



static inline void set_button ( Uint8 *storage, int mask, int pressed )
{
	if (pressed)
		*storage |= mask;
	else
		*storage &= 255 - mask;
}



void emu_mouse_button ( Uint8 sdl_button, int press )
{
	const char *name;
	int id;
	switch (sdl_button) {
		case SDL_BUTTON_LEFT:
			name = "left";
			id = 0;
			break;
		case SDL_BUTTON_MIDDLE:
			name = "middle";
			id = 1;
			break;
		case SDL_BUTTON_RIGHT:
			name = "right";
			id = 2;
			break;
		case SDL_BUTTON_X1:
			name = "x1";
			id = 3;
			break;
		case SDL_BUTTON_X2:
			name = "x2";
			id = 4;
			break;
		default:
			name = "UNKNOWN";
			id = -1;
			break;
	}
	DEBUG("MOUSE: BUTTON: event: SDL#%d XEP#%d (%s) %s" NL, sdl_button, id, name, press ? "pressed" : "released");
	if (id == -1) {
		DEBUG("MOUSE: BUTTON: unknown button on SDL level (see previous MOUSE: line)!!" NL);
		return;	// unknown mouse button??
	}
	if (sdl_button == SDL_BUTTON_LEFT && press && mouse_grab == 0) {
		//emu_osd_msg("Mouse grab. Press ESC to exit.");
		set_mouse_grab(SDL_TRUE, 0);
		mouse_grab = 1;
		mouse_reset_button();
	}
	if (!mouse_grab) {
		DEBUG("MOUSE: BUTTON: not in grab mode, do not forward event" NL);
		return; // not in mouse grab mode
	}
	switch (mode->buttons[id]) {
		case BUTTON_MAIN:
			mouse_button1 = press;
			break;
		case BUTTON_OTHER:
			mouse_button2 = press;
			break;
		case BUTTON_EX3:
			set_button(&mouse_buffer[2], 1, press);
			break;
		case BUTTON_EX4:
			set_button(&mouse_buffer[2], 2, press);
			break;
		case BUTTON_EX5:
			set_button(&mouse_buffer[2], 4, press);
			break;
		default:
			DEBUG("MOUSE: used mouse button cannot be mapped for the given mouse mode (map=%d), ignored" NL, mode->buttons[id]);
			break;
	}
}



void emu_mouse_motion ( int dx, int dy )
{
	DEBUG("MOUSE: MOTION: event: dx = %d, dy = %d" NL, dx, dy);
	if (!mouse_grab) {
		DEBUG("MOUSE: MOTION: not in grab mode, do not forward event" NL);
		return; // not in mouse grab mode
	}
	move_dx -= dx;
	if (move_dx > 127) move_dx = 127;
	else if (move_dx < -128) move_dx = -128;
	move_dy -= dy;
	if (move_dy > 127) move_dy = 127;
	else if (move_dy < -128) move_dy = -128;
}



void emu_mouse_wheel ( int x, int y, int flipped )
{
	DEBUG("MOUSE: WHEEL: event: x = %d, y = %d, flipped = %d" NL, x, y, flipped);
	if (!mouse_grab) {
		DEBUG("MOUSE: WHEEL: not in grab mode, do not forward event" NL);
		return; // not in mouse grab mode
	}
	flipped = flipped ? -1 : 1;
	wheel_dx -= x * flipped;
	if (wheel_dx > 127) wheel_dx = 127;
	else if (wheel_dx < -128) wheel_dx = -128;
	wheel_dy -= y * flipped;
	if (wheel_dy > 127) wheel_dy = 127;
	else if (wheel_dy < -128) wheel_dy = -128;
}



void mouse_reset ( void )
{
	// mouse_grab = 0; // commented out to fix the issue: emu reset hotkey is pressed with grabbed mouse ...
	nibble_counter = 0;
	//if (rts_old_level == -1)
	rts_old_level = 0;
	nibble = 0;
	move_dx = 0;
	move_dy = 0;
	wheel_dx = 0;
	wheel_dy = 0;
	watchdog_mouse = raster_time;
	watchdog_xep128 = raster_time;
	mouse_reset_button();
	mouse_buffer[0] = mouse_buffer[1] = mouse_buffer[3] = 0;
}



static inline void check_mouse_watchdog ( void )
{
	int time = raster_time - watchdog_mouse;
	watchdog_mouse = raster_time;
	if (mode->watchdog >= 0 && (time > mode->watchdog || time < 0))	// negative case in case of raster_time counter warp-around (integer overflow)
		nibble_counter = 0;	// in case of timeout, nibble counter resets to zero
}

// FIXME: port the old one here, it's for only able to compile :-/
int joystick_scan ( int num, int dir )
{
	if (num != 0 && num != 1)
		return 0;
	return !(kbd_matrix[10] & (1 << dir));
}


// Called from cpu.c in case of read port 0xB6, this function MUST only give back bits 0-2, ie control ports ones, higher bits
// bits should be zero as they are used for other purposes (ie: tape, printer ...) and OR'ed by cpu.c at port reading func ...
Uint8 read_control_port_bits ( void )
{
	int mouse_ok, joy1_ok;
	int time = raster_time - watchdog_xep128;
	// Note: unlike watchdog_mouse, watchdog_xep128 is set in check data shift only but queried here
	if (time < 0)
		watchdog_xep128 = raster_time;	// integer overflow
	if (mode->data_mask == K_COLUMN) {		// mouse on K-column, joy-1 is always enabled
		mouse_ok = 2;
		joy1_ok = 1;
	} else if (mouse_pulse && (time < WATCHDOG_USEC(100000) || time < 0)) {	// mouse on J-column, joy-1 is disabled on mouse usage
		mouse_ok = 2;
		joy1_ok = 0;
	} else {					// mouse on J-column, joy-1 is enabled because no mouse usage
		mouse_ok = 0;
		joy1_ok = 1;
		mouse_pulse = 0;
	}
	if (control_port_emu_mode != mouse_ok + joy1_ok) {
		static const char *m[] = { "joystick", "Mouse", "dual (K-col)" };
		control_port_emu_mode = mouse_ok + joy1_ok;
		OSD(-1, -1, "Control port: %s mode", m[control_port_emu_mode - 1 ]);
	}
	switch (kbd_selector) {
		/* joystick-1 or mouse related */
		case  0: // The Entermice wiki mentioned priority of mouse buttons are not so much implemented ... TODO
			return
				(mouse_ok ? ((mouse_button1 ? 0 : mode->data_mask) | (7 - mode->data_mask - 4) | (mouse_button2 ? 0 : 4)) : 7) &
				(joy1_ok ? ((JOYSTICK_SCAN(0, JOY_SCAN_FIRE1) ? 0 : 1) | (JOYSTICK_SCAN(0, JOY_SCAN_FIRE2) ? 0 : 2) | (JOYSTICK_SCAN(0, JOY_SCAN_FIRE3) ? 0 : 4)) : 7);
		case  1: return (mouse_ok ? (((nibble & 1) ? mode->data_mask : 0) | (7 - mode->data_mask)) : 7) & ((joy1_ok && JOYSTICK_SCAN(0, JOY_SCAN_UP   )) ? 6 : 7);
		case  2: return (mouse_ok ? (((nibble & 2) ? mode->data_mask : 0) | (7 - mode->data_mask)) : 7) & ((joy1_ok && JOYSTICK_SCAN(0, JOY_SCAN_DOWN )) ? 6 : 7);
		case  3: return (mouse_ok ? (((nibble & 4) ? mode->data_mask : 0) | (7 - mode->data_mask)) : 7) & ((joy1_ok && JOYSTICK_SCAN(0, JOY_SCAN_LEFT )) ? 6 : 7);
		case  4: return (mouse_ok ? (((nibble & 8) ? mode->data_mask : 0) | (7 - mode->data_mask)) : 7) & ((joy1_ok && JOYSTICK_SCAN(0, JOY_SCAN_RIGHT)) ? 6 : 7);
		/* always joystick#2 on J-column (bit 0), other bits are spare */
		case  5: return (JOYSTICK_SCAN(1, JOY_SCAN_FIRE1) ? 0 : 1) | (JOYSTICK_SCAN(1, JOY_SCAN_FIRE2) ? 0 : 2) | (JOYSTICK_SCAN(1, JOY_SCAN_FIRE3) ? 0 : 4);
		case  6: return  JOYSTICK_SCAN(1, JOY_SCAN_UP   ) ? 6 : 7;
		case  7: return  JOYSTICK_SCAN(1, JOY_SCAN_DOWN ) ? 6 : 7;
		case  8: return  JOYSTICK_SCAN(1, JOY_SCAN_LEFT ) ? 6 : 7;
		case  9: return  JOYSTICK_SCAN(1, JOY_SCAN_RIGHT) ? 6 : 7;
		/* and if not ... */
		default: return 7;	// it shouldn't happen too much (only if no valid scan row is selected?)
	}
}



// Called from cpu.c in case of write of port 0xB7
void mouse_check_data_shift ( Uint8 val )
{
	if ((val & 2) == rts_old_level)		// check of change on the RTS signal change
		return;	// if no change, we're not interested in at all, mouse "nibble shifting" in RTS _edge_ triggered (both of the edges!) not level
	rts_old_level = val & 2;
	mouse_pulse = 1;			// this variable is only for the emulator to keep track of mouse reading tries and display OSD, etc
	watchdog_xep128 = raster_time;		// this watchdog is used to control auto-switch of Xep128 between J-column mouse and joy-1 emulation
	check_mouse_watchdog();			// this is the mouse watchdog to reset nibble counter in a given timeout
	if (nibble_counter >= mode->nibbles && mode->wrap)	// support nibble counter wrap-around mode, if the current mouse mode directs that
		nibble_counter = 0;
	// note:  information larger than one nibble shouldn't updated by the mouse SDL events directly in mouse_buffer, because it's possible
	// that between the reading of two nibbles that is modified. To avoid this, these things are updated here at a well defined counter state only:
	if (nibble_counter == 0) {
		// update mouse buffer byte 0 with delta X
		//mouse_buffer[0] = ((unsigned int)move_dx) & 0xFF;    // signed will be converted to unsigned
		mouse_buffer[0] = move_dx;
		move_dx = 0;
	} else if (nibble_counter == 2) {
		// update mouse buffer byte 1 with delta Y
		//mouse_buffer[1] = ((unsigned int)move_dy) & 0xFF;    // signed will be converted to unsigned
		mouse_buffer[1] = move_dy;
		move_dy = 0;
	} else if (nibble_counter == 6) {	// this may not be used at all, if mouse_protocol_nibbles limits the available nibbles to read, boxsoft will not read this ever!
		mouse_buffer[3] = wheel_dy;
		wheel_dy = 0;
	}
	if (nibble_counter < mode->nibbles) {
		// if nibble counter is below the constraint of the used mouse protocol, provide the upper or lower nibble of the buffer
		// based on the counter's lowest bit (ie, odd or even)
		nibble = ((nibble_counter & 1) ? (mouse_buffer[nibble_counter >> 1] & 15) : (mouse_buffer[nibble_counter >> 1] >> 4));
		nibble_counter++;
	} else
		nibble = 0;	// if we hit the max number of nibbles, we constantly provide zero as the nibble, until watchdog resets
}



int mouse_setup ( int cfg )
{
	char buffer[128];
	if (cfg < 0)
		return mouse_mode;
	if (cfg < 1 || cfg > LAST_MOUSE_MODE)
		cfg = 1;
	mouse_mode = cfg;
	mode = &mouse_modes[cfg];
	mouse_mode_description(cfg, buffer);
	DEBUG("MOUSE: SETUP: %s" NL, buffer);
	mouse_reset();
	return cfg;
}


/* ------------------- KEYBOARD ----------------------- */

// HID needs this to be defined, it's up to the emulator if it uses or not ...
int emu_callback_key ( int pos, SDL_Scancode key, int pressed, int handled )
{
	if (!pressed && pos == -2 && key == 0 && handled == SDL_BUTTON_RIGHT)
		ui_enter();
	return 0;
}
