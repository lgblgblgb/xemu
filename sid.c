/*
 * This source is a modifed version of sidengine.c from here: https://github.com/wothke/websid
 * Modification is mainly about killing the not-SID related parts, and making emulation modular,
 * ie introducing a way to allow to emulate more SIDs through the SidEmulation structure pointer.
 * Otherwise it should be the same. Sorry, I am just lame with sound things, the modularization
 * can be done in a better way, ie some things should not be per SID stuff, etc.
 * Modifications done by: Gabor Lenart (aka LGB) lgblgblgb@gmail.com http://lgb.hu/
 * Mainly for having *some* sound for my primitive Commodore 65 emulator :)
 * https://github.com/lgblgblgb/xclcd
 *
 * ---- NOW THE ORIGINAL COMMENT HEADER FOLLOWS ----
 *
 * This file is largely the original file from the "TinySid for Linux" distribution.
 *
 * <p>TinySid (c) 1999-2012 T. Hinrichs, R. Sinsch
 *
 * <p>It was updated by merging in the latest "Rockbox" version (This noticably fixed playback problems with
 * "Yie Ar Kung Fu"..) and by applying fixes contributed by Markus Gritsch. Unfortunately a revision history of the old
 * TinySid codebase does not seem to exist.. so we'll probably never know what was used for the TinySid Windows executable and
 * why it is the only version that correctly plays Electric_Girls.sid..)
 * <p>In this file I deliberately kept the "naming conventions" used in the original TinySID code - to ease future merging
 * of potential TinySid fixes (consequently there is a mismatch with the conventions that I am using in my own code..)
 *
 * <p>My additions here are:
 *   <ol>
 *    <li>fixed PSID digi playback volume (was originally too low)
 *    <li>correct cycle-time calculation for ALL 6510 op codes (also illegal ones)
 *    <li>added impls for illegal 6510 op codes, fixed errors in V-flag calculation, added handling for 6510 addressing "bugs"
 *    <li>poor man's VIC and CIA handling
 *    <li>"cycle limit" feature used to interrupt emulation runs (e.g. main prog can now be suspended/continued)
 *    <li>Poor man's "combined pulse/triangle waveform" impl to allow playback of songs like Kentilla.sid.
 *    <li>added RSID digi playback support (D418 based as well as "pulse width modulation" based): it is a "special feature" of
 *    this implementation that regular SID emulation is performed somewhat independently from the handling of digi samples, i.e. playback
 *    of digi samples is tracked separately (for main, IRQ and NMI) and the respective digi samples are then merged with the regular SID
 *    output as some kind of postprocessing step
 *    <li> replaced original "envelope generator" impl with a more realistic one (incl. "ADSR-bug" handling)
 *  </ol>
 *
 *	FIXME: refactor CPU and SID emulation into separate files..
 *
 * known limitation: basic-ROM specific handling not implemented...
 *
 * <p>Notice: if you have questions regarding the details of the below SID emulation, then you should better get in touch with R.Sinsch :-)
 *
 * <p>Tiny'R'Sid add-ons (c) 2015 J.Wothke
 *
 * Terms of Use: This software is licensed under a CC BY-NC-SA
 * (http://creativecommons.org/licenses/by-nc-sa/4.0/).
 */


 // useful links:
 // http://www.waitingforfriday.com/index.php/Commodore_SID_6581_Datasheet
 // http://www.sidmusic.org/sid/sidtech2.html
 // http://www.oxyron.de/html/opcodes02.html

#include <string.h>
#include <stdio.h>
#include <math.h>
#include <stdlib.h>


#include "sidengine.h"


/*
#include "sidengine.h"

#include "defines.h"
#include "digi.h"
#include "nanovic.h"
#include "nanocia.h"
#include "rsidengine.h"
#include "sidplayer.h"
*/

static unsigned char exponential_delays[256];


static unsigned long getCyclesPerSec() {
        return 982800;
}




// hacks
//static unsigned char sFake_d012_count=0;
//static unsigned char sFake_d012_loop=0;

//static unsigned int sCiaNmiVectorHack= 0;

//static void setCiaNmiVectorHack(){
//	sCiaNmiVectorHack= 1;
//}

/* Routines for quick & dirty float calculation */
static inline int pfloat_ConvertFromInt(int i) 		{ return (i<<16); }
static inline int pfloat_ConvertFromFloat(float f) 	{ return (int)(f*(1<<16)); }
static inline int pfloat_Multiply(int a, int b) 	{ return (a>>8)*(b>>8); }
static inline int pfloat_ConvertToInt(int i) 		{ return (i>>16); }


// note: decay/release times are 3x longer (implemented via exponential_counter)
static const int attackTimes[16]  =	{
	2, 8, 16, 24, 38, 56, 68, 80, 100, 240, 500, 800, 1000, 3000, 5000, 8000
};


//static unsigned long getSampleFrequency() {
//	return mixing_frequency;
//}

//static void setMute(unsigned char voice) {
//	sMuteVoice[voice] = 1;
//}

/* Get the bit from an unsigned long at a specified position */
static inline unsigned char get_bit(unsigned long val, unsigned char b)
{
    return (unsigned char) ((val >> b) & 1);
}




// poor man's lookup table for combined pulse/triangle waveform (this table does not 
// lead to correct results but it is better that nothing for songs like Kentilla.sid)
// feel free to come up with a better impl!
// FIXME: this table was created by sampling kentilla output.. i.e. it already reflects the envelope
// used there and may actually be far from the correct waveform
static const signed char pulseTriangleWavetable[] =
{
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x06, 0x06, 0x00,
	0x00, 0x06, 0x06, 0x06, 0x00, 0x00, 0x00, 0x06, 0x06, 0x06, 0x06, 0x00, 0x06, 0x06, 0x00, 0x10,
	0x10, 0x00, 0x00, 0x06, 0x06, 0x06, 0x00, 0x06, 0x06, 0x06, 0x06, 0x00, 0x06, 0x06, 0x00, 0x20,
	0x10, 0x00, 0x06, 0x06, 0x06, 0x06, 0x0b, 0x15, 0x0b, 0x0b, 0x0b, 0x15, 0x25, 0x2f, 0x2f, 0x69,
	0x20, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x0b, 0x15, 0x15,
	0x1b, 0x06, 0x0b, 0x10, 0x0b, 0x0b, 0x15, 0x25, 0x15, 0x0b, 0x0b, 0x63, 0x49, 0x69, 0x88, 0x3a,
	0x3a, 0x06, 0x0b, 0x06, 0x10, 0x10, 0x15, 0x3f, 0x10, 0x25, 0x59, 0x59, 0x3f, 0x9d, 0xa7, 0x59,
	0x00, 0x00, 0x5e, 0x59, 0x88, 0xb7, 0xb7, 0xac, 0x83, 0xac, 0xd1, 0xc6, 0xc1, 0xdb, 0xdb, 0xeb,
	/* rather symetrical: let's just mirror the above part */
};











// util related to envelope generator LFSR counter
static int clocksToSamples(int clocks) {
#ifdef SID_USES_SAMPLE_ENV_COUNTER
	return round(((float)clocks)/cyclesPerSample)+1;
#else
	return clocks;
#endif
}




/*
* check if LFSR threshold was reached
*/
static unsigned char triggerLFSR_Threshold(unsigned int threshold, signed int *end) {
	if (threshold == (*end)) {
		(*end)= 0; // reset counter
		return 1;
	}
	return 0;
}




static unsigned char handleExponentialDelay ( struct SidEmulation *sidemu, unsigned char voice ) {
	sidemu->osc[voice].exponential_counter+= 1;
	unsigned char result= (sidemu->osc[voice].exponential_counter >= exponential_delays[sidemu->osc[voice].envelopeOutput]);
	if (result) {
		sidemu->osc[voice].exponential_counter= 0;	// reset to start next round
	}
	return result;
}




static void simOneEnvelopeCycle( struct SidEmulation *sidemu, unsigned char v ) {
	// now process the volume according to the phase and adsr values
	// (explicit switching of ADSR phase is handled in sidPoke() so there is no need to handle that here)

	// advance envelope LFSR counter (originally this would be a 15-bit cycle counter.. but we may be counting samples here)

	// ADSR bug scenario: normally the maximum thresholds used for the original 15-bit counter would have been around
	// 0x7a13 (i.e. somewhat below the 7fff range that can be handled by the counter). For certain bug scenarios
	// it is possible that the threshold is missed and the counter keeps counting until it again reaches the
	// threshold after a wrap-around.. (see sidPoke() for specific ADSR-bug handling)

	if (++sidemu->osc[v].currentLFSR == sidemu->limit_LFSR) {
		sidemu->osc[v].currentLFSR= 0;
	}

	unsigned char previousEnvelopeOutput = sidemu->osc[v].envelopeOutput;

	switch (sidemu->osc[v].envphase) {
		case Attack: {                          // Phase 0 : Attack
			if (triggerLFSR_Threshold(sidemu->osc[v].attack, &sidemu->osc[v].currentLFSR)) {	// inc volume when threshold is reached
				if (!sidemu->osc[v].zero_lock) {
					if (sidemu->osc[v].envelopeOutput < 0xff) {
						// see Alien.sid: "full envelopeOutput level" GATE off/on sequences within same
						// IRQ will cause undesireable overflow.. this might not be a problem in cycle accurate
						// emulations.. but here it is (we only see a 20ms snapshot)

						sidemu->osc[v].envelopeOutput= (sidemu->osc[v].envelopeOutput + 1) & 0xff;	// increase volume
					}
					sidemu->osc[v].exponential_counter = 0;
					if (sidemu->osc[v].envelopeOutput == 0xff) {
						sidemu->osc[v].envphase = Decay;
					}
				}
			}
			break;
		}
		case Decay: {                   	// Phase 1 : Decay
			if (triggerLFSR_Threshold(sidemu->osc[v].decay, &sidemu->osc[v].currentLFSR) && handleExponentialDelay(sidemu, v)) { 	// dec volume when threshold is reached
				if (!sidemu->osc[v].zero_lock) {
					if (sidemu->osc[v].envelopeOutput != sidemu->osc[v].sustain) {
						sidemu->osc[v].envelopeOutput= (sidemu->osc[v].envelopeOutput - 1) & 0xff;	// decrease volume
					} else {
						sidemu->osc[v].envphase = Sustain;
					}
				}
			}
			break;
		}
		case Sustain: {                        // Phase 2 : Sustain
			if (sidemu->osc[v].envelopeOutput != sidemu->osc[v].sustain) {
				sidemu->osc[v].envphase = Decay;
			}
			break;
		}
		case Release: {                          // Phase 3 : Release
			// this phase must be explicitly triggered by clearing the GATE bit.
			if (triggerLFSR_Threshold(sidemu->osc[v].release, &sidemu->osc[v].currentLFSR) && handleExponentialDelay(sidemu, v)) { 		// dec volume when threshold is reached
				if (!sidemu->osc[v].zero_lock) {
					sidemu->osc[v].envelopeOutput= (sidemu->osc[v].envelopeOutput - 1) & 0xff;	// decrease volume
				}
			}
			break;
		}
	}
	if ((sidemu->osc[v].envelopeOutput == 0) && (previousEnvelopeOutput > sidemu->osc[v].envelopeOutput)) {
		sidemu->osc[v].zero_lock = 1;	// new "attack" phase must be started to unlock
	}
}




// render a buffer of n samples with the actual register contents
void sid_render ( struct SidEmulation *sidemu, short *buffer, unsigned long len, int step )
{
    unsigned long bp;
    // step 1: convert the not easily processable sid registers into some
    //           more convenient and fast values (makes the thing much faster
    //          if you process more than 1 sample value at once)
    unsigned char v;
    for (v=0;v<3;v++) {
        sidemu->osc[v].pulse   = (sidemu->sid.v[v].pulse & 0xfff) << 16;
        sidemu->osc[v].filter  = get_bit(sidemu->sid.res_ftv,v);
        sidemu->osc[v].attack  = sidemu->envelope_counter_period[sidemu->sid.v[v].ad >> 4];		// threshhold to be reached before incrementing volume
        sidemu->osc[v].decay   = sidemu->envelope_counter_period[sidemu->sid.v[v].ad & 0xf];
		unsigned char sustain= sidemu->sid.v[v].sr >> 4;
        sidemu->osc[v].sustain = sustain<<4 | sustain;
        sidemu->osc[v].release = sidemu->envelope_counter_period[sidemu->sid.v[v].sr & 0xf];
        sidemu->osc[v].wave    = sidemu->sid.v[v].wave;
        sidemu->osc[v].freq    = ((unsigned long)sidemu->sid.v[v].freq)*sidemu->freqmul;
    }

#ifdef SID_USES_FILTER
	sidemu->filter.freq  = ((sidemu->sid.ffreqhi << 3) + (sidemu->sid.ffreqlo&0x7)) * sidemu->filtmul;
	sidemu->filter.freq <<= 1;

	if (sidemu->filter.freq>pfloat_ConvertFromInt(1)) {
		sidemu->filter.freq=pfloat_ConvertFromInt(1);
	}
	// the above line isnt correct at all - the problem is that the filter
	// works only up to rmxfreq/4 - this is sufficient for 44KHz but isnt
	// for 32KHz and lower - well, but sound quality is bad enough then to
	// neglect the fact that the filter doesnt come that high ;)
	sidemu->filter.l_ena = get_bit(sidemu->sid.ftp_vol,4);	// lowpass
	sidemu->filter.b_ena = get_bit(sidemu->sid.ftp_vol,5);	// bandpass
	sidemu->filter.h_ena = get_bit(sidemu->sid.ftp_vol,6);	// highpass
	sidemu->filter.v3ena = !get_bit(sidemu->sid.ftp_vol,7);	// chan3 off
	sidemu->filter.vol   = (sidemu->sid.ftp_vol & 0xf);
	//  filter.rez   = 1.0-0.04*(float)(sid.res_ftv >> 4);

	/* We precalculate part of the quick float operation, saves time in loop later */
	sidemu->filter.rez   = (pfloat_ConvertFromFloat(1.2f) -
		pfloat_ConvertFromFloat(0.04f)*(sidemu->sid.res_ftv >> 4)) >> 8;
#endif
	// now render the buffer
	for (bp=0;bp<len;bp+=step) {
		int outo=0;
		int outf=0;

		// step 2 : generate the two output signals (for filtered and non-
		//          filtered) from the osc/eg sections
		for (v=0;v<3;v++) {
			// update wave counter
			sidemu->osc[v].counter = (sidemu->osc[v].counter+sidemu->osc[v].freq) & 0xFFFFFFF;
			// reset counter / noise generator if TEST bit set (blocked at 0 as long as set)
			if (sidemu->osc[v].wave & 0x08) {
				// note: test bit has no influence on the envelope generator whatsoever
				sidemu->osc[v].counter  = 0;
				sidemu->osc[v].noisepos = 0;
				sidemu->osc[v].noiseval = 0xffffff;
			}
			unsigned char refosc = v?v-1:2;  // reference oscillator for sync/ring
			// sync oscillator to refosc if sync bit set
			if (sidemu->osc[v].wave & 0x02)
				if (sidemu->osc[refosc].counter < sidemu->osc[refosc].freq)
					sidemu->osc[v].counter = sidemu->osc[refosc].counter * sidemu->osc[v].freq / sidemu->osc[refosc].freq;
			// generate waveforms with really simple algorithms
			unsigned char tripos = (unsigned char) (sidemu->osc[v].counter>>19);
			unsigned char triout= tripos;
			if (sidemu->osc[v].counter>>27) {
				triout^=0xff;
			}
			unsigned char sawout = (unsigned char) (sidemu->osc[v].counter >> 20);
			unsigned char plsout = (unsigned char) ((sidemu->osc[v].counter > sidemu->osc[v].pulse)-1);
			if (sidemu->osc[v].wave&0x8) {
				// TEST (Bit 3): The TEST bit, when set to one, resets and locks oscillator 1 at zero
				// until the TEST bit is cleared. The noise waveform output of oscillator 1 is also
				// reset and the pulse waveform output is held at a DC level
				plsout= sidemu->level_DC;
			}

			if ((sidemu->osc[v].wave & 0x40) && (sidemu->osc[v].wave & 0x10)) {
				// note: correctly "Saw/Triangle should start from 0 and Pulse from FF".
				// see $50 waveform impl below.. (because the impl is just a hack, this
				// is an attempt to limit undesireable side effects and keep the original
				// impl unchanged as far as possible..)
				plsout ^= 0xff;
			}
			// generate noise waveform exactly as the SID does
			if (sidemu->osc[v].noisepos!=(sidemu->osc[v].counter>>23))
			{
				sidemu->osc[v].noisepos = sidemu->osc[v].counter >> 23;
				sidemu->osc[v].noiseval = (sidemu->osc[v].noiseval << 1) |
						(get_bit(sidemu->osc[v].noiseval,22) ^ get_bit(sidemu->osc[v].noiseval,17));
				// impl consistent with: http://www.sidmusic.org/sid/sidtech5.html
				// doc here is probably wrong: http://www.oxyron.de/html/registers_sid.html
				sidemu->osc[v].noiseout = (get_bit(sidemu->osc[v].noiseval,22) << 7) |
						(get_bit(sidemu->osc[v].noiseval,20) << 6) |
						(get_bit(sidemu->osc[v].noiseval,16) << 5) |
						(get_bit(sidemu->osc[v].noiseval,13) << 4) |
						(get_bit(sidemu->osc[v].noiseval,11) << 3) |
						(get_bit(sidemu->osc[v].noiseval, 7) << 2) |
						(get_bit(sidemu->osc[v].noiseval, 4) << 1) |
						(get_bit(sidemu->osc[v].noiseval, 2) << 0);
			}
			unsigned char nseout = sidemu->osc[v].noiseout;

			// modulate triangle wave if ringmod bit set
			if (sidemu->osc[v].wave & 0x04)
				if (sidemu->osc[refosc].counter < 0x8000000)
					triout^=0xff;

			// now mix the oscillators with an AND operation as stated in
			// the SID's reference manual - even if this is completely wrong.
			// well, at least, the $30 and $70 waveform sounds correct and there's
			// no real solution to do $50 and $60, so who cares.

			// => wothke: the above statement is nonsense: there are many songs that need $50!

			unsigned char outv=0xFF;
#ifdef SID_DEBUG
			if ((0x1 << v) & voiceEnableMask) {
#endif
				if ((sidemu->osc[v].wave & 0x40) && (sidemu->osc[v].wave & 0x10))  {
					// this is a poor man's impl for $50 waveform to improve playback of
					// songs like Kentilla.sid, Convincing.sid, etc

					unsigned char idx= tripos > 0x7f ? 0xff-tripos : tripos;
					outv &= pulseTriangleWavetable[idx];
					outv &= plsout;	// either on or off
				} else {
					int updated = 0;
					if ((sidemu->osc[v].wave & 0x10) && ++updated)  outv &= triout;
					if ((sidemu->osc[v].wave & 0x20) && ++updated)  outv &= sawout;
					if ((sidemu->osc[v].wave & 0x40) && ++updated) 	outv &= plsout;
					if ((sidemu->osc[v].wave & 0x80) && ++updated)  outv &= nseout;
					if (!updated) 	outv &= sidemu->level_DC;
				}
#ifdef SID_DEBUG
			} else {
				outv=level_DC;
			}
#endif
#ifdef SID_USES_SAMPLE_ENV_COUNTER
			// using samples
			simOneEnvelopeCycle(v);
#else
			// using cycles
			float c= sidemu->cyclesPerSample+sidemu->cycleOverflow;
			unsigned int cycles= (unsigned int)c;
			sidemu->cycleOverflow= c-cycles;

			for (int i= 0; i<cycles; i++) {
				simOneEnvelopeCycle(sidemu, v);
			}
#endif
			// now route the voice output to either the non-filtered or the
			// filtered channel and dont forget to blank out osc3 if desired

#ifdef SID_USES_FILTER
			if (((v<2) || sidemu->filter.v3ena) && !sidemu->sMuteVoice[v]) {
				if (sidemu->osc[v].filter) {
					outf+=( ((int)(outv-0x80)) * (int)((sidemu->osc[v].envelopeOutput)) ) >>6;
				} else {
					outo+=( ((int)(outv-0x80)) * (int)((sidemu->osc[v].envelopeOutput)) ) >>6;
				}
			}
#else
			// Don't use filters, just mix all voices together
			if (!sMuteVoice[v]) outf+= (int)(((signed short)(outv-0x80)) * (sidemu->osc[v].envelopeOutput));
#endif
		}

#ifdef SID_USES_FILTER
		// step 3
		// so, now theres finally time to apply the multi-mode resonant filter
		// to the signal. The easiest thing is just modelling a real electronic
		// filter circuit instead of fiddling around with complex IIRs or even
		// FIRs ...
		// it sounds as good as them or maybe better and needs only 3 MULs and
		// 4 ADDs for EVERYTHING. SIDPlay uses this kind of filter, too, but
		// Mage messed the whole thing completely up - as the rest of the
		// emulator.
		// This filter sounds a lot like the 8580, as the low-quality, dirty
		// sound of the 6581 is uuh too hard to achieve :)

		sidemu->filter.h = pfloat_ConvertFromInt(outf) - (sidemu->filter.b>>8)*sidemu->filter.rez - sidemu->filter.l;
		sidemu->filter.b += pfloat_Multiply(sidemu->filter.freq, sidemu->filter.h);
		sidemu->filter.l += pfloat_Multiply(sidemu->filter.freq, sidemu->filter.b);

		if (sidemu->filter.l_ena || sidemu->filter.b_ena || sidemu->filter.h_ena) {
			// voice may be routed through filter without actually using any
			// filters.. e.g. Dancing_in_the_Moonshine.sid
			outf = 0;

			if (sidemu->filter.l_ena) outf+=pfloat_ConvertToInt(sidemu->filter.l);
			if (sidemu->filter.b_ena) outf+=pfloat_ConvertToInt(sidemu->filter.b);
			if (sidemu->filter.h_ena) outf+=pfloat_ConvertToInt(sidemu->filter.h);
		}

		int final_sample = (sidemu->filter.vol*(outo+outf));
#else
		int final_sample = outf>>2;
#endif

		// final_sample= generatePsidDigi(final_sample);	// PSID stuff

		// Clipping
		const int clipValue = 32767;
		if ( final_sample < -clipValue ) {
			final_sample = -clipValue;
		} else if ( final_sample > clipValue ) {
			final_sample = clipValue;
		}

		short out= final_sample;
		*(buffer+bp)= out;
    }
}

#ifdef SID_DEBUG
static char hex1 [16]= {'0','1','2','3','4','5','6','7','8','9','a','b','c','d','e','f'};
static char *pokeInfo;

static void traceSidPoke(int reg, unsigned char val) {
	pokeInfo= malloc(sizeof(char)*13);

	pokeInfo[0]= hex1[(sFrameCount>>12)&0xf];
	pokeInfo[1]= hex1[(sFrameCount>>8)&0xf];
	pokeInfo[2]= hex1[(sFrameCount>>4)&0xf];
	pokeInfo[3]= hex1[(sFrameCount&0xf)];
	pokeInfo[4]= ' ';
	pokeInfo[5]= 'D';
	pokeInfo[6]= '4';
	pokeInfo[7]= hex1[(reg>>4)];
	pokeInfo[8]= hex1[(reg&0xf)];
	pokeInfo[9]= ' ';
	pokeInfo[10]= hex1[(val>>4)];
	pokeInfo[11]= hex1[(val&0xf)];

	fprintf(stderr, "%s\n", pokeInfo);
	free(pokeInfo);
}
#endif

#if 0
static unsigned long getFrameCount() {
	return sFrameCount;
}

static void incFrameCount() {
	sFrameCount++;
}
#endif


//
// Poke a value into the sid register
//
void sid_write_reg ( struct SidEmulation *sidemu, int reg, unsigned char val )
{
	int voice=0;
#ifdef SID_DEBUG
	// if (sTraceon) traceSidPoke(reg, val);
#endif
	if (reg < 7) {}
	if ((reg >= 7) && (reg <=13)) {voice=1; reg-=7;}
	if ((reg >= 14) && (reg <=20)) {voice=2; reg-=14;}

	switch (reg) {
		case 0:	// Set frequency: Low byte
			sidemu->sid.v[voice].freq = (sidemu->sid.v[voice].freq&0xff00) | val;
			break;
		case 1:	// Set frequency: High byte
			sidemu->sid.v[voice].freq = (sidemu->sid.v[voice].freq&0xff) | (val<<8);
			break;
		case 2:	// Set pulse width: Low byte
			sidemu->sid.v[voice].pulse = (sidemu->sid.v[voice].pulse&0x0f00) | val;
			break;
		case 3:	// Set pulse width: High byte
			sidemu->sid.v[voice].pulse = (sidemu->sid.v[voice].pulse&0xff) | ((val & 0xf)<<8);
			break;
		case 4: {
			unsigned char oldGate= sidemu->sid.v[voice].wave&0x1;
			unsigned char oldTest= sidemu->sid.v[voice].wave&0x8;		// oscillator stop
			unsigned char newGate= val & 0x01;
			sidemu->sid.v[voice].wave = val;
			// poor man's ADSR-bug detection: this is the kind of logic a player would most
			// likely be using to deliberately trigger the the counter overflow..
			if (oldTest && (val&0x8) && !oldGate && newGate) {
				sidemu->sAdsrBugTriggerTime= sidemu->sCycles;
				sidemu->sAdsrBugFrameCount= getFrameCount();
			}
			if (!oldGate && newGate) {
				// If the envelope is then gated again (before the RELEASE cycle has reached
				// zero amplitude), another ATTACK cycle will begin, starting from whatever amplitude had been reached.
				sidemu->osc[voice].envphase= Attack;
				sidemu->osc[voice].zero_lock= 0;
			} else if (oldGate && !newGate) {
				// if the gate bit is reset before the envelope has finished the ATTACK cycle,
				// the RELEASE cycles will immediately begin, starting from whatever amplitude had been reached
				// see http://www.sidmusic.org/sid/sidtech2.html
				sidemu->osc[voice].envphase= Release;
			}
			}
			break;
		case 5:
			sidemu->sid.v[voice].ad = val;
			// ADSR-bug: if somebody goes through the above TEST/GATE drill and shortly thereafter
			// sets up an A/D that is bound to already have run over then we can be pretty sure what he is after..
			if (sidemu->sAdsrBugFrameCount == getFrameCount()) {				// limitation: only works within the same frame
				int delay= sidemu->envelope_counter_period_clck[val >> 4];
				if ((sidemu->sCycles-sidemu->sAdsrBugTriggerTime) > delay ) {
					sidemu->osc[voice].currentLFSR= clocksToSamples(delay);	// force ARSR-bug by setting counter higher than the threshold
				}
				sidemu->sAdsrBugTriggerTime= 0;
			}
			break;
		case  6: sidemu->sid.v[voice].sr = val;	break;
		case 21: sidemu->sid.ffreqlo = val;	break;
		case 22: sidemu->sid.ffreqhi = val;	break;
		case 23: sidemu->sid.res_ftv = val;	break;
		case 24: sidemu->sid.ftp_vol = val; 	break;
	}
}



#if 0
static void simOsc3Polling(unsigned short ad) {
	// handle busy polling for sid oscillator3 (e.g. Ring_Ring_Ring.sid)
	if ((ad == 0xd41b) && (memory[pc] == 0xd0) && (memory[pc+1] == 0xfb) /*BEQ above read*/) {
		unsigned int t=(16777216/sid.v[2].freq)>>8; // cycles per 1 osc step up (if waveform is "sawtooth")
		unsigned long usedCycles= sidemu->sCycles;
		if (sLastPolledOsc < usedCycles) {
			usedCycles-= sLastPolledOsc;
		}
		if (usedCycles<t) {
			sidemu->sCycles+= (t-usedCycles);	// sim busywait	(just give them evenly spaced signals)
		}
		sLastPolledOsc= sidemu->sCycles;
		io_area[0x041b]+= 1;	// this hack should at least avoid endless loops
	}
}
#endif

static void resetEngine ( struct SidEmulation *sidemu, unsigned long mixfrq, unsigned level_dc )
{
	sidemu->mixing_frequency = mixfrq;
	sidemu->freqmul = 15872000 / mixfrq;
	sidemu->filtmul = pfloat_ConvertFromFloat(21.5332031f)/mixfrq;
	memset((unsigned char*)&sidemu->sid,0,sizeof(struct SidRegisters));
	memset((unsigned char*)&sidemu->osc[0],0,sizeof(struct SidOsc));
	memset((unsigned char*)&sidemu->osc[1],0,sizeof(struct SidOsc));
	memset((unsigned char*)&sidemu->osc[2],0,sizeof(struct SidOsc));
	memset((unsigned char*)&sidemu->filter,0,sizeof(struct SidFilter));
	int i;
	for (i=0;i<3;i++) {
		// note: by default the rest of sid, osc & filter
		// above is set to 0
		sidemu->osc[i].envphase= Release;
		sidemu->osc[i].zero_lock= 1;
		sidemu->osc[i].noiseval = 0xffffff;
		sidemu->sMuteVoice[i]= 0;
	}
	sidemu->bval= 0;
	sidemu->wval= 0;
	// status
	sidemu->sFrameCount= 0;
	sidemu->sCycles= 0;
	sidemu->sAdsrBugTriggerTime= 0;
	sidemu->sAdsrBugFrameCount= 0;
	sidemu->cyclesPerSample=0;
	sidemu->cycleOverflow=0;
	// 0x38: supposedly DC level for MOS6581 (whereas it would be 0x80 for the "crappy new chip")
	sidemu->level_DC = level_dc;
#ifdef SID_DEBUG
	sidemu->voiceEnableMask = 0x7;        // for debugging: allows to mute certain voices..
#endif
	sidemu->limit_LFSR = 0;
	sidemu->sLastFrameCount = 0;
	// reset hacks
	//sCiaNmiVectorHack= 0;
	//sDummyDC04= 0;
	//sFake_d012_count= 0;
	//sFake_d012_loop= 0;
	sidemu->sLastPolledOsc= 0;
}




//   initialize SID and frequency dependant values
void sid_init ( struct SidEmulation *sidemu, unsigned long mixfrq )
{
	resetEngine(sidemu, mixfrq, SID_DC_LEVEL);
	//resetPsidDigi();
	// envelope-generator stuff
	unsigned long cyclesPerSec= getCyclesPerSec();
	sidemu->cycleOverflow= 0;
	sidemu->cyclesPerSample= ((float)cyclesPerSec/mixfrq);
	// in regular SID, 15-bit LFSR counter counts cpu-clocks, our problem is the lack of cycle by cycle
	// SID emulation (we only have a SID snapshot every 20ms to work with) during rendering our computing
	// granularity then is 'one sample' (not 'one cpu cycle' - but around 20).. instead of still trying to simulate a
	// 15-bit cycle-counter we may directly use a sample-counter instead (which also reduces rounding issues).
	int i;
#ifdef SID_USES_SAMPLE_ENV_COUNTER
	limit_LFSR= round(((float)0x8000)/sidemu->cyclesPerSample);	// original counter was 15-bit
	for (i=0; i<16; i++) {
		// counter must reach respective threshold before envelope value is incremented/decremented
		sidemu->envelope_counter_period[i]= (int)round((float)(attackTimes[i]*cyclesPerSec)/1000/256/cyclesPerSample)+1;	// in samples
		sidemu->envelope_counter_period_clck[i]= (int)round((float)(attackTimes[i]*cyclesPerSec)/1000/256)+1;				// in clocks
	}
#else
	sidemu->limit_LFSR= 0x8000;	// counter 15-bit
	for (i=0;i<16;i++) {
		// counter must reach respective threshold before envelope value is incremented/decremented
		sidemu->envelope_counter_period[i]=      (int)floor((float)(attackTimes[i]*cyclesPerSec)/1000/256)+1;	// in samples
		sidemu->envelope_counter_period_clck[i]= (int)floor((float)(attackTimes[i]*cyclesPerSec)/1000/256)+1;	// in clocks
	}
#endif
	// lookup table for decay rates
	unsigned char from[] =  {93, 54, 26, 14,  6,  0};
	unsigned char val[] = { 1,  2,  4,  8, 16, 30};
	for (i= 0; i<256; i++) {
		unsigned char v= 1;
		for (unsigned char j= 0; j<6; j++) {
			if (i>from[j]) {
				v= val[j];
				break;
			}
		}
		exponential_delays[i]= v;
	}
}
