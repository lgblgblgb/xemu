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


/* Important WARNING:
 * This is a trap! If you have something here with '#ifdef', it's quite possible that the macro is
 * not defined here, but defined elsewhere, thus the emulator sees totally different structs for
 * real but the problem is hidden! That is, be very careful at configdb_st (the type definition
 * itself also at the usage!) that should be only dependent on macros defined in xemu-target.h,
 * since that header file is always included by the build system, at command line level. */

#include "xemu/emutools.h"
#include "xemu/emutools_config.h"
#include "configdb.h"

#include "mega65.h"
#include "xemu/emutools_hid.h"
#include "audio65.h"

struct configdb_st configdb;


static const struct xemutools_configdef_str_st str_options[] = {
	{ "8",		NULL, "Path of EXTERNAL D81 disk image (not on/the SD-image) on drive 8", &configdb.disk8 },
	{ "9",		NULL, "Path of ALWAYS EXTERNAL D81 disk image on drive 9", &configdb.disk9 },
	{ "fpga",	NULL, "Comma separated list of FPGA-board switches turned ON", &configdb.fpga },
	{ "kickup",	NULL, "Override path of external KickStart to be used", &configdb.kickup },
	{ "kickuplist",	NULL, "Set path of symbol list file for external KickStart", &configdb.kickuplist },
	{ "loadbanner",	NULL, "Load initial memory content for banner", &configdb.loadbanner },
	{ "loadc000",	NULL, "Load initial memory content at $C000 (usually disk mounter)", &configdb.loadc000 },
	{ "loadcram",	NULL, "Load initial content (32K) into the colour RAM", &configdb.loadcram },
	{ "loadrom",	NULL, "Preload C65 ROM image (you may need the -forcerom option to prevent KickStart to re-load from SD)", &configdb.loadrom },
	{ "prg",	NULL, "Load a PRG file directly into the memory (/w C64/65 auto-detection on load address)", &configdb.prg },
	{ "sdimg",	SDCARD_NAME, "Override path of SD-image to be used (also see the -virtsd option!)", &configdb.sdimg },
	{ "dumpmem",	NULL, "Save memory content on exit", &configdb.dumpmem },
#ifdef XEMU_SNAPSHOT_SUPPORT
	{ "snapload",	NULL, "Load a snapshot from the given file", &configdb.snapload },
	{ "snapsave",	NULL, "Save a snapshot into the given file before Xemu would exit", &configdb.snapsave },
#endif
#ifdef HAS_UARTMON_SUPPORT
	{ "uartmon",	NULL, "Sets the name for named unix-domain socket for uartmon, otherwise disabled", &configdb.uartmon },
#endif
#ifdef HAVE_XEMU_INSTALLER
	{ "installer",	NULL, "Sets a download-specification descriptor file for auto-downloading data files", &configdb.installer },
#endif
#ifdef HAVE_ETHERTAP
	{ "ethertap",	NULL, "Enable ethernet emulation, parameter is the already configured TAP device name", &configdb.ethertap },
#endif
#ifdef HID_KBD_MAP_CFG_SUPPORT
	{ "keymap",	KEYMAP_USER_FILENAME, "Set keymap configuration file to be used", &configdb.keymap },
#endif
	{ "gui",	NULL, "Select GUI type for usage. Specify some insane str to get a list", &configdb.selectedgui },
	{ NULL }
};

static const struct xemutools_configdef_switch_st switch_options[] = {
	{ "driveled", "Render drive LED at the top right corner of the screen", &configdb.show_drive_led },
	{ "allowmousegrab", "Allow auto mouse grab with left-click", &allow_mouse_grab },
	{ "fullscreen", "Start in fullscreen mode", &configdb.fullscreen_requested },
	{ "hyperdebug", "Crazy, VERY slow and 'spammy' hypervisor debug mode", &configdb.hyperdebug },
	{ "hyperserialascii", "Convert PETSCII/ASCII hypervisor serial debug output to ASCII upper-case", &configdb.hyperserialascii },
	{ "forcerom", "Re-fill 'ROM' from external source on start-up, requires option -loadrom <filename>", &configdb.forcerom },
	{ "fontrefresh", "Upload character ROM from the loaded ROM image", &configdb.force_upload_fonts },
#ifdef VIRTUAL_DISK_IMAGE_SUPPORT
	{ "virtsd", "Interpret -sdimg option as a DIRECTORY to be fed onto the FAT32FS and use virtual-in-memory disk storage.", &configdb.virtsd },
#endif
#ifdef FAKE_TYPING_SUPPORT
	{ "go64", "Go into C64 mode after start (with auto-typing, can be combined with -autoload)", &configdb.go64 },
	{ "autoload", "Load and start the first program from disk (with auto-typing, can be combined with -go64)", &configdb.autoload },
#endif
	{ "syscon", "Keep system console open (Windows-specific effect only)", &configdb.syscon },
	{ "besure", "Skip asking \"are you sure?\" on RESET or EXIT", &i_am_sure_override },
	{ "skipunhandledmem", "Do not even ask on unhandled memory access (hides problems!!)", &configdb.skip_unhandled_mem },
	{ "fullborders", "Show non-clipped display borders", &configdb.fullborders },
	{ "nosound", "Disables audio output generation", &configdb.nosound },
	{ "noopl3", "Disables OPL3 emulation", &configdb.noopl3 },
	{ NULL }
};

static const struct xemutools_configdef_num_st num_options[] = {
	{ "dmarev", 2 + 0x100, "DMA revision (0/1/2=F018A/B/auto +256=autochange, +512=modulo, you always wants +256!)", &configdb.dmarev, 0, 1024 },
	{ "model", 0xFF, "Emulated MEGA65 model (255=custom/Xemu)", &configdb.mega65_model, 0, 0xFF },
	{ "kicked", 0x0, "Answer to KickStart upgrade (128=ask user in a pop-up window)", &configdb.kicked, 0, 0xFF },
	{ "prgmode", 0, "Override auto-detect option for -prg (64 or 65 for C64/C65 modes, 0 = default, auto detect)", &configdb.prgmode, 0, 65 },
	{ "rtchofs", 0, "RTC (and CIA TOD) default hour offset to real-time -24 ... 24 (for testing!)", &configdb.rtc_hour_offset, -24, 24 },
#ifdef HAVE_XEMU_UMON
	{ "umon", 0, "TCP-based dual mode (http / text) monitor port number [NOT YET WORKING]", &configdb.umon, 0, 0xFFFF },
#endif
	{ "sdlrenderquality", RENDER_SCALE_QUALITY, "Setting SDL hint for scaling method/quality on rendering (0, 1, 2)", &configdb.sdlrenderquality, 0, 2 },
	{ "stereoseparation", AUDIO_DEFAULT_SEPARATION, "Audio stereo separation; 100(hard-stereo) ... 0(mono) ... -100(hard-reversed-stereo); default: " STRINGIFY(AUDIO_DEFAULT_SEPARATION), &configdb.stereoseparation, -100, 100 },
	{ "mastervolume", AUDIO_DEFAULT_VOLUME, "Audio emulation mixing final volume (100=unchanged ... 0=silence); default: " STRINGIFY(AUDIO_DEFAULT_VOLUME), &configdb.mastervolume, 0, 100 },
	{ "forcevideostd", -1, "Force video standard (0 = PAL, 1 = NTSC, -1 = default switchable by VIC-IV)", &configdb.force_videostd, -1, 1 },
	// FIXME: as a workaround, I set this to "0" PAL, as newer MEGA65's default is this. HOWEVER this should be not handled this way but using a newer Hyppo!
	{ "initvideostd", 0, "Use given video standard as the startup one (0 = PAL, 1 = NTSC, -1 = Hyppo default)", &configdb.init_videostd, -1, 1 },
	{ "sidmask", 15, "Enabled SIDs of the four, in form of a bitmask", &configdb.sidmask, 0, 15 },
	{ "audiobuffersize", AUDIO_BUFFER_SAMPLES_DEFAULT, "Audio buffer size in BYTES", &configdb.audiobuffersize, AUDIO_BUFFER_SAMPLES_MIN, AUDIO_BUFFER_SAMPLES_MAX },
	{ NULL }
};

static const struct xemutools_configdef_float_st float_options[] = {
	{ "fastclock", MEGA65_DEFAULT_FAST_CLOCK, "Clock of M65 fast mode (in MHz)", &configdb.fast_mhz, 4.0, 200.0 },
	{ NULL }
};



void configdb_define_emulator_options ( size_t size )
{
	if (size != sizeof(configdb))
		FATAL("Xemu internal error: ConfigDB struct definition size mismatch");
	xemucfg_define_str_option_multi(str_options);
	xemucfg_define_switch_option_multi(switch_options);
	xemucfg_define_num_option_multi(num_options);
	xemucfg_define_float_option_multi(float_options);
}
