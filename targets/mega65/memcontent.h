/* !THIS IS A GENERATED FILE! DO NOT EDIT!
 * Instead, say 'make recreatememcontent' to re-generate this file
 * from binary data from the MEGA65 project. Please note, that MEGA65 is
 * an open source, GNU/GPL project, like Xemu. Thus, it's valid
 * to use binaries from it, as it's from the compiled version of MEGA65
 * which is available in source form at https://github.com/MEGA65/mega65-core
 * always, as per GNU/GPL. */

#ifndef XEMU_MEGA65_MEMCONTENT_H_INCLUDED
#define XEMU_MEGA65_MEMCONTENT_H_INCLUDED

// This must be incremented by ONE every time, when memcontent.c changes, or even
// if sdcontent.c is changed in a way to write new files, new content, or whatever
// to the SD-card as part of the "update system files" process. Edit this in the python generator though, not in this file!
#define MEMCONTENT_VERSION_ID 3

// Special structure array for system files update on the SD-image
struct meminitdata_sdfiles_st { const Uint8 *p; const char *fn; const int size; };
#define MEMINITDATA_SDFILES_ITEMS 13
extern const struct meminitdata_sdfiles_st meminitdata_sdfiles_db[MEMINITDATA_SDFILES_ITEMS];

// Generated as "chrwom" from file ../../../mega65-core-0.97-for-xemu/bin/charrom.bin ($1000 bytes)
#define MEMINITDATA_CHRWOM_SIZE 0x1000
extern const Uint8 meminitdata_chrwom[MEMINITDATA_CHRWOM_SIZE];

// Generated as "hickup" from file ../../../mega65-core-0.97-for-xemu/bin/HICKUP.M65 ($4000 bytes)
#define MEMINITDATA_HICKUP_SIZE 0x4000
extern const Uint8 meminitdata_hickup[MEMINITDATA_HICKUP_SIZE];

// Generated as "cramutils" from file ../../../mega65-core-0.97-for-xemu/bin/COLOURRAM.BIN ($8000 bytes)
#define MEMINITDATA_CRAMUTILS_SIZE 0x8000
extern const Uint8 meminitdata_cramutils[MEMINITDATA_CRAMUTILS_SIZE];

// Generated as "banner" from file ../../../mega65-core-0.97-for-xemu/sdcard-files/BANNER.M65 ($5300 bytes)
#define MEMINITDATA_BANNER_SIZE 0x5300
extern const Uint8 meminitdata_banner[MEMINITDATA_BANNER_SIZE];

// Generated as "onboard" from file ../../../mega65-core-0.97-for-xemu/sdcard-files/ONBOARD.M65 ($3071 bytes)
#define MEMINITDATA_ONBOARD_SIZE 0x3071
extern const Uint8 meminitdata_onboard[MEMINITDATA_ONBOARD_SIZE];

// Generated as "initrom" from file ../../../mega65-core-0.97-for-xemu/src/open-roms/bin/mega65.rom ($20000 bytes)
#define MEMINITDATA_INITROM_SIZE 0x20000
extern const Uint8 meminitdata_initrom[MEMINITDATA_INITROM_SIZE];

// Generated as "freezer" from file ../../../mega65-core-0.97-for-xemu/sdcard-files/FREEZER.M65 ($607C bytes)
#define MEMINITDATA_FREEZER_SIZE 0x607C
extern const Uint8 meminitdata_freezer[MEMINITDATA_FREEZER_SIZE];

// Generated as "makedisk" from file ../../../mega65-core-0.97-for-xemu/sdcard-files/MAKEDISK.M65 ($5723 bytes)
#define MEMINITDATA_MAKEDISK_SIZE 0x5723
extern const Uint8 meminitdata_makedisk[MEMINITDATA_MAKEDISK_SIZE];

// Generated as "audiomix" from file ../../../mega65-core-0.97-for-xemu/sdcard-files/AUDIOMIX.M65 ($5A79 bytes)
#define MEMINITDATA_AUDIOMIX_SIZE 0x5A79
extern const Uint8 meminitdata_audiomix[MEMINITDATA_AUDIOMIX_SIZE];

// Generated as "c64thumb" from file ../../../mega65-core-0.97-for-xemu/sdcard-files/C64THUMB.M65 ($1BC0 bytes)
#define MEMINITDATA_C64THUMB_SIZE 0x1BC0
extern const Uint8 meminitdata_c64thumb[MEMINITDATA_C64THUMB_SIZE];

// Generated as "c65thumb" from file ../../../mega65-core-0.97-for-xemu/sdcard-files/C65THUMB.M65 ($2800 bytes)
#define MEMINITDATA_C65THUMB_SIZE 0x2800
extern const Uint8 meminitdata_c65thumb[MEMINITDATA_C65THUMB_SIZE];

// Generated as "romload" from file ../../../mega65-core-0.97-for-xemu/sdcard-files/ROMLOAD.M65 ($43E2 bytes)
#define MEMINITDATA_ROMLOAD_SIZE 0x43E2
extern const Uint8 meminitdata_romload[MEMINITDATA_ROMLOAD_SIZE];

// Generated as "sprited" from file ../../../mega65-core-0.97-for-xemu/sdcard-files/SPRITED.M65 ($7928 bytes)
#define MEMINITDATA_SPRITED_SIZE 0x7928
extern const Uint8 meminitdata_sprited[MEMINITDATA_SPRITED_SIZE];

// Generated as "m65thumb" from file ../../../mega65-core-0.97-for-xemu/sdcard-files/M65THUMB.M65 ($1940 bytes)
#define MEMINITDATA_M65THUMB_SIZE 0x1940
extern const Uint8 meminitdata_m65thumb[MEMINITDATA_M65THUMB_SIZE];

// Generated as "monitor" from file ../../../mega65-core-0.97-for-xemu/sdcard-files/MONITOR.M65 ($4732 bytes)
#define MEMINITDATA_MONITOR_SIZE 0x4732
extern const Uint8 meminitdata_monitor[MEMINITDATA_MONITOR_SIZE];

// Generated as "ethload" from file ../../../mega65-core-0.97-for-xemu/sdcard-files/ETHLOAD.M65 ($1F5 bytes)
#define MEMINITDATA_ETHLOAD_SIZE 0x1F5
extern const Uint8 meminitdata_ethload[MEMINITDATA_ETHLOAD_SIZE];

// Generated as "megainfo" from file ../../../mega65-core-0.97-for-xemu/sdcard-files/MEGAINFO.M65 ($55CE bytes)
#define MEMINITDATA_MEGAINFO_SIZE 0x55CE
extern const Uint8 meminitdata_megainfo[MEMINITDATA_MEGAINFO_SIZE];

#endif
