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

#ifndef XEMU_MEGA65_CONFIGDB_H_INCLUDED
#define XEMU_MEGA65_CONFIGDB_H_INCLUDED

/* Important WARNING:
 * This is a trap! If you have something here with '#ifdef', it's quite possible that the macro is
 * not defined here, but defined elsewhere, thus the emulator sees totally different structs for
 * real but the problem is hidden! That is, be very careful at configdb_st (the type definition
 * itself also at the usage!) that should be only dependent on macros defined in xemu-target.h,
 * since that header file is always included by the build system, at command line level. */


struct configdb_st {
	int	fullscreen_requested;
	char	*disk8;
	char	*disk9;
	char	*fpga;
	char	*hickup;
	char	*hickuplist;
	char	*extbanner;
	char	*extcramutils;
	char	*extinitrom;
	char	*extchrwom;
	char	*rom;
	char	*prg;
	char	*sdimg;
	char	*dumpmem;
#ifdef XEMU_SNAPSHOT_SUPPORT
	char	*snapload;
	char	*snapsave;
#endif
#ifdef HAS_UARTMON_SUPPORT
	char	*uartmon;
#endif
#ifdef HAVE_XEMU_INSTALLER
	char	*installer;
#endif
#ifdef HAVE_ETHERTAP
	char	*ethertap;
#endif
#ifdef HID_KBD_MAP_CFG_SUPPORT
	char	*keymap;
#endif
	char	*selectedgui;
	int	force_videostd;
	int	init_videostd;
	int	fullborders;
	int	show_drive_led;
	int	hyperdebug;
	int	hyperserialascii;
	int	stubrom;
	int	initrom;
#ifdef VIRTUAL_DISK_IMAGE_SUPPORT
	int	virtsd;
#endif
#ifdef FAKE_TYPING_SUPPORT
	int	go64;
	int	autoload;
#endif
	int	skip_unhandled_mem;
	int	syscon;
	int	dmarev;
	int	mega65_model;		// $FF = Xemu/others, 1/2/3 = MEGA65 PCB rev 1/2/3, $40=nexys4, $41=nexys4ddr, $42=nexys4ddr-widget, $FD=wukong, $FE=simulation
	int	hicked;
	int	prgmode;
	int	rtc_hour_offset;
#ifdef HAVE_XEMU_UMON
	int	umon;
#endif
	int	sdlrenderquality;
	int	stereoseparation;
	int	mastervolume;
	double	fast_mhz;
	int	nosound;
	int	noopl3;
	int	sidmask;
	int	audiobuffersize;
};

extern struct configdb_st configdb;

extern void configdb_define_emulator_options ( size_t size );

#endif
