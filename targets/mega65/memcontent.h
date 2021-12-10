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
#define MEMCONTENT_VERSION_ID 1

#include "xemu/emutools.h"

// Special structure array for system files update on the SD-image
struct meminitdata_sdfiles_st { const Uint8 *p; const char *fn; const int size; };
#define MEMINITDATA_SDFILES_ITEMS 8
extern const struct meminitdata_sdfiles_st meminitdata_sdfiles_db[MEMINITDATA_SDFILES_ITEMS];

// Generated as "chrwom" from file ../../../mega65-core/bin/charrom.bin (4096 bytes)
#define MEMINITDATA_CHRWOM_SIZE 4096
extern Uint8 meminitdata_chrwom[MEMINITDATA_CHRWOM_SIZE];

// Generated as "hickup" from file ../../../mega65-core/bin/HICKUP.M65 (16384 bytes)
#define MEMINITDATA_HICKUP_SIZE 16384
extern Uint8 meminitdata_hickup[MEMINITDATA_HICKUP_SIZE];

// Generated as "cramutils" from file ../../../mega65-core/bin/COLOURRAM.BIN (32768 bytes)
#define MEMINITDATA_CRAMUTILS_SIZE 32768
extern Uint8 meminitdata_cramutils[MEMINITDATA_CRAMUTILS_SIZE];

// Generated as "banner" from file ../../../mega65-core/sdcard-files/BANNER.M65 (21248 bytes)
#define MEMINITDATA_BANNER_SIZE 21248
extern Uint8 meminitdata_banner[MEMINITDATA_BANNER_SIZE];

// Generated as "freezer" from file ../../../mega65-core/sdcard-files/FREEZER.M65 (31639 bytes)
#define MEMINITDATA_FREEZER_SIZE 31639
extern Uint8 meminitdata_freezer[MEMINITDATA_FREEZER_SIZE];

// Generated as "onboard" from file ../../../mega65-core/sdcard-files/ONBOARD.M65 (10406 bytes)
#define MEMINITDATA_ONBOARD_SIZE 10406
extern Uint8 meminitdata_onboard[MEMINITDATA_ONBOARD_SIZE];

// Generated as "openrom" from file ../../../mega65-core/src/open-roms/bin/mega65.rom (131072 bytes)
#define MEMINITDATA_OPENROM_SIZE 131072
extern Uint8 meminitdata_openrom[MEMINITDATA_OPENROM_SIZE];

// Generated as "audiomix" from file ../../../mega65-core/sdcard-files/AUDIOMIX.M65 (27355 bytes)
#define MEMINITDATA_AUDIOMIX_SIZE 27355
extern Uint8 meminitdata_audiomix[MEMINITDATA_AUDIOMIX_SIZE];

// Generated as "c64thumb" from file ../../../mega65-core/sdcard-files/C64THUMB.M65 (11776 bytes)
#define MEMINITDATA_C64THUMB_SIZE 11776
extern Uint8 meminitdata_c64thumb[MEMINITDATA_C64THUMB_SIZE];

// Generated as "c65thumb" from file ../../../mega65-core/sdcard-files/C65THUMB.M65 (9152 bytes)
#define MEMINITDATA_C65THUMB_SIZE 9152
extern Uint8 meminitdata_c65thumb[MEMINITDATA_C65THUMB_SIZE];

// Generated as "romload" from file ../../../mega65-core/sdcard-files/ROMLOAD.M65 (25864 bytes)
#define MEMINITDATA_ROMLOAD_SIZE 25864
extern Uint8 meminitdata_romload[MEMINITDATA_ROMLOAD_SIZE];

// Generated as "sprited" from file ../../../mega65-core/sdcard-files/SPRITED.M65 (33954 bytes)
#define MEMINITDATA_SPRITED_SIZE 33954
extern Uint8 meminitdata_sprited[MEMINITDATA_SPRITED_SIZE];

#endif
