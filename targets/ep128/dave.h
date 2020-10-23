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

#ifndef XEMU_EP128_DAVE_H_INCLUDED
#define XEMU_EP128_DAVE_H_INCLUDED

#define AUDIO_SOURCE_DAVE		0
#define AUDIO_SOURCE_PRINTER_COVOX	1
#define AUDIO_SOURCE_DTM_DAC4		2

extern int audio_source;
extern Uint8 dave_int_read;
//extern Uint8 kbd_matrix[16];
extern int kbd_selector, cpu_cycles_per_dave_tick, mem_wait_states;

extern void audio_init ( int enable );
extern void audio_start ( void );
extern void audio_stop ( void );
extern void audio_close ( void );

extern void dave_set_clock ( void );
extern void kbd_matrix_reset ( void );
extern void dave_reset ( void );
extern void dave_int1 ( int level );
extern void dave_tick ( void );
extern void dave_configure_interrupts ( Uint8 n );
extern void dave_write_audio_register ( Uint8 port, Uint8 value );

#endif
