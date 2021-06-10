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

#ifndef __SIDENGINE_H_IS_INCLUDED
#define __SIDENGINE_H_IS_INCLUDED

#include <SDL_atomic.h>

// 0x38: supposedly DC level for MOS6581 (whereas it would be 0x80 for the "crappy new chip")
#define SID_DC_LEVEL 0x38

#define SID_USES_FILTER
//#define SID_DEBUG

// switch between 'cycle' or 'sample' based envelope-generator counter
// (performance wise it does not seem to make much difference)
//#define SID_USES_SAMPLE_ENV_COUNTER


typedef enum {
	Attack	= 0,
	Decay	= 1,
	Sustain	= 2,
	Release	= 3,
} EnvelopePhase;


#define NUMBER_OF_SID_REGISTERS_FOR_SNAPSHOT	0x20


struct SidEmulation {
	int sFrameCount;
	unsigned char writtenRegisterValues[NUMBER_OF_SID_REGISTERS_FOR_SNAPSHOT];
	struct SidRegisters {
		struct SidVoice {
			unsigned short freq;
			unsigned short pulse;
			unsigned char wave;
			unsigned char ad;
			unsigned char sr;
		} v[3];
		unsigned char ffreqlo;
		unsigned char ffreqhi;
		unsigned char res_ftv;
		unsigned char ftp_vol;
	} sid;
	struct SidOsc {						// internal oscillator def
		unsigned long freq;
		unsigned long pulse;
		unsigned char wave;
		unsigned char filter;
		unsigned long attack;
		unsigned long decay;
		unsigned long sustain;
		unsigned long release;
		unsigned long counter;
		// updated envelope generation based on reSID
		unsigned char envelopeOutput;
		signed int currentLFSR;				// sim counter
		unsigned char zero_lock;
		unsigned char exponential_counter;
		unsigned char envphase;
		unsigned long noisepos;
		unsigned long noiseval;
		unsigned char noiseout;
	} osc[3];
	struct SidFilter {					// internal filter def
		int freq;
		unsigned char  l_ena;
		unsigned char  b_ena;
		unsigned char  h_ena;
		unsigned char  v3ena;
		int vol;
		int rez;
		int h;
		int b;
		int l;
	} filter;
	unsigned int sMuteVoice[3];
	unsigned long sCycles;					// counter keeps track of burned cpu cycles
	unsigned long sAdsrBugTriggerTime;			// detection of ADSR-bug conditions
	unsigned long sAdsrBugFrameCount;
	float cyclesPerSample;
	float cycleOverflow;
	// 0x38: supposedly DC level for MOS6581 (whereas it would be 0x80 for the "crappy new chip")
	unsigned level_DC;
#ifdef SID_DEBUG
	unsigned char voiceEnableMask;				// for debugging: allows to mute certain voices..
#endif
	int limit_LFSR;						// the original cycle counter would be 15-bit (but we are counting samples & may rescale the counter accordingly)
	int envelope_counter_period[16];
	int envelope_counter_period_clck[16];
	unsigned long  mixing_frequency;
	unsigned long  freqmul;
	int  filtmul;
	unsigned long cyclesPerSec;
	unsigned long sLastFrameCount;
	unsigned char bval;
	unsigned short wval;
	unsigned long sLastPolledOsc;
	SDL_SpinLock spinlock;
};


extern void sid_write_reg ( struct SidEmulation *sidemu, int reg, unsigned char val );
extern void sid_init      ( struct SidEmulation *sidemu, unsigned long cyclesPerSec, unsigned long mixfrq );
extern void sid_render    ( struct SidEmulation *sidemu, short *buffer, unsigned long len, int step );

#ifdef XEMU_SNAPSHOT_SUPPORT
#include "xemu/emutools_basicdefs.h"
#include "xemu/emutools_snapshot.h"
extern int sid_snapshot_load_state ( const struct xemu_snapshot_definition_st *def , struct xemu_snapshot_block_st *block );
extern int sid_snapshot_save_state ( const struct xemu_snapshot_definition_st *def );
#endif

#endif
