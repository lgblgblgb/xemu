/* Xep128: Minimalistic Enterprise-128 emulator with focus on "exotic" hardware
   Copyright (C)2015,2016 LGB (Gábor Lénárt) <lgblgblgb@gmail.com>
   http://xep128.lgb.hu/

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
#include "dave.h"
#include "primoemu.h"
#include "cpu.h"
#include "printer.h"

#include <SDL.h>


Uint8 dave_int_read;
Uint8 kbd_matrix[16]; // the "real" EP kbd matrix only uses the first 10 bytes though
static Uint8 dave_int_write;
static int cnt_1hz, cnt_50hz, cnt_31khz, cnt_1khz, cnt_tg0, cnt_tg1, cnt_tg2;
static int cnt_load_tg0, cnt_load_tg1, cnt_load_tg2;
static int tg0_ff, tg1_ff, tg2_ff;
int kbd_selector;
int cpu_cycles_per_dave_tick;
int mem_wait_states;


int audio_source = AUDIO_SOURCE_DAVE;


static SDL_AudioDeviceID audio = 0;
static SDL_AudioSpec audio_spec;
#define AUDIO_BUFFER_SIZE 0x4000
static Uint8 audio_buffer[AUDIO_BUFFER_SIZE];
static Uint8 *audio_buffer_r = audio_buffer;
static Uint8 *audio_buffer_w = audio_buffer;
static int dave_ticks_per_sample_counter = 0;
static int dave_ticks_per_sample = 6;



static inline Uint16 dave_render_audio_sample ( void )
{
	int left, right;
	/* TODO: missing noise channel, polynom counters/ops, modulation, etc */
	if (ports[0xA7] &  8)
		left  = ports[0xA8] << 2;		// left ch is in D/A mode
	else {						// left ch is in "normal" mode
		//tg0_ff = tg1_ff = tg2_ff = 1;
		left  = tg0_ff * ports[0xA8] +
			tg1_ff * ports[0xA9] +
			tg2_ff * ports[0xAA];
	}
	if (ports[0xA7] & 16)
		right = ports[0xAC] << 2;		// right ch is in D/A mode
	else {						// right ch is in "normal" mode
		//tg0_ff = tg1_ff = tg2_ff = 1;
		right = tg0_ff * ports[0xAC] +
			tg1_ff * ports[0xAD] +
			tg2_ff * ports[0xAE];
	}
#if 0
	DEBUGPRINT("DAVE: TG: %d %d %d Vol-l=%02X,%02X,%02X,%02X Vol-r=%02X,%02X,%02X,%02X FREQ=%d,%d,%d CNT=%d,%d,%d SYNC=%d" NL, tg0_ff, tg1_ff, tg2_ff,
		ports[0xA8], ports[0xA9], ports[0xAA], ports[0xAB],
		ports[0xAC], ports[0xAD], ports[0xAE], ports[0xAF],
		ports[0xA0] | ((ports[0xA1] & 15) << 8),
		ports[0xA2] | ((ports[0xA3] & 15) << 8),
		ports[0xA4] | ((ports[0xA5] & 15) << 8),
		cnt_tg0, cnt_tg1, cnt_tg2,
		ports[0xA7] & 7
	);
#endif
	return (right << 8) | left;
}



static void audio_fill_stereo ( Uint16 stereo_sample )
{
	*(Uint16*)(audio_buffer_w) = stereo_sample;
	audio_buffer_w += 2;
	if (audio_buffer_w == audio_buffer + AUDIO_BUFFER_SIZE)
		audio_buffer_w = audio_buffer;
}



static void audio_callback(void *userdata, Uint8 *stream, int len)
{
	while (len--) {
		if (audio_buffer_r == audio_buffer_w) {
			//*(stream++) = 0;
			*(stream++) = 0;
		} else {
			//*(stream++) = *(audio_buffer_r++);
			*(stream++) = *(audio_buffer_r++);
			if (audio_buffer_r == audio_buffer + AUDIO_BUFFER_SIZE)
				audio_buffer_r = audio_buffer;
		}
	}
}



void audio_start ( void )
{
	if (audio)
		SDL_PauseAudioDevice(audio, 0);
}



void audio_stop ( void )
{
	if (audio)
		SDL_PauseAudioDevice(audio, 1);
}



void audio_close ( void )
{
	audio_stop();
	if (audio)
		SDL_CloseAudioDevice(audio);
	audio = 0;
}



void audio_init ( int enable )
{
	SDL_AudioSpec want;
	if (!enable) return;
	SDL_memset(&want, 0, sizeof(want));
	want.freq = 41666;
	want.format = AUDIO_U8;
	want.channels = 2;
	want.samples = 4096;
	want.callback = audio_callback;
	want.userdata = NULL;
	audio = SDL_OpenAudioDevice(NULL, 0, &want, &audio_spec, 0);
	if (!audio)
		ERROR_WINDOW("Cannot initiailze audio: %s\n", SDL_GetError());
	else if (want.freq != audio_spec.freq || want.format != audio_spec.format || want.channels != audio_spec.channels) {
		audio_close();
		ERROR_WINDOW("Bad audio parameters (w/h freq=%d/%d, fmt=%d/%d, chans=%d/%d, smpls=%d/%d, cannot use sound",
			want.freq, audio_spec.freq, want.format, audio_spec.format, want.channels, audio_spec.channels, want.samples, audio_spec.samples
		);
	} else
		audio_stop();	// still stopped ... must be audio_start()'ed by the caller
}



void dave_set_clock ( void )
{
	// FIXME maybe: Currently, it's assumed that Dave and CPU has fixed relation about clock
	//double turbo_rate = (double)CPU_CLOCK / (double)DEFAULT_CPU_CLOCK;
	if (ports[0xBF] & 2) {
		cpu_cycles_per_dave_tick = 24; // 12MHz (??)
		dave_ticks_per_sample = 4;
	} else {
		cpu_cycles_per_dave_tick = 16; // 8MHz  (??)
		dave_ticks_per_sample = 6;
	}
	//DEBUG("DAVE: CLOCK: assumming %dMHz input, CPU clock divisor is %d, CPU cycles per Dave tick is %d" NL, (ports[0xBF] & 2) ? 12 : 8, CPU_CLOCK / cpu_cycles_per_dave_tick, cpu_cycles_per_dave_tick);
	mem_wait_states = (CPU_CLOCK > 4000000) ? 2 : 1; // memory wait states (non-VRAM only!) asked by BF port is 1, but 2 for "turbo" Z80 solutions
}



void kbd_matrix_reset ( void )
{
	memset(kbd_matrix, 0xFF, sizeof(kbd_matrix));
}



void dave_reset ( void )
{
	int a;
	//kbd_matrix_reset();
	for (a = 0xA0; a <= 0xBF; a++)
		z80ex_pwrite_cb(a, 0);
	dave_int_read = 0;
	dave_int_write = 0;
	kbd_selector = -1;
	cnt_1hz = 0; cnt_50hz = 0; cnt_31khz = 0; cnt_1khz = 0; cnt_tg0 = 0; cnt_tg1 = 0; cnt_tg2 = 0;
	tg0_ff = 0; tg1_ff = 0; tg2_ff = 0;
	//mem_ws_all = 0;
	//mem_ws_m1  = 0;
	//NICK_SLOTS_PER_DAVE_TICK_HI = NICK_SLOTS_PER_SEC / 250000.0;
	//_set_timing();
	DEBUG("DAVE: reset" NL);
}



/* Called by Nick */
void dave_int1(int level)
{
	if (level) {
		dave_int_read |= 16; // set level
	} else {
		// the truth is here, if previous level was set (falling edge), and int1 is enabled, then set latch!
		if ((dave_int_read & 16) && (dave_int_write & 16)) {
			DEBUG("DAVE/VINT: LACTH is set!" NL);
			dave_int_read |= 32; // set latch
			if (primo_on)
				nmi_pending = primo_nmi_enabled;
		}
		dave_int_read &= 239; // reset level
	}
}



static inline void dave_int_tg ( void )
{
	if (dave_int_write & 1)
		dave_int_read |= 2;	// set latch if TG int is enabled
	dave_int_read ^= 1;		// negate level
}



void dave_tick ( void )
{
	// TODO 31.25KHz counter for some reason :) what I forgot :-P
	/* 50Hz counter */
	if ((--cnt_50hz) < 0) {
		cnt_50hz = 5000 - 1;
		if ((ports[0xA7] & 96) == 32)
			dave_int_tg();
	}
	/* 1KHz counter */
	if ((--cnt_1khz) < 0) {
		cnt_1khz = 250 - 1;
		if ((ports[0xA7] & 96) ==  0)
			dave_int_tg();
	}
	/* counter for tone channel #0 */
	if (ports[0xA7] & 1) { // sync mode?
		cnt_tg0 = cnt_load_tg0;
		tg0_ff = 0;
	} else if ((--cnt_tg0) < 0) {
		cnt_tg0 = cnt_load_tg0;
		tg0_ff ^= 1;
		if ((ports[0xA7] & 96) == 64)
			dave_int_tg();
	}
	/* counter for tone channel #1 */
	if (ports[0xA7] & 2) { // sync mode?
		cnt_tg1 = cnt_load_tg1;
		tg1_ff = 0;
	} else if ((--cnt_tg1) < 0) {
		cnt_tg1 = cnt_load_tg1;
		tg1_ff ^= 1;
		if ((ports[0xA7] & 96) == 96)
			dave_int_tg();
	}
	/* counter for tone channel #2 */
	if (ports[0xA7] & 4) { // sync mode?
		cnt_tg2 = cnt_load_tg2;
		tg2_ff = 0;
	} else if ((--cnt_tg2) < 0) {
		cnt_tg2 = cnt_load_tg2;
		tg2_ff ^= 1;
	}
	/* handling the 1Hz interrupt */
	if ((--cnt_1hz) < 0) {
		cnt_1hz = 250000 - 1;
		if (dave_int_write & 4)
			dave_int_read |= 8; // set latch, if 1Hz int source is enabled
		dave_int_read ^= 4; // negate 1Hz interrupt level bit (actually the freq is 0.5Hz, but int is generated on each edge, thus 1Hz)
		//DEBUG("DAVE: 1HZ interrupt level: %d" NL, dave_int_read & 4);
	}
	// SOUND
	if (audio) {
		if ((--dave_ticks_per_sample_counter) < 0) {
			switch (audio_source) {
				case AUDIO_SOURCE_DAVE:
					audio_fill_stereo(dave_render_audio_sample());
					break;
				case AUDIO_SOURCE_PRINTER_COVOX:
					audio_fill_stereo((printer_data_byte << 8) | printer_data_byte);	// both stereo channels has the same byte ...
					break;
				case AUDIO_SOURCE_DTM_DAC4:
					audio_fill_stereo(
						((ports[0xF0] + ports[0xF1]) >> 1) |	// left
						(((ports[0xF2] + ports[0xF3]) >> 1) << 8)	// right
					);
					break;
				default:
					FATAL("Audio source renderer %d is not known!", audio_source);
					break;
			}
			dave_ticks_per_sample_counter = dave_ticks_per_sample - 1;
		}
	}
}



void dave_configure_interrupts ( Uint8 n )
{
	dave_int_write = n;
	dave_int_read &= (0x55 | ((~n) & 0xAA)); // this "nice" stuff resets desired latches
	dave_int_read &= (0x55 | ((n << 1) & 0xAA)); // TODO / FIXME: not sure if it is needed!
}



void dave_write_audio_register ( Uint8 port, Uint8 value )
{
	printer_disable_covox();        // disable COVOX mode, if any
	audio_source = AUDIO_SOURCE_DAVE;
	switch (port) {
		case 0xA0:
			cnt_load_tg0 = (cnt_load_tg0 & 0xF00 ) | value;
			break;
		case 0xA1:
			cnt_load_tg0 = (cnt_load_tg0 & 0x0FF ) | ((value & 0xF) << 8);
			break;
		case 0xA2:
			cnt_load_tg1 = (cnt_load_tg1 & 0xF00 ) | value;
			break;
		case 0xA3:
			cnt_load_tg1 = (cnt_load_tg1 & 0x0FF ) | ((value & 0xF) << 8);
			break;
		case 0xA4:
			cnt_load_tg2 = (cnt_load_tg2 & 0xF00 ) | value;
			break;
		case 0xA5:
			cnt_load_tg2 = (cnt_load_tg2 & 0x0FF ) | ((value & 0xF) << 8);
			break;
		case 0xA8:
		case 0xA9:
		case 0xAA:
		case 0xAB:
		case 0xAC:
		case 0xAD:
		case 0xAE:
		case 0xAF:
			ports[port] &= 63;	// so we don't need to do this AND again and again ...
			break;
	}
}
