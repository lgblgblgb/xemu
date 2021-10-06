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
//#define CORRUPTION_DEBUG

#include "xemu/emutools.h"
#include "xemu/opl3.h"
#include "audio65.h"
// For D7XX (audio DMA):
#include "io_mapper.h"
// For accessing memory (audio DMA):
#include "memory_mapper.h"
#include "configdb.h"


int stereo_separation = AUDIO_DEFAULT_SEPARATION;
int audio_volume      = AUDIO_DEFAULT_VOLUME;

struct SidEmulation sid[NUMBER_OF_SIDS];

static opl3_chip opl3;

static SDL_AudioDeviceID audio = 0;		// SDL audio device
static int stereo_separation_orig = 100;
static int stereo_separation_other = 0;
static int system_sound_mix_freq;		// playback sample rate (in Hz) of the emulator itself
static int system_sid_cycles_per_sec;
static double dma_audio_mixing_value;


#if NUMBER_OF_SIDS != 4
#	error "Currently NUMBER_OF_SIDS macro must be set to 4!"
#endif

// --- choose one only, normally you want the last (no debug), especially in RELEASEs. Debugging is spammy and expensive in CPU time! ---
//#define DEBUG_AUDIO_LOCKS(...) DEBUGPRINT(__VA_ARGS__)
//#define DEBUG_AUDIO_LOCKS(...) DEBUG(__VA_ARGS__)
#define DEBUG_AUDIO_LOCKS(...)


#ifdef OPL_USES_LOCK
static SDL_SpinLock opl3_lock;
static XEMU_INLINE void LOCK_OPL ( const char *m )
{
	DEBUG_AUDIO_LOCKS("%s: Waiting for OPL3 lock (%d)" NL, m, opl3_lock);
	SDL_AtomicLock(&opl3_lock);
	DEBUG_AUDIO_LOCKS("%s: Got OPL3 lock (%d)" NL, m, opl3_lock);
}
static XEMU_INLINE void UNLOCK_OPL ( const char *m )
{
	SDL_AtomicUnlock(&opl3_lock);
	DEBUG_AUDIO_LOCKS("%s: Released OPL3 lock (%d)" NL, m, opl3_lock);
}
#else
#	define LOCK_OPL(m)
#	define UNLOCK_OPL(m)
#	warning "Disabled LOCK for OPL3, you may experience Xemu crashes and/or sound anomalies!"
#endif


#ifdef SID_USES_LOCK
static XEMU_INLINE void LOCK_SID ( const char *m, const int i )
{
	DEBUG_AUDIO_LOCKS("%s: Waiting for SID#%d lock (%d)" NL, m, i, sid[i].spinlock);
	SDL_AtomicLock(&sid[i].spinlock);
	DEBUG_AUDIO_LOCKS("%s: Got SID#%d lock (%d)" NL, m, i, sid[i].spinlock);
}
static XEMU_INLINE void UNLOCK_SID ( const char *m, const int i )
{
	SDL_AtomicUnlock(&sid[i].spinlock);
	DEBUG_AUDIO_LOCKS("%s: Released SID#%d lock (%d)" NL, m, i, sid[i].spinlock);
}
#else
#	define LOCK_SID(m,i)
#	define UNLOCK_SID(m,i)
#	warning "Disabled LOCK for SID, you may experience Xemu crashes and/or sound anomalies!"
#endif


void audio65_sid_write ( const int addr, const Uint8 data )
{
	// SIDs are separated by $20 bytes from each others, 4 SIDs (0-3), instance number
	// figured out from address
	const int instance = (addr >> 5) & 3;
	LOCK_SID("WRITER", instance);
	sid_write_reg(&sid[instance], addr & 0x1F, data);
	UNLOCK_SID("WRITER", instance);
}


void audio65_opl3_write ( Uint8 reg, Uint8 data )
{
	if (XEMU_UNLIKELY(configdb.noopl3))
		return;
	LOCK_OPL("WRITER");
	//OPL3_WriteReg(&opl3, reg, data);
	OPL3_WriteRegBuffered(&opl3, reg, data);
	UNLOCK_OPL("WRITER");
}


void audio65_sid_inc_framecount ( void )
{
	for (int i = 0; i < NUMBER_OF_SIDS; i++) {
		LOCK_SID("INCER", i);
		sid[i].sFrameCount++;
		UNLOCK_SID("INCER", i);
	}
}


#ifdef AUDIO_EMULATION
static inline void render_dma_audio ( int channel, Sint16 *buffer, int len )
{
	static Sint16 sample[4];	// current sample values of the four channels, normalized to 16 bit signed value
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


// 10 channels, consist of: 4 channel for audio DMA, 4 channel for SIDs (each SIDs are pre-mixed to one channel by sid.c), 2 OPL3 channel (OPL3 is pre-mixed into two channels in opl3.c)
#define MIXED_CHANNELS			10

#ifdef CORRUPTION_DEBUG
#	define	EXTRA_STREAM_CHANNELS	99
#else
#	define	EXTRA_STREAM_CHANNELS	0
#endif

#define	STREAMS_SIZE_ALL		(((MIXED_CHANNELS) + (EXTRA_STREAM_CHANNELS)) * (AUDIO_BUFFER_SAMPLES_MAX))
#define STREAMS(n)			(streams + ((n) * (AUDIO_BUFFER_SAMPLES_MAX)))
#define STREAMS_SAMPLE(n,d)		((int)(STREAMS(n)[d]))


static void audio_callback ( void *userdata, Uint8 *stereo_out_stream, int len )
{
	static Sint16 streams[STREAMS_SIZE_ALL];
	static int nosound_previous = -1;
	if (XEMU_UNLIKELY(nosound_previous != configdb.nosound)) {
		nosound_previous = configdb.nosound;
		DEBUGPRINT("AUDIO: callback switches to %s mode." NL, configdb.nosound ? "silent" : "working");
	}
	if (XEMU_UNLIKELY(configdb.nosound)) {
		// Render silence ...
		// Here we use "len" as-is, since it's before the shift operation below, and means BYTES, what SDL is asking from us
		memset(stereo_out_stream, 0, len);
		return;
	}
	//DEBUGPRINT("AUDIO: audio callback, wants %d bytes to be rendered" NL, len);
	len >>= 2;	// the size in *SAMPLES* (not in bytes) is /4, since it's a stereo stream, and 2 bytes/sample, we want to render
	//DEBUGPRINT("AUDIO: audio callback, wants %d samples to be rendered" NL, len);
	if (XEMU_UNLIKELY(len > AUDIO_BUFFER_SAMPLES_MAX)) {
		len = AUDIO_BUFFER_SAMPLES_MAX;
		DEBUGPRINT("AUDIO: ERROR, SDL wants more samples (%d) than buffer size (%d)!" NL, len, AUDIO_BUFFER_SAMPLES_MAX);
	}
	// Render samples from the four audio DMA units
	for (int i = 0; i < 4; i++)
		render_dma_audio(i, STREAMS(i), len);
	// Render samples from the four SIDs
	for (int i = 0; i < NUMBER_OF_SIDS; i++) {
		if (XEMU_UNLIKELY(!(configdb.sidmask & (1 << i)))) {
			memset(STREAMS(4 + i), 0, len * sizeof(Sint16));
			continue;
		}
		LOCK_SID("RENDER", i);
		sid_render(&sid[i], STREAMS(4 + i), len, 1);
		UNLOCK_SID("RENDER", i);
	}
	// Render samples for the OPL3 emulation
	if (XEMU_LIKELY(!configdb.noopl3)) {
		LOCK_OPL("RENDER");
		OPL3_GenerateStream(&opl3, STREAMS(8), STREAMS(9), len, 1, 1);
		UNLOCK_OPL("RENDER");
	} else {
		memset(STREAMS(8), 0, len * sizeof(Sint16));
		memset(STREAMS(9), 0, len * sizeof(Sint16));
	}
	// Now mix the result ...
	for (int i = 0, j = 0; i < len; i++) {
		// mixing streams together
		// Currently: put the first two SIDS to the left, the second two to the right, same for DMA audio channels, and OPL3 seems to need 2 channel
		const register int orig_left  = STREAMS_SAMPLE(0, i) + STREAMS_SAMPLE(1, i) + STREAMS_SAMPLE(4, i) + STREAMS_SAMPLE(5, i) + STREAMS_SAMPLE(8, i);
		const register int orig_right = STREAMS_SAMPLE(2, i) + STREAMS_SAMPLE(3, i) + STREAMS_SAMPLE(6, i) + STREAMS_SAMPLE(7, i) + STREAMS_SAMPLE(9, i);
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
		((Sint16*)stereo_out_stream)[j++] = left;
		((Sint16*)stereo_out_stream)[j++] = right;
	}
#ifdef CORRUPTION_DEBUG
#	warning "You have CORRUPTION_DEBUG enabled"
	for (Sint16 *p = STREAMS(MIXED_CHANNELS); p < streams + STREAMS_SIZE_ALL; p++) {
		if (*p)
			DEBUGPRINT("AUDIO BUFFER CORRUPTION AT OFFSET: %d" NL, (int)(p - STREAMS(0)));
	}
#endif
}
#endif



void audio65_clear_regs ( void )
{
	// OPL and SIDs lock are implemented by the ..._write() function, no need to take care here
	for (int i = 0; i < 0x100; i++)
		audio65_opl3_write(i, 0);
	for (int i = 0; i < NUMBER_OF_SIDS * 0x20; i++)
		audio65_sid_write(i << i, 0);
	memset(D7XX + 0x20, 0, 0x40);	// DMA audio related registers
	DEBUGPRINT("AUDIO: clearing audio related registers." NL);
}


void audio65_reset ( void )
{
	// We always initialize SIDs/OPL, even if no audio emulation is compiled in
	// Since there can be problem to write SID registers otherwise?
	for (int i = 0; i < NUMBER_OF_SIDS; i++) {
		LOCK_SID("RESET", i);
		sid_init(&sid[i], system_sid_cycles_per_sec, system_sound_mix_freq);
		UNLOCK_SID("RESET", i);
	}
	LOCK_OPL("RESET");
	OPL3_Reset(&opl3, system_sound_mix_freq);
	UNLOCK_OPL("RESET");
	DEBUGPRINT("AUDIO: reset for %d SIDs (%d cycles per sec) and 1 OPL3 chip for %dHz sampling rate." NL, NUMBER_OF_SIDS, system_sid_cycles_per_sec, system_sound_mix_freq);
	audio65_clear_regs();
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


void audio65_init ( int sid_cycles_per_sec, int sound_mix_freq, int volume, int separation, unsigned int buffer_size )
{
	static volatile int initialized = 0;
	if (initialized) {
		ERROR_WINDOW("Trying to reinitialize audio??\nRefusing to do so!!");
		return;
	}
	initialized = 1;
	for (int i = 0; i < NUMBER_OF_SIDS; i++)
		UNLOCK_SID("INIT", i);
	UNLOCK_OPL("INIT");
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
	audio_want.samples = buffer_size;	// Sample size suggested (?) for the callback to render once
	audio_want.callback = audio_callback;	// Audio render callback function, called periodically by SDL on demand
	audio_want.userdata = NULL;		// Not used, "userdata" parameter passed to the callback by SDL
	if (audio)
		ERROR_WINDOW("audio was not zero before calling SDL_OpenAudioDevice!");
	audio = SDL_OpenAudioDevice(NULL, 0, &audio_want, &audio_got, 0);
	if (audio) {
		for (int i = 0; i < SDL_GetNumAudioDevices(0); i++)
			DEBUG("AUDIO: audio device is #%d: %s" NL, i, SDL_GetAudioDeviceName(i, 0));
		// Sanity check that we really got the same audio specification we wanted
		if (audio_want.freq != audio_got.freq || audio_want.format != audio_got.format || audio_want.channels != audio_got.channels || audio_got.samples > AUDIO_BUFFER_SAMPLES_MAX) {
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
