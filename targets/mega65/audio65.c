/* A work-in-progess MEGA65 (Commodore 65 clone origins) emulator
   Part of the Xemu project, please visit: https://github.com/lgblgblgb/xemu
   Copyright (C)2016-2020 LGB (Gábor Lénárt) <lgblgblgb@gmail.com>

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
#include "xemu/sid.h"
#include "audio65.h"
// For D7XX (audio DMA):
#include "io_mapper.h"
// For accessing memory (audio DMA):
#include "memory_mapper.h"


SDL_AudioDeviceID audio = 0;	// SDL audio device
struct SidEmulation sid1, sid2;	// the two SIDs
static int mixing_freq;		// playback sample rate (in Hz) of the emulator itself
static double dma_audio_mixing_value;



#ifdef AUDIO_EMULATION
static void render_dma_audio ( int channel, short *buffer, int len )
{
	static short sample[4];	// current sample values of the four channels, normalized to 16 bit signed value
	static double rate_counter[4] = {0,0,0,0};
	Uint8 *chio = D7XX + 0x20 + channel * 0x10;
	unsigned int addr = chio[0xA] + (chio[0xB] << 8) + (chio[0xC] << 16);
	Uint16 limit = chio[0x7] + (chio[0x8] << 8);
	double rate_step = 
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
				sample[channel] = unsigned_read - 0x8000;
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
	chio[0xA] = addr & 0xFF;
	chio[0xB] = (addr >> 8) & 0xFF;
	chio[0xC] = (addr >> 16) & 0xFF;
}




static void audio_callback ( void *userdata, Uint8 *stereo_out_stream, int len )
{
#if 1
	//DEBUG("AUDIO: audio callback, wants %d samples" NL, len);
	len >>= 2;	// the real size if /4, since it's a stereo stream, and 2 bytes/sample, we want to render
	short streams[6][len];	// currently. 4 dma channels + two SIDs' 1-1 channel (SID is already rendered together)
	sid_render(&sid2, streams[4], len, 1);
	sid_render(&sid1, streams[5], len, 1);
	for (int i = 0; i < 4; i++)
		render_dma_audio(i, streams[i], len);
	// Now mix channels
	for (int i = 0; i < len; i++) {
		// mixing streams together
		int left  = streams[0][i] + streams[1][i] + streams[4][i];
		int right = streams[2][i] + streams[3][i] + streams[5][i];
		// do some ugly clipping ...
		if      (left  >  0x7FFF) left  =  0x7FFF;
		else if (left  < -0x8000) left  = -0x8000;
		if      (right >  0x7FFF) right =  0x7FFF;
		else if (right < -0x8000) right = -0x8000;
		// write the output stereo stream for SDL
		((short*)stereo_out_stream)[i << 1] = left;
		((short*)stereo_out_stream)[(i << 1) + 1] = right;
	}
#else
	// DEBUG("AUDIO: audio callback, wants %d samples" NL, len);
	// We use the trick, to render boths SIDs with step of 2, with a byte offset
	// to get a stereo stream, wanted by SDL.
	sid_render(&sid2, ((short *)(stereo_out_stream)) + 0, len >> 1, 2);	// SID @ left
	sid_render(&sid1, ((short *)(stereo_out_stream)) + 1, len >> 1, 2);	// SID @ right
#endif
}
#endif


void audio65_init ( int sid_cycles_per_sec, int sound_mix_freq )
{
	// We always initialize SIDs, even if no audio emulation is compiled in
	// Since there can be problem to write SID registers otherwise?
	sid_init(&sid1, sid_cycles_per_sec, sound_mix_freq);
	sid_init(&sid2, sid_cycles_per_sec, sound_mix_freq);
#ifdef AUDIO_EMULATION
	mixing_freq = sound_mix_freq;
	dma_audio_mixing_value =  (double)40500000.0 / (double)sound_mix_freq;	// ... but with Xemu we use a much lower sampling rate, thus compensate (will fail on samples, rate >= xemu_mixing_rate ...)
	SDL_AudioSpec audio_want, audio_got;
	SDL_memset(&audio_want, 0, sizeof(audio_want));
	audio_want.freq = sound_mix_freq;
	audio_want.format = AUDIO_S16SYS;	// used format by SID emulation (ie: signed short)
	audio_want.channels = 2;		// that is: stereo, for the two SIDs
	audio_want.samples = 1024;		// Sample size suggested (?) for the callback to render once
	audio_want.callback = audio_callback;	// Audio render callback function, called periodically by SDL on demand
	audio_want.userdata = NULL;		// Not used, "userdata" parameter passed to the callback by SDL
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
	} else
		ERROR_WINDOW("Cannot open audio device!");
#else
	DEBUGPRINT("AUDIO: has been disabled at compilation time." NL);
#endif
}
