#ifndef __XEMU_MEGA65_MEMCONTENT_H_INCLUDED
#define __XEMU_MEGA65_MEMCONTENT_H_INCLUDED

#define MEMINITDATA_KICKSTART_SIZE	0x4000
#define MEMINITDATA_CHRWOM_SIZE		0x1000
#define MEMINITDATA_CRAMUTILS_SIZE	0x8000
#define MEMINITDATA_BANNER_SIZE		21248

extern Uint8 meminitdata_kickstart[MEMINITDATA_KICKSTART_SIZE];
extern Uint8 meminitdata_chrwom[MEMINITDATA_CHRWOM_SIZE];
extern Uint8 meminitdata_cramutils[MEMINITDATA_CRAMUTILS_SIZE];
extern Uint8 meminitdata_banner[MEMINITDATA_BANNER_SIZE];
extern Uint8 meminitdata_freezer[];

#endif
