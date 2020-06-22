/* A work-in-progess MEGA65 (Commodore 65 clone origins) emulator
   Part of the Xemu project, please visit: https://github.com/lgblgblgb/xemu
   Copyright (C)2016-2020 LGB (Gábor Lénárt) <lgblgblgb@gmail.com>

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

#ifdef SD_CONTENT_SUPPORT

#include "xemu/emutools.h"
#include "sdcontent.h"
#include "sdcard.h"
#include "fat32.h"
#include "mega65.h"
// to get D81_SIZE
#include "xemu/d81access.h"
#include "xemu/emutools_files.h"

#include "memcontent.h"

#include <string.h>
#include <sys/types.h>
#include <dirent.h>
#include <errno.h>
#include <sys/stat.h>
#include <unistd.h>

#define MAX_IMPORT_FILE_SIZE 1000000



static Uint8 block[512];
static int   our_data_partition;

static mfat_dirent_t sd_rootdirent;


// From mega65-fdisk
static const Uint8 fat_bytes[12] = {
	0xf8,0xff,0xff,0x0f,
	0xff,0xff,0xff,0x0f,
	0xf8,0xff,0xff,0x0f
};
static const Uint8 dir_bytes[15] = {0x08,0x00,0x00,0x53,0xae,0x93,0x4a,0x93,0x4a,0x00,0x00,0x53,0xae,0x93,0x4a};
static const Uint8 default_volume_name[11]	= "M.E.G.A.65!";
static const char  default_disk_image[]		= "mega65.d81";	// must be lower case
static const char  xemu_disk_image[]		= "external.d81";
static const Uint8 sys_part_magic[]		= {'M','E','G','A','6','5','S','Y','S','0','0'};
static const Uint8 boot_bytes[258] = {
	// Jump to boot code, required by most version of DOS
	0xeb, 0x58, 0x90,
	// OEM String: MEGA65r1
	0x4d, 0x45, 0x47, 0x41, 0x36, 0x35, 0x72, 0x31,
	// BIOS Parameter block.  We patch certain
	// values in here.
	0x00, 0x02,  // Sector size = 512 bytes
	0x08 , // Sectors per cluster
	/* 0x0e */ 0x38, 0x02,  // Number of reserved sectors (0x238 = 568)
	/* 0x10 */ 0x02, // Number of FATs
	0x00, 0x00, // Max directory entries for FAT12/16 (0 for FAT32)
	/* offset 0x13 */ 0x00, 0x00, // Total logical sectors (0 for FAT32)
	0xf8, // Disk type (0xF8 = hard disk)
	0x00, 0x00, // Sectors per FAT for FAT12/16 (0 for FAT32)
	/* offset 0x18 */ 0x00, 0x00, // Sectors per track (0 for LBA only)
	0x00, 0x00, // Number of heads for CHS drives, zero for LBA
	0x00, 0x00, 0x00, 0x00, // 32-bit Number of hidden sectors before partition. Should be 0 if logical sectors == 0
	/* 0x20 */ 0x00, 0xe8, 0x0f, 0x00, // 32-bit total logical sectors
	/* 0x24 */ 0xf8, 0x03, 0x00, 0x00, // Sectors per FAT
	/* 0x28 */ 0x00, 0x00, // Drive description
	/* 0x2a */ 0x00, 0x00, // Version 0.0
	/* 0x2c */ 0x02, 0x00 ,0x00, 0x00, // Number of first cluster
	/* 0x30 */ 0x01, 0x00, // Logical sector of FS Information sector
	/* 0x32 */ 0x06, 0x00, // Sector number of backup-copy of boot sector
	/* 0x34 */ 0x00, 0x00, 0x00, 0x00, // Filler bytes
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ,0x00, 0x00, // Filler bytes
	/* 0x40 */ 0x80, // Physical drive number
	/* 0x41 */ 0x00, // FAT12/16 use only
	/* 0x42 */ 0x29, // 0x29 == Extended Boot Signature
	/* 0x43 */ 0x6d, 0x66, 0x62, 0x61, // Volume ID "mfba"
	/* 0x47 */ 0x4d, 0x2e, 0x45, 0x2e, 0x47, // 11 byte volume label
	0x2e, 0x41 ,0x2e, 0x20, 0x36, 0x35,
	/* 0x52 */ 0x46, 0x41, 0x54, 0x33, 0x32, 0x20, 0x20, 0x20, // "FAT32   "
	// Boot loader code starts here
	0x0e, 0x1f, 0xbe, 0x77 ,0x7c, 0xac,
	0x22, 0xc0, 0x74, 0x0b, 0x56, 0xb4, 0x0e, 0xbb,
	0x07, 0x00, 0xcd, 0x10, 0x5e, 0xeb ,0xf0, 0x32,
	0xe4, 0xcd, 0x16, 0xcd, 0x19, 0xeb, 0xfe,
	// From here on is the non-bootable error message
	// 0x82 - 0x69 =
	0x4d, 0x45, 0x47, 0x41, 0x36, 0x35, 0x20,
	// 9-character name of operating system
	'H','Y','P','P','O','B','O','O','T',
	0x20, 0x56, 0x30, 0x30, 0x2e, 0x31, 0x31,
	0x0d, 0x0a, 0x0d, 0x3f, 0x4e, 0x4f, 0x20, 0x34,
	0x35, 0x47, 0x53, 0x30, 0x32, 0x2c, 0x20, 0x34,
	0x35, 0x31, 0x30, 0x2c, 0x20, 0x36, 0x35, 0x5b,
	0x63, 0x65, 0x5d, 0x30, 0x32, 0x2c, 0x20, 0x36,
	0x35, 0x31, 0x30, 0x20, 0x4f, 0x52, 0x20, 0x38,
	0x35, 0x31, 0x30, 0x20, 0x50, 0x52, 0x4f, 0x43,
	0x45, 0x53, 0x53, 0x4f, 0x52, 0x20, 0x20, 0x45,
	0x52, 0x52, 0x4f, 0x52, 0x0d, 0x0a, 0x49, 0x4e, 0x53,
	0x45, 0x52, 0x54, 0x20, 0x44, 0x49, 0x53, 0x4b,
	0x20, 0x49, 0x4e, 0x20, 0x52, 0x45, 0x41, 0x4c,
	0x20, 0x43, 0x4f, 0x4d, 0x50, 0x55, 0x54, 0x45,
	0x52, 0x20, 0x41, 0x4e, 0x44, 0x20, 0x54, 0x52,
	0x59, 0x20, 0x41, 0x47, 0x41, 0x49, 0x4e, 0x2e,
	0x0a, 0x0a, 0x52, 0x45, 0x41, 0x44, 0x59, 0x2e,
	0x0d, 0x0a
};
static const Uint8 d81_at_61800[] = {
	/* 61800 */ 0x28,0x03,0x44,0x00,0x44,0x45,0x4d,0x4f,0x45,0x4d,0x50,0x54,0x59,0xa0,0xa0,0xa0,	/* | (.D.DEMOEMPTY...| */
	/* 61810 */ 0xa0,0xa0,0xa0,0xa0,0xa0,0xa0,0x30,0x30,0xa0,0x33,0x44,0xa0,0xa0			/* | ......00.3D.....| */
};
static const Uint8 d81_at_61900[] = {
	/* 61900 */ 0x28,0x02,0x44,0xbb,0x30,0x30,0xc0,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,	/* | (.D.00..........| */
	/* 61910 */ 0x28,0xff,0xff,0xff,0xff,0xff,0x28,0xff,0xff,0xff,0xff,0xff,0x28,0xff,0xff,0xff,	/* | (.....(.....(...| */
	/* 61920 */ 0xff,0xff,0x28,0xff,0xff,0xff,0xff,0xff,0x28,0xff,0xff,0xff,0xff,0xff,0x28,0xff,	/* | ..(.....(.....(.| */
	/* 61930 */ 0xff,0xff,0xff,0xff,0x28,0xff,0xff,0xff,0xff,0xff,0x28,0xff,0xff,0xff,0xff,0xff,	/* | ....(.....(.....| */
	/* 61940 */ 0x28,0xff,0xff,0xff,0xff,0xff,0x28,0xff,0xff,0xff,0xff,0xff,0x28,0xff,0xff,0xff,	/* | (.....(.....(...| */
	/* 61950 */ 0xff,0xff,0x28,0xff,0xff,0xff,0xff,0xff,0x28,0xff,0xff,0xff,0xff,0xff,0x28,0xff,	/* | ..(.....(.....(.| */
	/* 61960 */ 0xff,0xff,0xff,0xff,0x28,0xff,0xff,0xff,0xff,0xff,0x28,0xff,0xff,0xff,0xff,0xff,	/* | ....(.....(.....| */
	/* 61970 */ 0x28,0xff,0xff,0xff,0xff,0xff,0x28,0xff,0xff,0xff,0xff,0xff,0x28,0xff,0xff,0xff,	/* | (.....(.....(...| */
	/* 61980 */ 0xff,0xff,0x28,0xff,0xff,0xff,0xff,0xff,0x28,0xff,0xff,0xff,0xff,0xff,0x28,0xff,	/* | ..(.....(.....(.| */
	/* 61990 */ 0xff,0xff,0xff,0xff,0x28,0xff,0xff,0xff,0xff,0xff,0x28,0xff,0xff,0xff,0xff,0xff,	/* | ....(.....(.....| */
	/* 619a0 */ 0x28,0xff,0xff,0xff,0xff,0xff,0x28,0xff,0xff,0xff,0xff,0xff,0x28,0xff,0xff,0xff,	/* | (.....(.....(...| */
	/* 619b0 */ 0xff,0xff,0x28,0xff,0xff,0xff,0xff,0xff,0x28,0xff,0xff,0xff,0xff,0xff,0x28,0xff,	/* | ..(.....(.....(.| */
	/* 619c0 */ 0xff,0xff,0xff,0xff,0x28,0xff,0xff,0xff,0xff,0xff,0x28,0xff,0xff,0xff,0xff,0xff,	/* | ....(.....(.....| */
	/* 619d0 */ 0x28,0xff,0xff,0xff,0xff,0xff,0x28,0xff,0xff,0xff,0xff,0xff,0x28,0xff,0xff,0xff,	/* | (.....(.....(...| */
	/* 619e0 */ 0xff,0xff,0x28,0xff,0xff,0xff,0xff,0xff,0x28,0xff,0xff,0xff,0xff,0xff,0x28,0xff,	/* | ..(.....(.....(.| */
	/* 619f0 */ 0xff,0xff,0xff,0xff,0x28,0xff,0xff,0xff,0xff,0xff,0x24,0xf0,0xff,0xff,0xff,0xff,	/* | ....(.....$.....| */
	/* 61a00 */ 0x00,0xff,0x44,0xbb,0x30,0x30,0xc0,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,	/* | ..D.00..........| */
	/* 61a10 */ 0x28,0xff,0xff,0xff,0xff,0xff,0x28,0xff,0xff,0xff,0xff,0xff,0x28,0xff,0xff,0xff,	/* | (.....(.....(...| */
	/* 61a20 */ 0xff,0xff,0x28,0xff,0xff,0xff,0xff,0xff,0x28,0xff,0xff,0xff,0xff,0xff,0x28,0xff,	/* | ..(.....(.....(.| */
	/* 61a30 */ 0xff,0xff,0xff,0xff,0x28,0xff,0xff,0xff,0xff,0xff,0x28,0xff,0xff,0xff,0xff,0xff,	/* | ....(.....(.....| */
	/* 61a40 */ 0x28,0xff,0xff,0xff,0xff,0xff,0x28,0xff,0xff,0xff,0xff,0xff,0x28,0xff,0xff,0xff,	/* | (.....(.....(...| */
	/* 61a50 */ 0xff,0xff,0x28,0xff,0xff,0xff,0xff,0xff,0x28,0xff,0xff,0xff,0xff,0xff,0x28,0xff,	/* | ..(.....(.....(.| */
	/* 61a60 */ 0xff,0xff,0xff,0xff,0x28,0xff,0xff,0xff,0xff,0xff,0x28,0xff,0xff,0xff,0xff,0xff,	/* | ....(.....(.....| */
	/* 61a70 */ 0x28,0xff,0xff,0xff,0xff,0xff,0x28,0xff,0xff,0xff,0xff,0xff,0x28,0xff,0xff,0xff,	/* | (.....(.....(...| */
	/* 61a80 */ 0xff,0xff,0x28,0xff,0xff,0xff,0xff,0xff,0x28,0xff,0xff,0xff,0xff,0xff,0x28,0xff,	/* | ..(.....(.....(.| */
	/* 61a90 */ 0xff,0xff,0xff,0xff,0x28,0xff,0xff,0xff,0xff,0xff,0x28,0xff,0xff,0xff,0xff,0xff,	/* | ....(.....(.....| */
	/* 61aa0 */ 0x28,0xff,0xff,0xff,0xff,0xff,0x28,0xff,0xff,0xff,0xff,0xff,0x28,0xff,0xff,0xff,	/* | (.....(.....(...| */
	/* 61ab0 */ 0xff,0xff,0x28,0xff,0xff,0xff,0xff,0xff,0x28,0xff,0xff,0xff,0xff,0xff,0x28,0xff,	/* | ..(.....(.....(.| */
	/* 61ac0 */ 0xff,0xff,0xff,0xff,0x28,0xff,0xff,0xff,0xff,0xff,0x28,0xff,0xff,0xff,0xff,0xff,	/* | ....(.....(.....| */
	/* 61ad0 */ 0x28,0xff,0xff,0xff,0xff,0xff,0x28,0xff,0xff,0xff,0xff,0xff,0x28,0xff,0xff,0xff,	/* | (.....(.....(...| */
	/* 61ae0 */ 0xff,0xff,0x28,0xff,0xff,0xff,0xff,0xff,0x28,0xff,0xff,0xff,0xff,0xff,0x28,0xff,	/* | ..(.....(.....(.| */
	/* 61af0 */ 0xff,0xff,0xff,0xff,0x28,0xff,0xff,0xff,0xff,0xff,0x28,0xff,0xff,0xff,0xff,0xff,	/* | ....(.....(.....| */
	/* 61b00 */ 0x00,0xff										/* | ................| */
};

#define ZERO_BUFFER()	memset(block,0,512)
#define WRITE_BLOCK(n)	do { if (sdcard_write_block(n,block)) return -1; } while(0)
#define WRITE_BLOCK_RANGE(n1,n2) do { for (Uint32 i = n1; i <= n2; i++) if (sdcard_write_block(i,block)) return -1; } while(0)

static inline void block_uint32 ( unsigned int ofs, Uint32 data )
{
	block[ofs    ] = (data      ) & 0xff;
	block[ofs + 1] = (data >>  8) & 0xff;
	block[ofs + 2] = (data >> 16) & 0xff;
	block[ofs + 3] = (data >> 24) & 0xff;
}
static inline void block_uint16 ( unsigned int ofs, Uint16 data )
{
	block[ofs    ] = (data      ) & 0xff;
	block[ofs + 1] = (data >>  8) & 0xff;
}


// Make a FAT32FS. Called by fdisk() function
// I copied the algorithm from mega65-fdisk closely (and hopefully well enough) not to have any compatibility issue.
static int mkfs_fat32 ( Uint32 start_block, Uint32 number_of_blocks )
{
	DEBUGPRINT("SDCARD: FAT32FS: creating file system between sectors $%X-$%X, $%X sectors (%uK) in size" NL, start_block, start_block + number_of_blocks - 1, number_of_blocks, number_of_blocks >> 11);
	// *** Let's do some calculations. According to mega65-fdisk, as usual in these mkfs parts :)
	const Uint8  sectors_per_cluster = 8; // 4Kbyte clusters
	//Uint32 reserved_sectors = 1024 * 1024 / 512;
	const Uint32 reserved_sectors = boot_bytes[0xE] + (boot_bytes[0xF] << 8);
	const Uint32 fat_available_sectors = number_of_blocks - reserved_sectors;
	Uint32 fs_clusters = fat_available_sectors / sectors_per_cluster;
	Uint32 fat_sectors = fs_clusters/(512 / 4);
	if (fs_clusters % (512 / 4))
		fat_sectors++;
	Uint32 sectors_required = 2 * fat_sectors+((fs_clusters - 2) * sectors_per_cluster);
	while (sectors_required > fat_available_sectors) {
		Uint32 excess_sectors = sectors_required - fat_available_sectors;
		Uint32 delta = (excess_sectors / (1 + sectors_per_cluster));
		if (delta < 1)
			delta = 1;
		fs_clusters -= delta;
		fat_sectors = fs_clusters / (512 / 4);
		if (fs_clusters % (512 / 4))
			fat_sectors++;
		sectors_required = 2 * fat_sectors + ((fs_clusters - 2) * sectors_per_cluster);
	}
	Uint32 fat1_sector = reserved_sectors;
	Uint32 fat2_sector = fat1_sector + fat_sectors;
	Uint32 rootdir_sector = fat2_sector + fat_sectors;
	// *** Create boot sector
	ZERO_BUFFER();
	memcpy(block, boot_bytes, sizeof boot_bytes);
	// 0x20-0x23 = 32-bit number of data sectors in file system
	//for (int i = 0; i < 4; i++)
	//	block[0x20 + i] = (number_of_blocks >> (i * 8)) & 0xff;
	block_uint32(0x20, number_of_blocks);
	// 0x24-0x27 = 32-bit number of sectors per fat
	//for (int i = 0; i < 4; i++)
	//	block[0x24 + i] = (fat_sectors      >> (i * 8)) & 0xff;
	block_uint32(0x24, fat_sectors);
	//block_uint16(0x0e, reserved_sectors);
	block[510] = 0x55;	// boot signature
	block[511] = 0xaa;	// boot signature
	WRITE_BLOCK(start_block);	// write boot sector
	WRITE_BLOCK(start_block + 6);	// write backup boot sector (the very same, with @ +6 sector)
	// *** Create FAT FS information sector
	ZERO_BUFFER();
	block[0] = 0x52;	// four "magic" bytes
	block[1] = 0x52;
	block[2] = 0x61;
	block[3] = 0x41;
	block[0x1e4] = 0x72;	// four another "mgic" bytes
	block[0x1e5] = 0x72;
	block[0x1e6] = 0x41;
	block[0x1e7] = 0x61;
	//for(int i = 0; i < 4; i++)	// Number of free clusters
	//	block[0x1e8 + i] = ((fs_clusters - 3) >> (i * 8)) & 0xff;
	//block_uint32(0x1e8, fs_clusters - 3);
	block_uint32(0x1e8, 0xFFFFFFFFU);	// according to some documents, this must be set to FFFFFFFF if not used
	// First free cluster = 2
	//block[0x1ec] = 0x02 + 1;	// OSX newfs/fsck puts 3 here instead?
	block_uint32(0x1ec, 0xFFFFFFFFU);	// according to some documents, this must be set to FFFFFFFF if not used
	// Boot sector signature
	block[510] = 0x55;
	block[511] = 0xaa;
	WRITE_BLOCK(start_block + 1);
	WRITE_BLOCK(start_block + 7);
	// *** Build empty FATs
	ZERO_BUFFER();
	memcpy(block, fat_bytes, sizeof fat_bytes);
	WRITE_BLOCK(start_block + fat1_sector);
	WRITE_BLOCK(start_block + fat2_sector);
	// *** Build empty root directory
	ZERO_BUFFER();
	memcpy(block, default_volume_name, 11);
	memcpy(block + 11, dir_bytes, sizeof dir_bytes);
	WRITE_BLOCK(start_block + rootdir_sector);
	// *** Erase some sectors
	ZERO_BUFFER();
	WRITE_BLOCK_RANGE(start_block + 1 + 1, start_block + 6 - 1);
	WRITE_BLOCK_RANGE(start_block + 6 + 1, start_block + fat1_sector - 1);
	WRITE_BLOCK_RANGE(start_block + fat1_sector + 1, start_block + fat2_sector - 1);
	WRITE_BLOCK_RANGE(start_block + fat2_sector + 1, start_block + rootdir_sector - 1);
	WRITE_BLOCK_RANGE(start_block + rootdir_sector + 1, start_block + rootdir_sector + 1 + sectors_per_cluster - 1);
	return 0;
}



static int mksyspart ( Uint32 start_block, Uint32 number_of_blocks )
{
	Uint32 slot_size = 512 * 1024 / 512;	// slot_size units is sectors
	// Take 1MB from partition size, for reserved space when
	// calculating what can fit.
	Uint32 reserved_sectors = 1024 * 1024 / 512;
	Uint32 slot_count = (number_of_blocks - reserved_sectors) / (slot_size * 2 + 1);
	if (slot_count >= 0xffff)
		slot_count = 0xffff;
	Uint16 dir_size = 1 + (slot_count / 4);
	Uint16 freeze_dir_sectors = dir_size;
	Uint16 service_dir_sectors = dir_size;
	// Freeze directory begins at 1MB
	Uint32 sys_partition_freeze_dir = reserved_sectors;
	// System service directory begins after that
	Uint32 sys_partition_service_dir = sys_partition_freeze_dir + slot_size * slot_count;
	// *** System sector
	ZERO_BUFFER();
	memcpy(block, sys_part_magic, sizeof sys_part_magic);	// magic bytes
	block_uint32(0x10, 0);					// $010-$013 = Start of freeze program area
	block_uint32(0x14, slot_size * slot_count + dir_size);	// $014-$017 = Size of freeze program area
	block_uint32(0x18, slot_size);				// $018-$01b = Size of each freeze program slot
	block_uint16(0x1c, slot_count);				// $01c-$01d = Number of freeze slots
	block_uint16(0x1e, dir_size);				// $01e-$01f = Number of sectors in freeze slot directory
	block_uint32(0x20, slot_size * slot_count + dir_size);	// $020-$023 = Start of freeze program area
	block_uint32(0x24, slot_size * slot_count + dir_size);	// $024-$027 = Size of service program area
	block_uint32(0x28, slot_size);				// $028-$02b = Size of each service slot
	block_uint16(0x2c, slot_count);				// $02c-$02d = Number of service slots
	block_uint16(0x2e, dir_size);				// $02e-$02f = Number of sectors in service slot directory
	// Now make sector numbers relative to start of disk for later use
	sys_partition_freeze_dir += start_block;
	sys_partition_service_dir += start_block;
	WRITE_BLOCK(start_block);
	// *** Config sector
	ZERO_BUFFER();
	block[0x000] = 0x01;	// Structure version bytes
	block[0x001] = 0x01;
	block[0x002] = 0x80;	// PAL=$00, NTSC=$80
	block[0x003] = 0x41;	// Enable audio amp, mono output
	block[0x004] = 0x00;	// Use SD card for floppies
	block[0x005] = 0x01;	// Enable use of Amiga mouses automatically
	block[0x006] = 0x41;	// Ethenet MAC, start, from mega65-fdisk: "Should do a better job of this!"
	block[0x007] = 0x41;
	block[0x008] = 0x41;
	block[0x009] = 0x41;
	block[0x00A] = 0x41;
	block[0x00B] = 0x41;	// Ethernet MAC, end
	// Set name of default disk image
	memcpy(block + 0x10, default_disk_image, strlen(default_disk_image));
	// DMAgic version: default, new revision (F011B)
	block[0x020]=0x01;
	WRITE_BLOCK(start_block + 1U);
	// ***** erase some sectors
	ZERO_BUFFER();
	// Erase the rest of the 1MB reserved area
	WRITE_BLOCK_RANGE(start_block + 2, start_block + 1023);
	// erase frozen program directory
	WRITE_BLOCK_RANGE(sys_partition_freeze_dir, sys_partition_freeze_dir + freeze_dir_sectors - 1);
	// erase system service image directory
	WRITE_BLOCK_RANGE(sys_partition_service_dir, sys_partition_service_dir + service_dir_sectors - 1);
	return 0;
}



// "fdisk" (ie, partitioning and "format" - make FAT32FS's) the SD-card
// Actually this only makes the MBR and partition boundaries, and calls mkfs_fat32() for the filesystem creation on a partition.
// I copied the algorithm from mega65-fdisk closely (and hopefully well enough) not to have any compatibility issue.
static int fdisk ( Uint32 device_size )
{
	our_data_partition = -1;
	DEBUGPRINT("SDCARD: FDISK: creating partition table ..." NL);
	// *** Current policy to create sizes and boundaries of the partitions are based on mega65-fdisk source
	Uint32 sys_part_sects = (device_size - 0x0800) >> 1;
	if (sys_part_sects > 0x400000)
		sys_part_sects = 0x400000;
	sys_part_sects &= 0xfffff800;	// round down to nearest 1MB boundary
	Uint32 usr_part_sects = device_size - 0x0800 - sys_part_sects;
	Uint32 usr_part_start = 0x0800;
	Uint32 sys_part_start = usr_part_start + usr_part_sects;
	// *** Create partition table (according to mega65-fdisk source itself)
	//     note: zero values are not set to zero individually, as those are the default after ZERO_BUFFER()
	ZERO_BUFFER();
	// disk signature, fixed value
	block[0x1b8] = 0x83;
	block[0x1b9] = 0x7d;
	block[0x1ba] = 0xcb;
	block[0x1bb] = 0xa6;
	// MBR signature
	block[0x1fe] = 0x55;
	block[0x1ff] = 0xaa;
	// FAT32 data ("user" partition)
	block[0x1c2] = 0x0c;				// partition type, VFAT32
	block[0x1c6] = (usr_part_start      ) & 0xff;
	block[0x1c7] = (usr_part_start >>  8) & 0xff;	// LBA start of our FS
	block[0x1c8] = (usr_part_start >> 16) & 0xff;
	block[0x1c9] = (usr_part_start >> 24) & 0xff;
	block[0x1ca] = (usr_part_sects      ) & 0xff;	// LBA size of our FS
	block[0x1cb] = (usr_part_sects >>  8) & 0xff;
	block[0x1cc] = (usr_part_sects >> 16) & 0xff;
	block[0x1cd] = (usr_part_sects >> 24) & 0xff;
	// The 'system' area/partition
	block[0x1d2] = 0x41;				// partition type, Mega-65 system partition
	block[0x1d6] = (sys_part_start      ) & 0xff;	// LBA start of our sys-area
	block[0x1d7] = (sys_part_start >>  8) & 0xff;
	block[0x1d8] = (sys_part_start >> 16) & 0xff;
	block[0x1d9] = (sys_part_start >> 24) & 0xff;
	block[0x1da] = (sys_part_sects      ) & 0xff;	// LBA size of our sys-area
	block[0x1db] = (sys_part_sects >>  8) & 0xff;
	block[0x1dc] = (sys_part_sects >> 16) & 0xff;
	block[0x1dd] = (sys_part_sects >> 24) & 0xff;
	WRITE_BLOCK(0);
	// *** Create FAT32 (data/user area)
	if (mkfs_fat32(usr_part_start, usr_part_sects))
		return -1;
	// *** Create SYSTEM (system area/partition)
	if (mksyspart(sys_part_start, sys_part_sects))
		return -1;
	// *** Make fat32.c to re-read partition table ...
	DEBUGPRINT("SDCARD: FDISK: Re-reading MBR ..." NL);
	// this should give result '0' (the 0'th aka first partition is valid)
	// Note: in this implementation we ALWAYS make data partition as the first one (well, the zero'th ...)
	if (mfat_init_mbr() != 0)
		FATAL("First partition entry is not valid even after FDISK!");
	if (mfat_use_part(0) != 0)
		FATAL("First partition cannot be used as FAT32FS even after FDISK/MKFS!");
	our_data_partition = 0;
	return 0;
}

/* Parameters:
	on_card_name: file of the name on the card, will be "DOS-ized", ie 8+3 chars, all capitals. Cannot have directory component!
	options: options with SDCONTENT_* stuff, to check overwrite is OK, should ask, etc
	fn_or_data:	# if size_to_install > 0, it's a memory pointer, with size_to_install bytes to install from there
			# if size_to_install = 0, it's a file name to load data from and install that, with the size of the given file
			# if size_to_install < 0, it's a file name, with -size_to_install long, and size mismatch will be checked and even asserted
	size_to_install: see above
  Notes:
	Currently policy: if file already existed on the image, we will reuse the FAT chain, _IF_ it's not fragmented and would fit.
	If the FAT chain is not long enough (ie, the size of the new file wouldn't fit in terms of number of clusters) we rather
	create a NEW fat chain, and free old one. This is because it's easier to do so, and some Mega65 features (disk images) needs
	strictly UNFRAGMENTED state anyway.
*/
static int update_sdcard_file ( const char *on_card_name, int options, const char *fn_or_data, int size_to_install )
{
	int fd = -1;
	if (size_to_install <= 0) {
		fd = open(fn_or_data, O_RDONLY | O_BINARY);
		if (fd < 0) {
			ERROR_WINDOW("SD-card image updater cannot open file:\n%s\n(%s)", fn_or_data, strerror(errno));
			goto error_on_maybe_sys_file;
		}
		off_t oft = xemu_safe_file_size_by_fd(fd);
		if (oft == OFF_T_ERROR || (size_to_install < 0 && oft != (off_t)(-size_to_install))) {
			DEBUGPRINT("Got size: " PRINTF_LLD " instruct size: %d" NL, (long long int)oft, size_to_install);
			ERROR_WINDOW("Bad file, size is incorrect or other I/O error\n%s", fn_or_data);
			goto error_on_maybe_sys_file;
		}
		size_to_install = (int)oft;
		if (!size_to_install)
			return 0;	// FIXME: we don't allow empty files to be written. Though we fake OK result no to disturb the user
	}
	//mfat_dirent_t rootdir;
	//mfat_open_rootdir(&rootdir.stream);
	Uint32 block = mfat_overwrite_file_with_direct_linear_device_block_write(&sd_rootdirent, on_card_name, size_to_install);
	if (block == 0)
		goto error_on_maybe_sys_file;
	// Copy file block by block
	while (size_to_install) {
		Uint8 buffer[512];
		int need = (size_to_install < sizeof buffer) ? size_to_install : sizeof buffer;
		if (need < sizeof(buffer))
			memset(buffer + need, 0, sizeof(buffer) - need);
		if (fd >= 0) {
			// Read from external file
			int got = xemu_safe_read(fd, buffer, need);
			if (got < 0)
				goto error;
			if (got == 0)
				goto error;
			if (got != need)
				goto error;
		} else {
			// Read from memory
			memcpy(buffer, fn_or_data, need);
			fn_or_data += need;
		}
		// And now WRITE!!!!!
		sdcard_write_block(block++, buffer);	// FIXME: error handling!!!
		size_to_install -= need;
	}
	if (fd >= 0)
		close(fd);
	mfat_flush_fat_cache();
	return 0;
error_on_maybe_sys_file:
	if ((options & SDCONTENT_SYS_FILE))
		ERROR_WINDOW(
			"This file is a must for Xemu/Mega65, however it's under\n"
			"copyright by their respective owners.\n"
			"It's totally the user's responsibility to get/use/own/handle this file!\n%s",
			fn_or_data
		);
error:
	if (fd >= 0)
		close(fd);
	mfat_flush_fat_cache();
	return -1;
}


static int update_from_directory ( const char *dirname, int options )
{
	DIR *dir = opendir(dirname);
	if (!dir) {
		ERROR_WINDOW("Cannot update SD image / virtual-disk:\nCannot open specified directory:\n%s\n%s",
			dirname,
			strerror(errno)
		);
		return errno;
	}
	for (;;) {
		errno = 0;
		struct dirent *entry = readdir(dir);
		if (!entry)
			break;
		if (entry->d_name[0] == '.')
			continue;
		char fn[PATH_MAX];
		snprintf(fn, sizeof(fn), "%s" DIRSEP_STR "%s", dirname, entry->d_name);
		struct stat st;
		if (stat(fn, &st)) {
			DEBUGPRINT("SDCARD: CONTENT: skipping updating file \"%s\": start() did not worked: %s" NL, fn, strerror(errno));
			continue;
		}
		if ((st.st_mode & S_IFMT) != S_IFREG) {
			DEBUGPRINT("SDCARD: CONTENT: skipping updating file \"%s\": not a regular file" NL, fn);
			continue;
		}
		if (st.st_size == 0 || st.st_size > MAX_IMPORT_FILE_SIZE) {	// FIXME: I limit here the max file size, but is there a sane limit?
			DEBUGPRINT("SDCARD: CONTENT: skipping updating file \"%s\": too large (limit is %d bytes) or null-sized file" NL, fn, MAX_IMPORT_FILE_SIZE);
			continue;
		}
		int ret = update_sdcard_file(entry->d_name, options, fn, -(int)st.st_size);
		DEBUGPRINT("SDCARD: CONTENT: updated file \"%s\" status is %d" NL, fn, ret);
	}
	int ret = errno;
	closedir(dir);
	mfat_flush_fat_cache();
	return ret;
}


#if 0
static int system_files_directory_update_item ( const char *dir_name, const char *fn_name, Uint8 *data, off_t size, int options )
{
	int fd;
	char fn[PATH_MAX];
	int reinstall = 0;
	snprintf(fn, sizeof fn, "%s" DIRSEP_STR "%s", dir_name, fn_name);
	fd = open(fn, O_RDONLY | O_BINARY);
	if (fd < 0) {
		reinstall = 1;
	} else {
		off_t oft = xemu_safe_file_size_by_fd(fd);
		if (oft == OFF_T_ERROR) {
		}
		if (oft != size)
			reinstall = 1;
		close(fd);
	}
	if ((options & SDCONENT_UPDATE_SYSDIR) && data && !reinstall)
		reinstall = 1;
	if (reinstall) {
		if (!data) {
		} else {
			fd = open(fn, O_WRONLY | O_TRUNC | O_CREAT | O_BINARY, 0666);
			if (fd >= 0) {
				xemu_safe_write(fd, data, size);
				close(fd);
			}
		}
	}
	return 0;
}


static int system_files_directory_check ( const char *dir_name, int policy )
{
	int ret = 0;
	MKDIR(dir_name);	// just in case, if it does not exist yet
	// Check the two ROM FILES which are needed, but we can't include them in Xemu because of copyright reasons
	// Data pointer being NULL signals that it's a check only, we have no source to create!
	// Also, options/policy is zero. This is a must for calling this function in this situation:
	system_files_directory_update_item(dir_name, MEGA65_ROM_NAME, NULL, MEGA65_ROM_SIZE, 0);
	system_files_directory_update_item(dir_name, CHAR_ROM_NAME,   NULL, CHAR_ROM_SIZE,   0);
	// Update system files in the system directory of Xemu which can be generated by ourselves
	Uint8 *d81 = xemu_malloc(D81_SIZE);
	memset(d81, 0, D81_SIZE);
	memcpy(d81 + 0x61800, d81_at_61800, sizeof d81_at_61800);
	memcpy(d81 + 0x61900, d81_at_61900, sizeof d81_at_61900);
	ret |= system_files_directory_update_item(dir_name, default_disk_image, d81, D81_SIZE, policy);
	strcpy((char*)d81, xemu_external_d81_signature);
	ret |= system_files_directory_update_item(dir_name, xemu_disk_image,    d81, D81_SIZE, policy);
	free(d81);
	ret |= system_files_directory_update_item(dir_name, "BANNER.M65",  meminitdata_banner,  MEMINITDATA_BANNER_SIZE,  policy);
	ret |= system_files_directory_update_item(dir_name, "FREEZER.M65", meminitdata_freezer, MEMINITDATA_FREEZER_SIZE, policy);
	return ret;
}
#endif


// This function must be called after initializing SDcard, so it's safe for use to call sdcard_read_block() and sdcard_write_block()
int sdcontent_handle ( Uint32 size_in_blocks, const char *update_dir_path, int options )
{
	static int init_done = 0;
	//static char system_files_directory[PATH_MAX];
	if (!init_done) {
		//snprintf(system_files_directory, sizeof system_files_directory, "%s%s", sdl_pref_dir, "system-files");
		mfat_init(sdcard_read_block, sdcard_write_block, size_in_blocks);
		init_done = 1;
	}
	//system_files_directory_check(system_files_directory, (options & SDCONTENT_UPDATE_SYSDIR));
	/* ---- ROUND#1: check card, partitions, FS validity, fdisk/format card if needed ---- */
	DEBUGPRINT("SDCARD: Reading MBR ..." NL);
	if (sdcard_read_block(0, block))
		FATAL("Cannot read MBR of SD-Card");
	int do_fdisk = 0;
	our_data_partition = -1;
	if ((options & SDCONTENT_FORCE_FDISK)) {
		do_fdisk = 1;
	} else {
		const char *p = NULL;
		// If the MBR is ALL zero, it's clearly a sign it's an empty image, at least no partition table,
		// would be unusable anyway that we can judge it as being subject of fdisk/format.
		if (has_block_nonzero_byte(block)) {
			// Though data partition is the first one created by THIS .c source, older existing
			// images formatted with Mega65's util itself has it as the second. Thus we just
			// hear mfat_init_mbr()'s response, which gives us back the first VALID FAT32 partition,
			// or -1 if fails to find any.
			int part = mfat_init_mbr();
			if (part < 0)
				p = "No valid partition entry found for FAT32FS";
			else if (mfat_use_part(part))
				p = "Invalid/bad FAT32FS on the data partition";
			else {
				our_data_partition = part;
				DEBUGPRINT("SDCARD: assumed data FAT32FS partition: #%d" NL, part);
			}
		} else
			p = "Empty MBR (new/empty image?)";
		// FIXME: maybe check/validate system partition as well?
		if (p) {
			if ((options & SDCONTENT_ASK_FDISK)) {
				char msg[256];
				snprintf(msg, sizeof msg, "SD-card image seems to have invalid format!\nReason: %s\nCan I format it?\nALL DATA WILL BE LOST!", p);
				do_fdisk = ARE_YOU_SURE(msg, 0);
			} else
				DEBUGPRINT("SDCARD: WARNING(SDCONTENT_ASK_FDISK was not requested, but problem detected): %s" NL, p);
		}
	}
	if (do_fdisk) {
		//FATAL("YAY, DOING FDISK!");
		if (fdisk(size_in_blocks))
			return -1;
	}
	if ((options & (SDCONTENT_FORCE_FDISK | SDCONTENT_ASK_FDISK)) && (our_data_partition < 0)) {
		ERROR_WINDOW("Warning! SD-card image content (partitions/fat32fs) seems to be invalid!\nCannot proceed with further SD-card Xemu FS level operations!");
		return -1;
	}
	if (our_data_partition >= 0) {
		DEBUGPRINT("SDCARD: great, it seems the card format is valid." NL);
		// Use the root directory of FATFS associated with the "sd_rootdirent" obj, so we can use to find/etc stuffs in root dir then
		mfat_open_rootdir(&sd_rootdirent.stream);
	}
	/* ---- ROUND#2: check important system files (if requested) ---- */
	options &= SDCONTENT_ASK_FILES | SDCONTENT_DO_FILES | SDCONTENT_OVERWRITE_FILES;	// no more rounds, so we mask output to the important remaing ones
	if (options) {
		if (our_data_partition < 0)
			FATAL("System file update/check requested, but format/FS was not validated!");
		mfat_flush_fat_cache();
		//update_from_directory(system_files_directory, options);
		char rom_path[PATH_MAX];
		snprintf(rom_path, sizeof rom_path, "%s%s", sdl_pref_dir, MEGA65_ROM_NAME);
		update_sdcard_file(MEGA65_ROM_NAME,	options | SDCONTENT_SYS_FILE,	rom_path,				-MEGA65_ROM_SIZE);
		snprintf(rom_path, sizeof rom_path, "%s%s", sdl_pref_dir, CHAR_ROM_NAME);
		update_sdcard_file(CHAR_ROM_NAME,	options | SDCONTENT_SYS_FILE,	rom_path,				-CHAR_ROM_SIZE);
		update_sdcard_file("BANNER.M65",	options,			(const char*)meminitdata_banner,	MEMINITDATA_BANNER_SIZE);
		update_sdcard_file("FREEZER.M65",	options,			(const char*)meminitdata_freezer,	MEMINITDATA_FREEZER_SIZE);
		char *d81 = xemu_malloc(D81_SIZE);
		memset(d81, 0, D81_SIZE);
		memcpy(d81 + 0x61800, d81_at_61800, sizeof d81_at_61800);
		memcpy(d81 + 0x61900, d81_at_61900, sizeof d81_at_61900);
		update_sdcard_file(default_disk_image,	options,			d81,					D81_SIZE);
		strcpy(d81, xemu_external_d81_signature);
		update_sdcard_file(xemu_disk_image,	options,			d81,					D81_SIZE);
		free(d81);
	}
	/* ---- ROUND#3: update user specified files (if any) ---- */
	if (update_dir_path) {
		if (our_data_partition < 0)
			FATAL("User directory based file update/check requested, but format/FS was not validated!");
		return update_from_directory(update_dir_path, SDCONTENT_OVERWRITE_FILES);
	}
	return 0;
}

#endif
