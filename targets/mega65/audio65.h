/* A work-in-progess MEGA65 (Commodore-65 clone origins) emulator
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

#ifndef XEMU_MEGA65_AUDIO65_H_INCLUDED
#define XEMU_MEGA65_AUDIO65_H_INCLUDED

#define NUMBER_OF_SIDS	4

#ifdef NEED_SID_H
#include "xemu/sid.h"
extern struct SidEmulation sid[NUMBER_OF_SIDS];
#endif

// You may want to disable audio emulation since it can disturb non-real-time emulation
#define AUDIO_EMULATION

#define AUDIO_DEFAULT_SEPARATION	60
#define AUDIO_DEFAULT_VOLUME		100
#define AUDIO_UNCHANGED_SEPARATION	-1000
#define AUDIO_UNCHANGED_VOLUME		-1000

extern int stereo_separation;

extern void audio65_init ( int sid_cycles_per_sec, int sound_mix_freq, int volume, int separation );
extern void audio65_reset ( void );
extern void audio65_start ( void );
extern void audio65_opl3_write ( Uint8 reg, Uint8 data );
extern void audio65_sid_write ( const int addr, const Uint8 data );
extern void audio65_sid_inc_framecount ( void );
extern void audio_set_stereo_parameters ( int vol, int sep );

#endif
