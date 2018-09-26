/* Test-case for a very simple Primo (a Hungarian U880 - Z80
   compatible clone CPU - based 8 bit computer) emulator.
   Part of the Xemu project, please visit: https://github.com/lgblgblgb/xemu
   Copyright (C)2016-2018 LGB (Gábor Lénárt) <lgblgblgb@gmail.com>

   NOTE: Primo's CPU is U880, but for the simplicity I still call it Z80, as
   it's a clone anyway.

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
#include "xemu/z80.h"
#include "primo.h"


Z80EX_CONTEXT z80ex;

#define ROM_Z80_PC_LOAD_TRIGGER	0x3994

static Uint8 memory[0x10000];
static const Uint8 init_primo_palette_rgb[2 * 3] = {	// Primo is simple enough, b&w :) Primo C/colour Primo is another problem ...
	0x00, 0x00, 0x00,	// black
	0xFF, 0xFF, 0xFF	// white
};
static Uint32 primo_palette[2];
static int primo_screen = 0xC800;
static int nmi_enabled = 0;
static int nmi_status = 0;

static const char *pri_name;

static int cpu_clocks_per_scanline;
static int cpu_clocks_per_audio_sample;
static int cpu_clock;
static int scanline;
static Uint64 all_cycles_spent;

static int emu_loop_notification;

#define EMU_LOOP_NMI_NOTIFY	1
#define	EMU_LOOP_LOAD_NOTIFY	2
#define EMU_LOOP_UPDATE_NOTIFY	4

#define VBLANK_ON	32
#define VBLANK_OFF	0
#define VBLANK_START_SCANLINE	(312 - 9)
#define PAL_LINE_FREQ		15625

static int vblank = VBLANK_OFF;

#define JOY_CLOCKING_TIMEOUT_MICROSECS 272

static struct {
	int	step;
	int	clock;
	Uint64	last_clocked;
	Uint64	clocking_timeout;
	int	instance;
} joy;

#define AUDIO_SAMPLING_FREQ	(PAL_LINE_FREQ * 2)
#define AUDIO_PULSE_SAMPLES_MAX_PASS	(AUDIO_SAMPLING_FREQ / 32)

static int beeper;
static Uint64 beeper_last_changed;
static SDL_AudioDeviceID audio;



static int primo_read_joy ( int on, int off )
{
	switch (joy.step) {
		case 0:
			return hid_read_joystick_left(on, off);
		case 1:
			return hid_read_joystick_down(on, off);
		case 2:
			return hid_read_joystick_right(on, off);
		case 3:
			return hid_read_joystick_up(on, off);
		case 4:
			return hid_read_joystick_button(on, off);
		default:
			return off;
	}
}


Z80EX_BYTE z80ex_mread_cb ( Z80EX_WORD addr, int m1_state )
{
	return memory[addr];
}


void z80ex_mwrite_cb ( Z80EX_WORD addr, Z80EX_BYTE value )
{
	if (XEMU_LIKELY(addr >= 0x4000))
		memory[addr] = value;
}


Z80EX_BYTE z80ex_pread_cb ( Z80EX_WORD port16 )
{
	if ((port16 & 0xFF) < 0x40) {
		// See the comments at the "matrix" definition below
		return (((~kbd_matrix[(port16 >> 3) & 7]) >> (port16 & 7)) & 1) | vblank;
	} else if ((port16 & 0xFF) < 0x80) {
		DEBUG("JOY: read state at step=%d" NL, joy.step);
		return primo_read_joy(0, 1);
	} else
		return 0xFF;
}


// Call this, if "enable NMI" or VBLANK signals change!
static void manage_nmi ( void )
{
	// NMI signal on the CPU in the Primo is created from VBLANK, and a gate signal to allow to mask the unmaskable :) interrupt (NMI)
	// VBLANK is a looong signal, the reason that we can use this way, that Z80 has an edge triggered NMI input, not level (unlike INT)
	int new_status = nmi_enabled && vblank;
	if (new_status && !nmi_status) {	// rising edge (note: in the emulator we only use high-active signal, which can be different than the real hw)
		emu_loop_notification |= EMU_LOOP_NMI_NOTIFY;
		DEBUG("NMI: triggering edge" NL);
	}
	nmi_status = new_status;
}




void z80ex_pwrite_cb ( Z80EX_WORD port16, Z80EX_BYTE value )
{
	if ((port16 & 0xFF) < 0x40) {
		primo_screen = (value & 8) ? 0xE800 : 0xC800;
		nmi_enabled = value & 128;
		manage_nmi();
		if ((value & 16) != beeper) {
			static int rounding_error = 0;
			Uint64 no_of_samples = (rounding_error + all_cycles_spent - beeper_last_changed) / cpu_clocks_per_audio_sample;
			rounding_error       = (rounding_error + all_cycles_spent - beeper_last_changed) % cpu_clocks_per_audio_sample;
			if (no_of_samples <= AUDIO_PULSE_SAMPLES_MAX_PASS && no_of_samples > 0 && audio) {
				Uint8 samples[no_of_samples];
				memset(samples, value & 16 ? 0xFF : 0x00, no_of_samples);
				int ret = SDL_QueueAudio(audio, samples, no_of_samples);	// last param are (number of) BYTES not samles. But we use 1 byte/samle audio ...
				if (ret)
					DEBUGPRINT("AUDIO: DATA: ERROR: %s" NL, SDL_GetError());
				else
					DEBUG("AUDIO: DATA: queued" NL);
			} else
				DEBUG("AUDIO: DATA: rejected! samples=%d" NL, (int)no_of_samples);
			beeper = value & 16;
			beeper_last_changed = all_cycles_spent;
		}
		if ((value & 64) != joy.clock) {
			joy.clock = value & 64;
			if (joy.clock) {
				joy.step = (all_cycles_spent - joy.last_clocked >= joy.clocking_timeout) ? 0 : (joy.step + 1) & 7;
				joy.last_clocked = all_cycles_spent;
				if (joy.step > 4)	// FIXME: should we do this??? or real HW would always depends on timed reset of counter?
					joy.step = 0;
			}
		}
	}
}


Z80EX_BYTE z80ex_intread_cb ( void )
{
	return 0xFF;
}


void z80ex_reti_cb ( void )
{
}



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
*/
static const struct KeyMapping primo_key_map[] = {
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


static inline void render_primo_screen ( void )
{
	int tail, y;
	Uint32 *pix = xemu_start_pixel_buffer_access(&tail);
	Uint8 *scr = memory + primo_screen;
	for (y = 0; y < 192; y++) {
		int x;
		for (x = 0; x < 32; x++) {
			Uint8 b = *(scr++);
			*(pix++) = primo_palette[(b >> 7) & 1];
			*(pix++) = primo_palette[(b >> 6) & 1];
			*(pix++) = primo_palette[(b >> 5) & 1];
			*(pix++) = primo_palette[(b >> 4) & 1];
			*(pix++) = primo_palette[(b >> 3) & 1];
			*(pix++) = primo_palette[(b >> 2) & 1];
			*(pix++) = primo_palette[(b >> 1) & 1];
			*(pix++) = primo_palette[b & 1];
		}
		pix += tail;
	}
	xemu_update_screen();
}



// HID needs this to be defined, it's up to the emulator if it uses or not ...
int emu_callback_key ( int pos, SDL_Scancode key, int pressed, int handled )
{
        return 0;
}



static void update_emulator ( void )
{
	static int frameskip = 0;
	if (!frameskip) {
		render_primo_screen();
		hid_handle_all_sdl_events();
		xemu_timekeeping_delay(40000);
	}
	frameskip = !frameskip;
}


static int pri_apply ( Uint8 *data, int size, int wet_run )
{
	int i = 0;
	while (i < size) {
		int btype = data[i];
		DEBUGPRINT("PRI: block_type=$%02X @ %d" NL, btype, i);
		if (btype == 0xC9)
			return 0;	// return, OK, no Z80 PC specified
		if (i >= size - 2)
			break;
		int baddr = data[i + 1] | (data[i + 2] << 8);
		if (btype == 0xC3)
			return baddr;	// return OK, Z80 PC specified!
		if (i >= size - 4)
			break;
		int bsize = data[i + 3] | (data[i + 4] << 8);
		if (btype == 0xD1) {
			baddr += memory[0x40A4] | (memory[0x40A5] << 8);
			if (wet_run) {
				memory[0x40F9] = (baddr + bsize + 1) &  0xFF;
				memory[0x40FA] = (baddr + bsize + 1) >> 8;
			}
		} else if (btype != 0xD5 && btype != 0xD9) {
			ERROR_WINDOW("Invalid PRI file, unknown block type $%02X @ %d", btype, i);
			return -1;
		}
		if (i >= size - bsize)
			break;
		DEBUGPRINT("  ... ADDR=%04X SIZE=%04X" NL, baddr, bsize);
		if (wet_run)
			memcpy(memory + baddr, data + i + 5, bsize);
		i += 5 + bsize;
	}
	ERROR_WINDOW("Invalid PRI file, probably truncated");
	return -1;
}



static int pri_load ( const char *file_name )
{
	if (!file_name || !*file_name)
		return -1;
	int file_size = xemu_load_file(file_name, NULL, 10, 0xC000, "Cannot load PRI file");
	if (file_size < 0)
		return file_size;
	int ret = pri_apply(xemu_load_buffer_p, file_size, 1);
	free(xemu_load_buffer_p);
	return ret;
}



static int set_cpu_hz ( int hz )
{
	if (hz < 1000000)
		hz = 1000000;
	else if (hz > 8000000)
		hz = 8000000;
	cpu_clocks_per_scanline = (hz / PAL_LINE_FREQ) & ~1;	// 15625 Hz = 312.5 * 50, PAL "scanline frequency", how many Z80 cycles we need for that. Also make it to an even number
	cpu_clock = cpu_clocks_per_scanline * PAL_LINE_FREQ;	// to reflect the possible situation when it's not a precise divider above
	DEBUGPRINT("CLOCK: CPU: clock speed set to %.2f MHz (%d CPU cycles per scanline)" NL, cpu_clock / 1000000.0, cpu_clocks_per_scanline);
	joy.clocking_timeout = (int)(((double)JOY_CLOCKING_TIMEOUT_MICROSECS / 1000000.0) * (double)cpu_clock);
	DEBUGPRINT("CLOCK: JOY: clocking timeout is %d microseconds, %d CPU cycles" NL, JOY_CLOCKING_TIMEOUT_MICROSECS, (int)joy.clocking_timeout);
	cpu_clocks_per_audio_sample = cpu_clock / AUDIO_SAMPLING_FREQ;
	DEBUGPRINT("CLOCK: AUDIO: %d CPU clocks per audio sample at sampling frequency of %d Hz" NL, cpu_clocks_per_audio_sample, AUDIO_SAMPLING_FREQ);
	return cpu_clock;
}


static int set_cpu_clock_from_string ( const char *s )
{
	char *end;
	double result = strtod(s, &end);
	//DEBUGPRINT("RESULT=%f end=%p" NL, result, end);
	if (result < 1.0 || result > 8.0 || !end || *end) {
		ERROR_WINDOW("Cannot interpret the -clock option you specified. Defaulting to %.2f MHz", DEFAULT_CPU_CLOCK / 1000000.0);
		return set_cpu_hz(DEFAULT_CPU_CLOCK);
	} else
		return set_cpu_hz(result * 1000000);
}





static int emulation_loop ( int cycles )
{
	for (;;) { // our emulation loop ...
		int op_cycles = z80ex_step();
		cycles -= op_cycles;
		all_cycles_spent += op_cycles;
		if (XEMU_UNLIKELY(emu_loop_notification)) {
			DEBUG("EMU: notification" NL);
			// DESIGN: for fast emulation loop, we have a "notification" system. In general,
			// there are no notifications. So this way we have to check only a single variable,
			// and do more "if"-s only if we have something. So the usual scenario (not having
			// notification) would require to check a single condition -> faster emulation. Still,
			// we can expand the range of notifications without affecting the emulation speed
			// this way, if there are no notifications, which should be the majority of the emulator
			// time. NOTE: debugger support/etc, should use notification system as well!
			if (emu_loop_notification & EMU_LOOP_NMI_NOTIFY) {	// Notification for triggering NMI
				// check if Z80ex accepted NMI. Though a real Z80 should always do this (NMI is not maskable),
				// Z80ex is constructed in a way, that it actually *returns* after z80ex_step() even if it was
				// a prefix byte, like FD,DD,CB,ED ... Surely, Z80 would not allow this, that's why z80ex_nmi()
				// may report zero, that is NMI *now* cannot be accepted. Then we simply try again on the next
				// run. Note: the construction of this z80ex_step() function actually GOOD because of more fine
				// grained timing, so it's not only "a problem"! Actually it's IMPOSSIBLE to write an accurate
				// Z80 emulation without this behaviour! Since, a real Z80 would react for a loong opocde
				// sequence of FD/DD bytes to block even NMIs till its end, which would block the execution
				// of the emulator updates etc as well, probably.
				op_cycles = z80ex_nmi();
				if (op_cycles) {
					DEBUG("NMI accepted" NL);
					cycles -= op_cycles;
					all_cycles_spent += op_cycles;
					emu_loop_notification &= ~EMU_LOOP_NMI_NOTIFY;	// clear notification, it's done
				} else {
					DEBUG("NMI not accepted" NL);
				}
			}
			if (emu_loop_notification & EMU_LOOP_LOAD_NOTIFY) {	// Notification for checking PC to trigger PRI loading!
				if (Z80_PC == ROM_Z80_PC_LOAD_TRIGGER) {
					emu_loop_notification &= ~EMU_LOOP_LOAD_NOTIFY;	// clear notification, it's done
					int ret = pri_load(pri_name);
					if (ret > 0) {
						DEBUGPRINT("PRI: loaded, Z80 PC change $%04X -> $%04X" NL, Z80_PC, ret);
						Z80_PC = ret;
					} else if (!ret)
						DEBUGPRINT("PRI: loaded, no Z80 PC change requested" NL);
					else
						DEBUGPRINT("PRI: cannot load program" NL);
				}
			}
			if (emu_loop_notification & EMU_LOOP_UPDATE_NOTIFY) {	// update notification for the emulator core
				emu_loop_notification &= ~EMU_LOOP_UPDATE_NOTIFY;	// clear notification, it's done
				break;	// end of the emulation loop!
			}
		}
		if (XEMU_UNLIKELY(cycles <= 0)) {
			// We run at least enough CPU cycles for a scanline, by decrementing cycles with the number
			// of cycles Z80 spent to execute code at each step. The value "cycles" must be intialized
			// with the desired number of CPU cycles for a scanline, which is calculated by the set_cpu_hz()
			// function to configure CPU clock speed.
			if (scanline == 310) {
				cycles += cpu_clocks_per_scanline / 2;	// last scanline is only a "half" according to the PAL standard. Or such ...
				scanline++;
			} else if (scanline == 311) {	// we were at the last scanline of a half-frame.
				cycles += cpu_clocks_per_scanline;
				scanline = 0;
				vblank = VBLANK_OFF;
				manage_nmi();
				//DEBUGPRINT("VIDEO: new frame!" NL);
				break;	// end of the emulation loop!
			} else {
				cycles += cpu_clocks_per_scanline;
				scanline++;
				if (scanline == VBLANK_START_SCANLINE) {
					vblank = VBLANK_ON;
					manage_nmi();
				}
			}
			if (XEMU_UNLIKELY(cycles <= 0)) {
				FATAL("Emulation problem, underflow of cpu cycles / scanline tracking counter!");
			}
		}
	}
	return cycles;
}






int main ( int argc, char **argv )
{
	xemu_pre_init(APP_ORG, TARGET_NAME, "The Unknown Primo emulator from LGB");
	xemucfg_define_switch_option("fullscreen", "Start in fullscreen mode");
	xemucfg_define_str_option("clock", "2.5", "Selects CPU frequency (1.00-8.00 in MHz)");
	xemucfg_define_str_option("rom", DEFAULT_ROM_FILE_PATH, "Select ROM to use");
	xemucfg_define_str_option("pri", NULL, "Loads a PRI file");
	xemucfg_define_switch_option("syscon", "Keep system console open (Windows-specific effect only)");
	if (xemucfg_parse_all(argc, argv))
		return 1;
	/* Initiailize SDL - note, it must be before loading ROMs, as it depends on path info from SDL! */
	if (xemu_post_init(
		TARGET_DESC APP_DESC_APPEND,	// window title
		1,				// resizable window
		SCREEN_WIDTH, SCREEN_HEIGHT,	// texture sizes
		SCREEN_WIDTH, SCREEN_HEIGHT,	// logical size (width is doubled for somewhat correct aspect ratio)
		SCREEN_WIDTH * 3, SCREEN_HEIGHT * 3,	// window size (tripled in size, original would be too small)
		SCREEN_FORMAT,		// pixel format
		2,			// we have 2 colours
		init_primo_palette_rgb,	// initialize palette from this constant array
		primo_palette,		// initialize palette into this stuff
		RENDER_SCALE_QUALITY,	// render scaling quality
		USE_LOCKED_TEXTURE,	// 1 = locked texture access
		NULL			// no emulator specific shutdown function
	))
		return 1;
	hid_init(
		primo_key_map,
		VIRTUAL_SHIFT_POS,
		SDL_ENABLE		// enable joystick HID events
	);
	SDL_AudioSpec audio_want, audio_have;
	SDL_memset(&audio_want, 0, sizeof audio_want);
	audio_want.freq = AUDIO_SAMPLING_FREQ;
	audio_want.format = AUDIO_U8;
	audio_want.channels = 1;
	audio_want.samples = 4096;
	audio_want.callback = NULL;
	audio = SDL_OpenAudioDevice(NULL, 0, &audio_want, &audio_have, 0);
	DEBUGPRINT("AUDIO: dev=#%d driver=\"%s\" sampling at %d Hz" NL, audio, SDL_GetCurrentAudioDriver(), audio_have.freq);
	/* Intialize memory and load ROMs */
	memset(memory, 0xFF, sizeof memory);
	if (xemu_load_file(xemucfg_get_str("rom"), memory, 0x4000, 0x4000, "This is the selected primo ROM. Without it, Xemu won't work.\nInstall it, or use -rom CLI switch to specify another path.") < 0)
		return 1;
	// Continue with initializing ...
	clear_emu_events();	// also resets the keyboard
	z80ex_init();
	beeper = 0;
	beeper_last_changed = 0;
	joy.step = 0;
	joy.clock = 0;
	joy.last_clocked = 0;
	//set_cpu_hz(DEFAULT_CPU_CLOCK);
	set_cpu_clock_from_string(xemucfg_get_str("clock"));
	pri_name = xemucfg_get_str("pri");
	scanline = 0;
	xemu_set_full_screen(xemucfg_get_bool("fullscreen"));
	if (!xemucfg_get_bool("syscon"))
		sysconsole_close(NULL);
	int cycles = cpu_clocks_per_scanline;
	emu_loop_notification = 0;
	all_cycles_spent = 0;
	if (pri_name)
		emu_loop_notification |= EMU_LOOP_LOAD_NOTIFY;
	SDL_PauseAudioDevice(audio, 0);
	xemu_timekeeping_start();	// we must call this once, right before the start of the emulation
	for (;;) {
		cycles = emulation_loop(cycles);
		update_emulator();
	}
	return 0;
}
