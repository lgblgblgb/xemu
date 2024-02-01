/* A work-in-progess MEGA65 (Commodore 65 clone origins) emulator
   Part of the Xemu project, please visit: https://github.com/lgblgblgb/xemu
   Copyright (C)2016-2024 LGB (Gábor Lénárt) <lgblgblgb@gmail.com>

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
	{ "hickup",	NULL, "Use external HICKUP", &configdb.hickup },
	{ "hickuprep",	NULL, "Set path of hyper-debug REP file for external HICKUP", &configdb.hickuprep },
	{ "extbanner",	NULL, "Use external initial memory content for banner", &configdb.extbanner },
	{ "extcramutils",NULL,"Use external initial content (32K) into the colour RAM (\"cram-utils\")", &configdb.extcramutils },
	{ "extinitrom",	NULL, "Use external init-ROM. Beware: this is not the normal ROM you may think!", &configdb.extinitrom },
	{ "extchrwom",	NULL, "Use external initial memory content for char-WOM (WriteOnlyMemory)", &configdb.extchrwom },
	{ "extflashutil",NULL,"Use external initial memory content for flashing utility", &configdb.extflashutil },
	{ "extonboard",	NULL, "Use external initial memory content for the onboarding utility", &configdb.extonboard },
	{ "extfreezer",	NULL, "Use external initial memory content for the Freezer", &configdb.extfreezer },
	{ "hdosdir",	NULL, "Set directory with HyppoDOS redirections", &configdb.hdosdir },
	{ "defaultdir",	NULL, "Set initial default directory for most file selector UIs", &configdb.defaultdir },
	{ "hyperserialfile", NULL, "Use a file to write serial output into (no ASCII conversion, not even with -hyperserialascii)", &configdb.hyperserialfile },
	{ "rom",	NULL, "Override Hyppo's loaded ROM during booting.", &configdb.rom },
	{ "prg",	NULL, "Load a PRG file directly into the memory (/w C64/65 auto-detection on load address)", &configdb.prg },
	{ "sdimg",	SDCARD_NAME, "Override path of SD-image to be used (also see the -virtsd option!)", &configdb.sdimg },
	{ "dumpmem",	NULL, "Save memory content on exit", &configdb.dumpmem },
	{ "dumpscreen",	NULL, "Save screen content (ASCII) on exit", &configdb.dumpscreen },
	{ "screenshot",	NULL, "Save screenshot (PNG) on exit and vice-versa (for testing!)", &configdb.screenshot_and_exit },
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
	{ "importbas",	NULL, "Import and RUN BASIC65 program from TEXT file", &configdb.importbas },
	{ "cartbin8000",NULL, "Load binary cartridge image from $8000", &configdb.cartbin8000 },
	{ "winpos",	NULL, "Window position: x,y (integers)", &configdb.winpos },
	{ NULL }
};

static const struct xemutools_configdef_switch_st switch_options[] = {
	{ "headless", "Run in headless mode (for testing!)", &emu_is_headless },
	{ "sleepless", "Use maximum emulation speed (for testing!)", &emu_is_sleepless },
	{ "cpusinglestep", "Force CPU emulation to do single step emulation (slower!)", &configdb.cpusinglestep },
	{ "hdosvirt", "Virtualize HDOS file access functions, but via only traps", &configdb.hdosvirt },
	{ "driveled", "Render drive LED at the top right corner of the screen", &configdb.show_drive_led },
	{ "allowmousegrab", "Allow auto mouse grab with left-click", &allow_mouse_grab },
	{ "allowfreezer", "Allow triggering freezer [NOT YET WORKING]", &configdb.allowfreezer },
	{ "fullscreen", "Start in fullscreen mode", &configdb.fullscreen_requested },
	{ "hyperdebug", "Crazy, VERY slow and 'spammy' hypervisor debug mode", &configdb.hyperdebug },
	{ "hyperdebugfrz", "Only start hyperdebug on entering the freezer", &configdb.hyperdebugfreezer },
	{ "hyperserialascii", "Convert PETSCII/ASCII hypervisor serial debug output to ASCII upper-case", &configdb.hyperserialascii },
	{ "usestubrom", "Use Xemu's internal stub-rom", &configdb.usestubrom },
	{ "useinitrom", "Use Xemu's internal init-rom", &configdb.useinitrom },
	{ "useutilmenu", "Try to trigger utility menu access on boot", &configdb.useutilmenu },
	{ "romfromsd", "Force ROM to be used from SD-card", &configdb.romfromsd },
	{ "defd81fromsd", "Force default D81 to be used from SD-card", &configdb.defd81fromsd },
	{ "testing", "Turn on features allows program to do privileged things", &configdb.testing },
#ifdef VIRTUAL_DISK_IMAGE_SUPPORT
	{ "virtsd", "Interpret -sdimg option as a DIRECTORY to be fed onto the FAT32FS and use virtual-in-memory disk storage.", &configdb.virtsd },
#endif
	{ "go64", "Go into C64 mode after start (with auto-typing, can be combined with -autoload)", &configdb.go64 },
	{ "autoload", "Load and start the first program from disk (with auto-typing, can be combined with -go64)", &configdb.autoload },
	{ "syscon", "Keep system console open (Windows-specific effect only)", &configdb.syscon },
	{ "besure", "Skip asking \"are you sure?\" on RESET or EXIT", &i_am_sure_override },
	{ "skipunhandledmem", "Do not even ask on unhandled memory access (hides problems!!)", &configdb.skip_unhandled_mem },
	{ "fullborders", "Show non-clipped display borders", &configdb.fullborders },
	{ "nosound", "Disables audio output generation", &configdb.nosound },
	{ "noopl3", "Disables OPL3 emulation", &configdb.noopl3 },
	{ "lockvideostd", "Lock video standard (programs cannot change it)", &configdb.lock_videostd },
	{ "curskeyjoy", "Cursor keys as joystick [makes your emulator unsable to move cursor in BASIC/etc!]", &hid_joy_on_cursor_keys },
	{ "showscanlines", "Show scanlines in V200 modes", &configdb.show_scanlines },
	{ "allowscanlines", "Allow user programs to control scanline visibility", &configdb.allow_scanlines },
	{ "fastboot", "Try to use sleepless emulation mode during booting", &configdb.fastboot },
	{ "matrixstart", "Start with matrix-mode activated", &configdb.matrixstart },
	{ "matrixdisable", "Disable the matrix hotkey", &configdb.matrixdisable },
	{ NULL }
};

static const struct xemutools_configdef_num_st num_options[] = {
	{ "model", 3, "Emulated MEGA65 model ID", &configdb.mega65_model, 0, 0xFF },
	{ "hicked", 0x0, "Answer to HICKUP upgrade (128=ask user in a pop-up window)", &configdb.hicked, 0, 0xFF },
	{ "prgmode", 0, "Override auto-detect option for -prg (64 or 65 for C64/C65 modes, 0 = default, auto detect)", &configdb.prgmode, 0, 65 },
	{ "rtchofs", 0, "RTC (and CIA TOD) default hour offset to real-time -24 ... 24 (for testing!)", &configdb.rtc_hour_offset, -24, 24 },
#ifdef HAVE_XEMU_UMON
	{ "umon", 0, "TCP-based dual mode (http / text) monitor port number [NOT YET WORKING]", &configdb.umon, 0, 0xFFFF },
#endif
	{ "sdlrenderquality", RENDER_SCALE_QUALITY, "Setting SDL hint for scaling method/quality on rendering (0, 1, 2)", &configdb.sdlrenderquality, 0, 2 },
	{ "stereoseparation", AUDIO_DEFAULT_SEPARATION, "Audio stereo separation; 100(hard-stereo) ... 0(mono) ... -100(hard-reversed-stereo); default: " STRINGIFY(AUDIO_DEFAULT_SEPARATION), &configdb.stereoseparation, -100, 100 },
	{ "mastervolume", AUDIO_DEFAULT_VOLUME, "Audio emulation mixing final volume (100=unchanged ... 0=silence); default: " STRINGIFY(AUDIO_DEFAULT_VOLUME), &configdb.mastervolume, 0, 100 },
	// FIXME: as a workaround, I set this to "0" PAL, as newer MEGA65's default is this. HOWEVER this should be not handled this way but using a newer Hyppo!
	{ "videostd", 0, "Use given video standard at startup/reset (0 = PAL, 1 = NTSC, -1 = Hyppo default)", &configdb.videostd, -1, 1 },
	{ "sidmask", 15, "Enabled SIDs of the four, in form of a bitmask", &configdb.sidmask, 0, 15 },
	{ "audiobuffersize", AUDIO_BUFFER_SAMPLES_DEFAULT, "Audio buffer size in BYTES", &configdb.audiobuffersize, AUDIO_BUFFER_SAMPLES_MIN, AUDIO_BUFFER_SAMPLES_MAX },
	{ "coloureffect", 0, "Colour effect to be applied to the SDL output (0=none, 1=grayscale, 2=green-monitor, ...)", &configdb.colour_effect, 0, 255 },
	{ NULL }
};

static const struct xemutools_configdef_float_st float_options[] = {
	{ "fastclock", MEGA65_DEFAULT_FAST_CLOCK, "Clock of M65 fast mode (in MHz)", &configdb.fast_mhz, 4.0, 200.0 },
	{ NULL }
};


// Options (given by the value pointers!) which SHOULD NOT BE saved when user saves their config.
// The list MUST BE closed with a NULL.
// The intent: some options makes sense mostly from using the command line (testing, called from scripts,
// etc), however if the user saves the config in Xemu when started this way, it would also save these
// CLI-given options, which is not the thing he wants, 99.999999% of time, I guess ...

static const void *do_not_save_opts[] = {
	&configdb.prg, &configdb.prgmode, &configdb.autoload, &configdb.go64, &configdb.hyperserialfile, &configdb.importbas,
	&emu_is_sleepless, &emu_is_headless, &configdb.testing,
	&configdb.matrixstart,
	&configdb.dumpmem, &configdb.dumpscreen, &configdb.screenshot_and_exit,
	&configdb.testing, &configdb.hyperdebug, &configdb.hyperdebugfreezer, &configdb.usestubrom, &configdb.useinitrom, &configdb.useutilmenu,
	&configdb.cartbin8000, &configdb.winpos,
	NULL
};


void configdb_define_emulator_options ( size_t size )
{
	if (size != sizeof(configdb))
		FATAL("Xemu internal error: ConfigDB struct definition size mismatch");
	xemucfg_define_str_option_multi(str_options);
	xemucfg_define_switch_option_multi(switch_options);
	xemucfg_define_num_option_multi(num_options);
	xemucfg_define_float_option_multi(float_options);
	xemucfg_add_flags_to_options(do_not_save_opts, XEMUCFG_FLAG_NO_SAVE);
}
