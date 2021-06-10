/* A work-in-progess MEGA65 (Commodore 65 clone origins) emulator
   Part of the Xemu project, please visit: https://github.com/lgblgblgb/xemu
   Copyright (C)2016-2021 LGB (Gábor Lénárt) <lgblgblgb@gmail.com>

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

#define SID_USES_LOCK
#define OPL_USES_LOCK
#define NEED_SID_H

#include "xemu/emutools.h"
#include "xemu/opl3.h"
#include "audio65.h"
// For D7XX (audio DMA):
#include "io_mapper.h"
// For accessing memory (audio DMA):
#include "memory_mapper.h"
#include "configdb.h"

//#define DEBUG_AUDIO_LOCKS(...) DEBUGPRINT(__VA_ARGS__)
#define DEBUG_AUDIO_LOCKS(...)


static SDL_AudioDeviceID audio = 0;	// SDL audio device

int stereo_separation = AUDIO_DEFAULT_SEPARATION;
int audio_volume      = AUDIO_DEFAULT_VOLUME;

static int stereo_separation_orig = 100;
static int stereo_separation_other = 0;
struct SidEmulation sid[4];
static int system_sound_mix_freq;		// playback sample rate (in Hz) of the emulator itself
static int system_sid_cycles_per_sec;
static double dma_audio_mixing_value;
static opl3_chip opl3;
#ifdef OPL_USES_LOCK
static SDL_SpinLock opl3_lock;
#endif



void audio65_sid_write ( const int addr, const Uint8 data )
{
	// SIDs are separated by $20 bytes from each others, 4 SIDs (0-3)
	const int instance = (addr >> 5) & 3;
#ifdef SID_USES_LOCK
	DEBUG_AUDIO_LOCKS("WRITER: Waiting for SID#%d lock ... (%d)" NL, instance, sid[instance].spinlock);
	SDL_AtomicLock(&sid[instance].spinlock);
	DEBUG_AUDIO_LOCKS("WRITER: Got SID#%d lock (%d)" NL, instance, sid[instance].spinlock);
#endif
	sid_write_reg(&sid[instance], addr & 0x1F, data);
#ifdef SID_USES_LOCK
	SDL_AtomicUnlock(&sid[instance].spinlock);
	DEBUG_AUDIO_LOCKS("WRITER: Released SID#%d lock (%d)" NL, instance, sid[instance].spinlock);
#endif
}


void audio65_opl3_write ( Uint8 reg, Uint8 data )
{
	if (configdb.noopl3)
		return;
#ifdef OPL_USES_LOCK
	DEBUG_AUDIO_LOCKS("WRITER: Waiting for OPL3 lock ... (%d)" NL, opl3_lock);
	SDL_AtomicLock(&opl3_lock);
	DEBUG_AUDIO_LOCKS("WRITER: Got OPL3 lock (%d)" NL, opl3_lock);
#endif
	//OPL3_WriteReg(&opl3, reg, data);
	OPL3_WriteRegBuffered(&opl3, reg, data);
#ifdef OPL_USES_LOCK
	SDL_AtomicUnlock(&opl3_lock);
	DEBUG_AUDIO_LOCKS("WRITER: Released OPL3 lock (%d)" NL, opl3_lock);
#endif
}


void audio65_sid_inc_framecount ( void )
{
	for (int i = 0; i < 4; i++) {
#ifdef SID_USES_LOCK
		DEBUG_AUDIO_LOCKS("INCER: Waiting for SID#%d lock ... (%d)" NL, i, sid[i].spinlock);
		SDL_AtomicLock(&sid[i].spinlock);
		DEBUG_AUDIO_LOCKS("INCER: Got SID#%d lock (%d)" NL, i, sid[i].spinlock);
#endif
		sid[i].sFrameCount++;
#ifdef SID_USES_LOCK
		SDL_AtomicUnlock(&sid[i].spinlock);
		DEBUG_AUDIO_LOCKS("WRITER: Released SID#%d lock (%d)" NL, i, sid[i].spinlock);
#endif
	}
}


#ifdef AUDIO_EMULATION
static inline void render_dma_audio ( int channel, short *buffer, int len )
{
	static short sample[4];	// current sample values of the four channels, normalized to 16 bit signed value
	static double rate_counter[4] = {0,0,0,0};
	Uint8 *chio = D7XX + 0x20 + channel * 0x10;
	unsigned int addr = chio[0xA] + (chio[0xB] << 8) + (chio[0xC] << 16);
	const Uint16 limit = chio[0x7] + (chio[0x8] << 8);
	const double rate_step =
		(double)(chio[4] + (chio[5] << 8) + (chio[6] << 16))		// this value is added to the 24 bit counter every 40.5MHz clock, to see overflow
		*
		dma_audio_mixing_value;
	for (unsigned int i = 0; i < len; i++) {
		if (!(chio[0] & 0x80) || (chio[0] & 0x08)) {
			// silence
			sample[channel] = 0;
			rate_counter[channel] = 0;
			buffer[i] = 0;
			continue;
		}
		rate_counter[channel] += rate_step;
		// the reason for while loop: real MEGA65 would not do this, but mixing frequency of audio on typical
		// PC is low (~44-48KHz) compared to the native MEGA65 40.5MHz clock. Thus in some cases we need more steps
		// within a single point. Surely it can be optimized a better way, but for now ...
		while (rate_counter[channel] >= 0x1000000) {
			rate_counter[channel] -= 0x1000000;
			if (XEMU_UNLIKELY(addr >= (sizeof(main_ram) - 1))) {	// do not overflow fast RAM, as DMA audio can do 24 bit, but fast RAM is much smaller
				sample[channel] = 0;
				addr += ((chio[0] & 3) == 3) ? 2 : 1;
			} else {
				Uint16 unsigned_read;
				switch (chio[0] & 3) {
					case 0:	// lower nybble only
						unsigned_read = (main_ram[addr] & 0x0F) << 12;
						addr++;
						break;
					case 1:	// high nybble only
						unsigned_read = (main_ram[addr] & 0xF0) << 8;
						addr++;
						break;
					case 2:	// 8 bit sample
						unsigned_read = main_ram[addr] << 8;
						addr++;
						break;
					case 3:	// 16 bit sample
						unsigned_read = main_ram[addr] + (main_ram[addr + 1] << 8);
						addr += 2;
						break;
					default:
						XEMU_UNREACHABLE();
				}
				// TODO: use unsigned_read, convert signed<->unsigned stuff, etc ....
				// NOTE: the read above to 'unsigned_read' can be still signed, we just read as unsigned 16 bit uniform data
				// so we can transform here to the output needs (that is: signed 16 bit). It's based on MEGA65's audio DMA
				// setting: is it fed by unsigned or signed samples?
				sample[channel] = unsigned_read - 0x8000;
				sample[channel] = unsigned_read;
				sample[channel] = (sample[channel] * chio[9]) / 0xFF;	// volume control (reg9, max volume $FF)
			}
			if (XEMU_UNLIKELY((addr & 0xFFFF) == limit)) {
				// if top address is reached: either stop, or loop (on looped samples only!)
				if ((chio[0] & 0x40)) {
					addr = chio[1] + (chio[2] << 8) + (chio[3] << 16);	// looping, set address to the begin address
				} else {
					chio[0] |= 8;		// no loop, stop!
					sample[channel] = 0;
					rate_counter[channel] = 0;
					break;
				}
			}
		}
		// render one sample for this channel to the buffer
		buffer[i] = sample[channel];
	}
	// End of loop:
	// write back address ...
	chio[0xA] =  addr        & 0xFF;
	chio[0xB] = (addr >>  8) & 0xFF;
	chio[0xC] = (addr >> 16) & 0xFF;
}


void audio_set_stereo_parameters ( int vol, int sep )
{
	if (sep == AUDIO_UNCHANGED_SEPARATION) {
		sep = stereo_separation;
	} else {
		if (sep > 100)
			sep = 100;
		else if (sep < -100)
			sep = -100;
		stereo_separation = sep;
	}
	if (vol == AUDIO_UNCHANGED_VOLUME) {
		vol = audio_volume;
	} else {
		if (vol > 100)
			vol = 100;
		else if (vol < 0)
			vol = 0;
		audio_volume = vol;
	}
	//sep = ((sep + 100) * 0x100) / 200;
	sep = (sep + 100) / 2;
	//sep = (sep + 100) * 0x100 / 200;
	stereo_separation_orig  = (sep * vol) / 100;
	stereo_separation_other = ((100 - sep) * vol) / 100;
	DEBUGPRINT("AUDIO: volume is set to %d%%, stereo separation is %d%% [component-A is %d, component-B is %d]" NL, audio_volume, stereo_separation, stereo_separation_orig, stereo_separation_other);
}


#define AUDIO_BUFFER_SAMPLES_MAX	1024
static Sint16 streams[9][AUDIO_BUFFER_SAMPLES_MAX];


static void audio_callback ( void *userdata, Uint8 *stereo_out_stream, int len )
{
	static volatile int in_progress = 0;
	if (XEMU_UNLIKELY(in_progress)) {
		DEBUGPRINT("AUDIO: Error, overlapping audio callback calls!" NL);
		return;
	}
	in_progress = 1;
	static int nosound_previous = -1;
	if (XEMU_UNLIKELY(nosound_previous != configdb.nosound)) {
		nosound_previous = configdb.nosound;
		DEBUGPRINT("AUDIO: callback switches to %s mode." NL, configdb.nosound ? "silent" : "working");
	}
	if (configdb.nosound) {
		// Render silence ...
		memset(stereo_out_stream, 0, len);
		goto END;
	}
	len >>= 2;	// the size in *SAMPLES* (not in bytes) is /4, since it's a stereo stream, and 2 bytes/sample, we want to render
	//DEBUGPRINT("AUDIO: audio callback, wants %d samples to be rendered" NL, len);
	//short streams[9][len];	// currently. 4 dma channels + 4 SIDs + 1 for OPL3
	if (len > AUDIO_BUFFER_SAMPLES_MAX) {
		len = AUDIO_BUFFER_SAMPLES_MAX;
		DEBUGPRINT("AUDIO: ERROR, SDL wants more samples (%d) than buffer size (%d)!" NL, len, AUDIO_BUFFER_SAMPLES_MAX);
	}
	//DEBUGPRINT("p=%p 0=%p 1=%p, 2=%p, 3=%p, 4=%p, 5=%p, 6=%p, 7=%p, 8=%p" NL, streams, streams[0], streams[1], streams[2], streams[3], streams[4], streams[5], streams[6], streams[7], streams[8]);
	for (int i = 0; i < 4; i++)
		render_dma_audio(i, streams[i], len);
	// SIDs: #0 $D400 - left,  #1 $D420 - left, #2 $D440 - right, #3 $D460 - right
	for (int i = 0; i < 4; i++) {
		if (XEMU_UNLIKELY(!(configdb.sidmask & (1 << i)))) {
			memset(streams[4 + i], 0, len * 4);
			continue;
		}
#ifdef SID_USES_LOCK
		DEBUG_AUDIO_LOCKS("RENDER: Waiting for SID lock #%d (%d)" NL, i, sid[i].spinlock);
		SDL_AtomicLock(&sid[i].spinlock);
		DEBUG_AUDIO_LOCKS("RENDER: Got SID lock #%d (%d)" NL, i, sid[i].spinlock);
#endif
		sid_render(&sid[i], streams[4 + i], len, 1);
#ifdef SID_USES_LOCK
		SDL_AtomicUnlock(&sid[i].spinlock);
		DEBUG_AUDIO_LOCKS("RENDER: Released SID lock #%d (%d)" NL, i, sid[i].spinlock);
#endif
	}
	//sid_render(&sid[0], streams[4], len, 1);	// $D400 - left
	//sid_render(&sid[1], streams[5], len, 1);	// $D420 - left
	//sid_render(&sid[2], streams[6], len, 1);	// $D440 - right
	//sid_render(&sid[3], streams[7], len, 1);	// $D460 - right
	if (XEMU_LIKELY(!configdb.noopl3)) {
		//DEBUGPRINT("before OPL buffer will be %d, len requested: %d" NL, (int)(streams[8] - streams[0]), len);
#ifdef OPL_USES_LOCK
		DEBUG_AUDIO_LOCKS("RENDER: Waiting for OPL3 lock ... (%d)" NL, opl3_lock);
		SDL_AtomicLock(&opl3_lock);
		DEBUG_AUDIO_LOCKS("RENDER: Got OPL3 lock (%d)" NL, opl3_lock);
#endif
		OPL3_GenerateStream(&opl3, streams[8], len, 1);
#ifdef OPL_USES_LOCK
		SDL_AtomicUnlock(&opl3_lock);
		DEBUG_AUDIO_LOCKS("RENDER: Released OPL3 lock (%d)" NL, opl3_lock);
#endif
		//DEBUGPRINT("after OPL" NL);
	} else {
		memset(streams[8], 0, len * 4);
	}
	// Now mix channels
	for (int i = 0; i < len; i++) {
		// mixing streams together
		const int orig_left  = (int)streams[0][i] + (int)streams[1][i] + (int)streams[4][i] + (int)streams[5][i] + (int)streams[8][i];
		const int orig_right = (int)streams[2][i] + (int)streams[3][i] + (int)streams[6][i] + (int)streams[7][i] + (int)streams[8][i];
#if 1
		// channel stereo separation (including inversion) + volume handling
		int left  = ((orig_left  * stereo_separation_orig) / 100) + ((orig_right * stereo_separation_other) / 100);
		int right = ((orig_right * stereo_separation_orig) / 100) + ((orig_left  * stereo_separation_other) / 100);
#else
		int left = orig_left;
		int right = orig_right;
#endif
		// do some ugly clipping ...
		if      (left  >  0x7FFF) left  =  0x7FFF;
		else if (left  < -0x8000) left  = -0x8000;
		if      (right >  0x7FFF) right =  0x7FFF;
		else if (right < -0x8000) right = -0x8000;
		// write the output stereo stream for SDL (it's an interlaved left-right-left-right kind of thing)
		((short*)stereo_out_stream)[ i << 1     ] = left;
		((short*)stereo_out_stream)[(i << 1) + 1] = right;
	}
	//DEBUGPRINT("AUDIO: END OF SDL AUDIO THREAD" NL);
END:
	in_progress = 0;
}
#endif


void audio65_reset ( void )
{
	// We always initialize SIDs/OPL, even if no audio emulation is compiled in
	// Since there can be problem to write SID registers otherwise?
	sid_init(&sid[0], system_sid_cycles_per_sec, system_sound_mix_freq);
	sid_init(&sid[1], system_sid_cycles_per_sec, system_sound_mix_freq);
	sid_init(&sid[2], system_sid_cycles_per_sec, system_sound_mix_freq);
	sid_init(&sid[3], system_sid_cycles_per_sec, system_sound_mix_freq);
	OPL3_Reset(&opl3, system_sound_mix_freq);
	DEBUGPRINT("AUDIO: reset for 4 SIDs (%d cycles per sec) and 1 OPL3 chip for %dHz sampling rate." NL, system_sid_cycles_per_sec, system_sound_mix_freq);
#if 0
	SDL_AtomicUnlock(&opl3_lock);
	for (int i = 0; i < 4; i++)
		SDL_AtomicUnlock(&sid_locks[i]);
#endif
}


void audio65_start ( void )
{
	static volatile int started = 0;
	if (started) {
		ERROR_WINDOW("Trying to restart audio??\nRefuseing to do so!!");
		return;
	}
	started = 1;
	if (!audio)
		return;
	DEBUGPRINT("AUDIO: start mixing." NL);
	SDL_PauseAudioDevice(audio, 0);
}


void audio65_init ( int sid_cycles_per_sec, int sound_mix_freq, int volume, int separation )
{
	static volatile int initialized = 0;
	if (initialized) {
		ERROR_WINDOW("Trying to reinitialize audio??\nRefusing to do so!!");
		return;
	}
	initialized = 1;
	system_sound_mix_freq = sound_mix_freq;
	system_sid_cycles_per_sec = sid_cycles_per_sec;
	audio65_reset();
#ifdef AUDIO_EMULATION
	dma_audio_mixing_value =  (double)40500000.0 / (double)sound_mix_freq;	// ... but with Xemu we use a much lower sampling rate, thus compensate (will fail on samples, rate >= xemu_mixing_rate ...)
	SDL_AudioSpec audio_want, audio_got;
	SDL_memset(&audio_want, 0, sizeof(audio_want));
	audio_want.freq = sound_mix_freq;
	audio_want.format = AUDIO_S16SYS;	// used format by SID emulation (ie: signed short, with native endianness)
	audio_want.channels = 2;		// that is: stereo, for the two SIDs
	audio_want.samples = AUDIO_BUFFER_SAMPLES_MAX;		// Sample size suggested (?) for the callback to render once
	audio_want.callback = audio_callback;	// Audio render callback function, called periodically by SDL on demand
	audio_want.userdata = NULL;		// Not used, "userdata" parameter passed to the callback by SDL
	if (audio)
		ERROR_WINDOW("audio was not zero before calling SDL_OpenAudioDevice!");
	audio = SDL_OpenAudioDevice(NULL, 0, &audio_want, &audio_got, 0);
	if (audio) {
		for (int i = 0; i < SDL_GetNumAudioDevices(0); i++)
			DEBUG("AUDIO: audio device is #%d: %s" NL, i, SDL_GetAudioDeviceName(i, 0));
		// Sanity check that we really got the same audio specification we wanted
		if (audio_want.freq != audio_got.freq || audio_want.format != audio_got.format || audio_want.channels != audio_got.channels) {
			SDL_CloseAudioDevice(audio);	// forget audio, if it's not our expected format :(
			audio = 0;
			ERROR_WINDOW("Audio parameter mismatches.");
		}
		DEBUGPRINT("AUDIO: initialized (#%d), %d Hz, %d channels, %d buffer sample size." NL, audio, audio_got.freq, audio_got.channels, audio_got.samples);
		//if (audio) {
		//	DEBUGPRINT("AUDIO: !!!!!!!!!!! sample size = %d" NL, audio_got.samples);
		//}
	} else
		ERROR_WINDOW("Cannot open audio device!");
	audio_set_stereo_parameters(volume, separation);
#else
	DEBUGPRINT("AUDIO: has been disabled at compilation time." NL);
#endif
}
