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

// Very slow, naive and stupid FAT32 implementation. There is even no LFN support, or anything fancy.

#ifdef __CC65__
#	define MEGA65_BUILD
#endif

#if defined(MEGA65_BUILD) || !defined(XEMU_BUILD) || (defined(XEMU_BUILD) && defined(SD_CONTENT_SUPPORT))

#ifndef MEGA65_BUILD
#	define	FDISK_SUPPORT
#endif

#ifdef XEMU_BUILD
#	include "xemu/emutools.h"
#	include "xemu/emutools_files.h"
#	define FATDEBUG		DEBUG
#	define FATDEBUGPRINT	DEBUGPRINT
#else
#	define NL		"\n"
#	define FATDEBUG		printf
#	define FATDEBUGPRINT	printf
#	define FATAL(...)	do { fprintf(stderr, "FATAL: "); fprintf(stderr, __VA_ARGS__); exit(1); } while(0)
#	define ERROR_WINDOW(...)	do { fprintf(stderr, "ERROR: "); fprintf(stderr, __VA_ARGS__); } while(0)
#	define OFF_T_ERROR	((off_t)-1)
#	ifdef MEGA65_BUILD
		typedef unsigned long int	Uint32;
		typedef unsigned int		Uint16;
		typedef unsigned char		Uint8 ;
#	else
#		include <stdint.h>
		typedef uint32_t		Uint32;
		typedef uint16_t		Uint16;
		typedef uint8_t			Uint8 ;
#	endif
#endif

#include <stdio.h>
#include <sys/types.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>

#include "fat32.h"


#define FS_MINIMAL_SIZE_IN_BLOCKS 10


static struct {
	//int (*reader)(Uint32 block, void *data);
	//int (*writer)(Uint32 block, void *data);
	mfat_io_callback_func_t reader, writer;
	Uint32	blocks;
	int	part;	// selected partition number
} disk;


struct mfat_part_st mfat_partitions[4];


static int mfat_read_DEVICE_blk ( Uint32 block, void *buf )
{
	if (block >= disk.blocks) {
		FATDEBUGPRINT("FATFS: WARNING: Host FS: reading outside of the device" NL);
		return -1;
	}
	return disk.reader(block, buf);
}
static int mfat_write_DEVICE_blk ( Uint32 block, void *buf )
{
	if (block >= disk.blocks) {
		FATDEBUGPRINT("FATFS: WARNING: Host FS: writing outside of the device" NL);
		return -1;
	}
	return disk.writer(block, buf);
}

static int mfat_read_part_blk ( Uint32 block, void *buf )
{
	if (disk.part < 0) {
		FATDEBUGPRINT("FATFS: WARNING: read partition block: invalid partition is selected" NL);
		return -1;
	}
	if (block >= mfat_partitions[disk.part].blocks) {
		FATDEBUGPRINT("FATFS: WARNING: read partition block: trying to read block outside of partition!" NL);
		return -1;
	}
	return mfat_read_DEVICE_blk(block + mfat_partitions[disk.part].first_block, buf);
}
static int mfat_write_part_blk ( Uint32 block, void *buf )
{
	if (disk.part < 0) {
		FATDEBUGPRINT("FATFS: WARNING: write partition block: invalid partition is selected" NL);
		return -1;
	}
	if (block >= mfat_partitions[disk.part].blocks) {
		FATDEBUGPRINT("FATFS: WARNING: write partition block: trying to read block outside of partition!" NL);
		return -1;
	}
	return mfat_write_DEVICE_blk(block + mfat_partitions[disk.part].first_block, buf);
}

static int mfat_read_cluster ( Uint32 cluster, Uint32 block_in_cluster, void *buf )
{
	cluster &= 0x0FFFFFFFU;	// some well known fact, that FAT32 is actually FAT28, and the highest 4 bits should not be used!
	if (cluster < 2 || cluster >= mfat_partitions[disk.part].clusters) {
		FATDEBUGPRINT("FATFS: WARNING: read cluster: invalid cluster number %d" NL, cluster);
		return -1;
	}
	if (block_in_cluster >= mfat_partitions[disk.part].cluster_size_in_blocks) {
		FATDEBUGPRINT("FATFS: WARNING: read cluster: invalid in-cluster-block data %d" NL, block_in_cluster);
		return -1;
	}
	return mfat_read_part_blk(cluster * mfat_partitions[disk.part].cluster_size_in_blocks + mfat_partitions[disk.part].data_area_fake_ofs + block_in_cluster, buf);
}
static int mfat_write_cluster ( Uint32 cluster, Uint32 block_in_cluster, void *buf )
{
	cluster &= 0x0FFFFFFFU;	// some well known fact, that FAT32 is actually FAT28, and the highest 4 bits should not be used!
	if (cluster < 2 || cluster >= mfat_partitions[disk.part].clusters) {
		FATDEBUGPRINT("FATFS: WARNING: write cluster: invalid cluster number %d" NL, cluster);
		return -1;
	}
	if (block_in_cluster >= mfat_partitions[disk.part].cluster_size_in_blocks) {
		FATDEBUGPRINT("FATFS: WARNING: write cluster: invalid in-cluster-block data %d" NL, block_in_cluster);
		return -1;
	}
	return mfat_write_part_blk(cluster * mfat_partitions[disk.part].cluster_size_in_blocks + mfat_partitions[disk.part].data_area_fake_ofs + block_in_cluster, buf);
}

#define AS_WORD(p,o)	(p[o] + (p[o+1] << 8))
#define AS_DWORD(p,o)	(p[o] + (p[o+1] << 8) + (p[o+2] << 16) + (p[o+3] << 24))


static struct {
	Uint8  buf[512];
	Uint32 block;
	int    dirty;
	int    ofs;
} fat_cache;



int mfat_flush_fat_cache ( void )
{
	if (fat_cache.dirty) {
		if (mfat_write_part_blk(fat_cache.block, fat_cache.buf))
			return -1;
		// Also, we need to write the "backup" copy of FAT.
		// We assume max of two FATs are used, but currenty, other parts of this source assumes ALWAYS two copies.
		// To be careful, let's check it here though.
		int fat2_offset = mfat_partitions[disk.part].fat2_start - mfat_partitions[disk.part].fat1_start;
		if (fat2_offset > 0)
			mfat_write_part_blk(fat_cache.block + fat2_offset, fat_cache.buf);	// FIXME: should we error check this? if "main" FAT was OK to be written but not the "backup" one??
		fat_cache.dirty = 0;
	}
	return 0;
}


static int cluster_was_free;

// Return value:
// 0 = end of chain [regardless of the real used EOC]
// 1 = fatal error [like I/O layer read problem ...]
// other = cluster number from the chained cluster
static Uint32 mfat_read_fat_chain ( Uint32 cluster )
{
	Uint32 block;
	cluster &= 0x0FFFFFFFU;	// some well known fact, that FAT32 is actually FAT28, and the highest 4 bits should not be used!
	if (cluster < 2 || cluster >= mfat_partitions[disk.part].clusters) {
		FATDEBUGPRINT("FATFS: WARNING: read fat: invalid cluster number %d" NL, cluster);
		return 1;
	}
	Uint32 cluster_in = cluster;
	block = mfat_partitions[disk.part].fat1_start + (cluster >> 7);
	fat_cache.ofs   = (cluster & 127) << 2;
	if (block != fat_cache.block) {
		mfat_flush_fat_cache();
		if (mfat_read_part_blk(block, fat_cache.buf))
			return 1;
		fat_cache.block = block;
		FATDEBUG("FATFS: UNCACHED block: %d" NL, block);
	} else
		FATDEBUG("FATFS: COOL, fat block is cached for %d" NL, block);
	cluster = AS_DWORD(fat_cache.buf, fat_cache.ofs) & 0x0FFFFFFFU;
	FATDEBUG("FATFS: DEBUG: mfat_read_chain: got cluster: $%08X" NL, AS_DWORD(fat_cache.buf, fat_cache.ofs));
	// In theory there is some "official" end-of-chain marker, but in reality it seems anything which is outside of normal
	// cluster number on the FS "should" be considered as end-of-chain marker.
	// That's actually great, we should not worry here what is the "EOC" marker, and just say this:
	if (cluster < 2 || cluster >= mfat_partitions[disk.part].clusters) {
		// THIS IS AN IMPORTANT HACK! Normally this function is used to follow FAT chain. There, it's should never
		// enounter free cluster in chain, if it does, that's considered error or "EOC". However. In case of searching
		// free cluster it is VERY important to know it was EOC (thus the cluster is not free!) or free. So we create
		// this ugly stuff here can be used by the caller ...
		cluster_was_free = (cluster == 0);
		return 0;
	}
	if (cluster == cluster_in) {
		FATDEBUGPRINT("FATFS: ERROR: cluster %u refers itself!" NL, cluster);
		return 0;	// serious problem, cluster refeers to itself???? We just handle the problem as it would be EOC as well ...
	}
	return cluster;
}


// Next can be:
// 0 = end of chain
// 1 = FREE
// Other = valid next cluster to be chained
static int mfat_write_fat_chain ( Uint32 cluster, Uint32 next )
{
	if (next >= mfat_partitions[disk.part].clusters)
		return 1;
	else if (next == 0)
		next = mfat_partitions[disk.part].eoc_marker;	// 0x0FFFFFFFU;
	else if (next == 1)
		next = 0;
	// first we want to READ ... it also flushes possible previous cached dirty block
	// It also calculates the block offset (fat_cache.ofs) for us!
	// And moreover, it also uses cached copies to avoid read, if the same block from FAT was used before!!
	int ret = mfat_read_fat_chain(cluster);
	if (ret == 1)	// ERROR!!!!
		return 1;
	for (int a = fat_cache.ofs; a < fat_cache.ofs + 4; a++, next >>= 8) {
		if (fat_cache.buf[a] != (Uint8)next) {
			fat_cache.buf[a] = (Uint8)next;
			fat_cache.dirty = 1;	// now we make the cache dirty, oh-oh
		}
	}
	return 0;
}

// Free a chain of clusters in FAT starting at argument "cluster"
// Returns with zero on successfull operation, or non-zero if any error encountered
static int mfat_free_fat_chain ( Uint32 cluster )
{
	while (cluster >= 2) {
		Uint32 next_cluster = mfat_read_fat_chain(cluster);
		if (next_cluster == 1)
			goto error;
		if (mfat_write_fat_chain(cluster, 1))	// 1=FREE in our API
			goto error;
		cluster = next_cluster;
	}
	if (mfat_flush_fat_cache())
		return 1;
	return cluster;		// mfat_read_fat_chain() returns with zero on last cluster thus return with 0 considered OK, other error is PROBLEM!
error:
	mfat_flush_fat_cache();	// even in case of error, we try to flush FAT cache to avoid further corruption of FAT without forgetting this!
	return 1;
}


// Allocate LINEAR chunk in FAT for "size" bytes. SIZE CANNOT BE ZERO!!!!!!
// Returns cluster number as the first one, or ZERO IN CASE OF ERROR
// In case of success, FAT is already written to have the correct chain.
Uint32 mfat_allocate_linear_fat_chunk ( Uint32 size )
{
	if (!size)
		return 0;	// error
	Uint32 cluster_byte_size = mfat_partitions[disk.part].cluster_size_in_blocks * 512;
	FATDEBUG("FATFS: DEBUG: %s() is about seeking for free linear chunk in FAT for %u bytes size object" NL, __func__, size);
	// OK, so now, size is the needed number of clusters to allocate
	// start from cluster 2, the first data cluster, to try with (though probably that's root dir, so won't be free, but anyway, for strange situations ...)
	for (Uint32 cluster = mfat_partitions[disk.part].root_dir_cluster + 1, first = 0, len = 0, seq = 0; cluster < mfat_partitions[disk.part].clusters; cluster++) {
		Uint32 next = mfat_read_fat_chain(cluster);
		FATDEBUG("FATFS: %u cluster's result in FAT: %u" NL, cluster, next);
		if (next == 1)
			return 0;	// error!
		if (next == 0 && cluster_was_free) {	// cluster is free
			if (first == 0) {
				first = cluster;
				len = cluster_byte_size;
				seq = 1;
			} else {
				len += cluster_byte_size;
				seq++;
			}
			if (len >= size) {	// we found it :)
				FATDEBUG("FATFS: DEBUG: %s() founds %u sized free linear chunk in FAT at cluster %u" NL, __func__, size, first);
				for (Uint32 a = 0; a < seq; a++) {
					if (mfat_write_fat_chain(first + a, a != seq - 1 ? first + a + 1 : 0)) {	// 0 is end-of-chain marker in my implementation
						// In case of an error, we try to FREE what we've did ...
						// if that errors out, we can't do too much at this point
						// And btw that called mfat_flush_fat_cache() as well ...
						mfat_free_fat_chain(first);
						return 0;	// error!
					}
				}
				mfat_flush_fat_cache();	// flush FAT cache at the end, to be sure everything is written out. FIXME: what happens if it errors out? :-O
				return first;
			}
		} else			// cluster is not free
			first = 0;
	}
	FATDEBUG("FATFS: DEBUG: %s() could not found %d sized free linear chunk in FAT ..." NL, __func__, size);
	return 0;	// not found :(
}




void mfat_init ( mfat_io_callback_func_t reader, mfat_io_callback_func_t writer, Uint32 device_size )
{
	disk.reader = reader;
	disk.writer = writer;
	disk.blocks = device_size;
	disk.part = -1;
	fat_cache.block = -1;
	fat_cache.dirty = 0;
}


// Reads and parse MBR. Fills mfat_partitions array
// Returns with a canidate of the first usable partition [also partiton type], though the FS itself is NOT checked.
// returns with -1, if there is some error, or no usable partition candidate found.
// Caller must use mfat_use_part() after this function to really select a partition at FS level, by using the retval, or decide at your own.
// mfat_init() must be called before this function!
int mfat_init_mbr ( void )
{
	int first_valid = -1;
	Uint8 cache[512], *p;
	// DANGER WILL ROBINSON! Previous partition could be in use!! Flush cache!
	mfat_flush_fat_cache();
	fat_cache.block = -1;
	fat_cache.dirty =  0;
	// end of the danger zone
	if (mfat_read_DEVICE_blk(0, cache))	// read MBR
		return -1;
	// parse partition entries
	memset(mfat_partitions, 0, sizeof mfat_partitions);
	p = cache + 0x1BE;
	for (int a = 0; a < 4; a++, p += 16) {
		mfat_partitions[a].fs_validated = 0;
		mfat_partitions[a].valid = 0;
		mfat_partitions[a].first_block = AS_DWORD(p, 0x8); //p[0x8] + (p[0x9] << 8) + (p[0xA] << 16) + (p[0xB] << 24);
		mfat_partitions[a].blocks      = AS_DWORD(p, 0xC);	//p[0xC] + (p[0xD] << 8) + (p[0xE] << 16) + (p[0xF] << 24);
		mfat_partitions[a].last_block = mfat_partitions[a].first_block + mfat_partitions[a].blocks - 1;
		mfat_partitions[a].part_type = p[4];
		// Check some basic things on possible canditates for a valid partition, but it's only about sizes, layout, partition type, FS is not checked at this point!
		if (
			(p[4] == 0xB || p[4] == 0xC) &&
			mfat_partitions[a].blocks >= FS_MINIMAL_SIZE_IN_BLOCKS &&
			mfat_partitions[a].first_block > 0 &&
			mfat_partitions[a].first_block < disk.blocks &&
			mfat_partitions[a].last_block < disk.blocks
		) {
			mfat_partitions[a].valid = 1;
		}
		FATDEBUG("FATFS: Part#%d: %08X - %08X (size=%08X), type = %02X, valid=%d size=%d Mbytes" NL,
			a,
			mfat_partitions[a].first_block,
			mfat_partitions[a].last_block,
			mfat_partitions[a].blocks,
			mfat_partitions[a].part_type,
			mfat_partitions[a].valid,
			mfat_partitions[a].blocks >> 11
		);
		if (mfat_partitions[a].valid && first_valid == -1)
			first_valid = a;
	}
	return first_valid;
}


static void hexdump ( Uint8 *p, int lines, const char *title )
{
	if (title)
		FATDEBUG("*** %s" NL, title);
	for (int b = 0 ; b < lines ; b ++) {
		FATDEBUG("%03X  ", b * 32);
		for (int a = 0; a < 32; a++)
			FATDEBUG("%02X ", p[a]);
		for (int a = 0; a < 32; a++)
			FATDEBUG("%c", p[a] >=32 && p[a] < 127 ? p[a] : '?');
		FATDEBUG(NL);
		p += 32;
	}
}




// Use this function to select a partition to be used. It also does a check if a partition is a FAT32 one.
// Requires: mfat_init() then mfat_init_mbr() functions before.
int mfat_use_part ( int part )
{
	int previous_part = disk.part;
	// DANGER WILL ROBINSON! Previous partition may be was in use! Flush cache!
	mfat_flush_fat_cache();
	fat_cache.block = -1;
	fat_cache.dirty =  0;
	// end of the danger zone
	disk.part = part;
	if (part < 0 || part > 3 || !mfat_partitions[part].valid)
		goto error;
	if (mfat_partitions[part].fs_validated)	// if filesystem was validated before, we returns ...
		return 0;
	// try to validate filesystem now, if it's really FAT32, ALSO it puts the needed information into mfat_partitions[] used by the actual FS code!!!
	Uint8 cache[512];
	if (mfat_read_part_blk(0, cache))
		goto error;
	FATDEBUG("FATFS: INFO: Bytes per logical sector: %d" NL, AS_WORD(cache,0xB));
	if (AS_WORD(cache, 0xB) != 512) {
		FATDEBUGPRINT("FATFS: WARNING: Only 512 bytes / logical sector is supported!" NL);
		goto error;
	}
	FATDEBUG("FATFS: INFO: Logical sectors per cluster: %d (cluster size = %d bytes)" NL, cache[0xD], cache[0xD] * 512);
	if (cache[0xD] == 0) {	// FIXME: better check, should be some non-zero & power of two ...
		FATDEBUGPRINT("FATFS: WARNING: Invalid logical sectors per cluster information!" NL);
		goto error;
	}
	mfat_partitions[part].cluster_size_in_blocks = cache[0xD];
	FATDEBUG("FATFS: INFO: Reserved logical sectors: %d" NL, AS_WORD(cache, 0xE));
	FATDEBUG("FATFS: INFO: FAT copies: %d" NL, cache[0x10]);
	if (cache[0x10] != 2) {
		FATDEBUGPRINT("FATFS: WARNING: Invalid number of FAT copies!" NL);
		goto error;
	}
	FATDEBUG("FATFS: INFO: Logical sectors by FAT: %d" NL, AS_DWORD(cache, 0x24));
	FATDEBUG("FATFS: INFO: Cluster of ROOT dir: %d" NL, AS_DWORD(cache, 0x2C));
	mfat_partitions[part].root_dir_cluster = AS_DWORD(cache, 0x2C);

	FATDEBUG("FATFS: INFO: FS information sector at block: %d" NL, AS_WORD(cache, 0x30));
	mfat_partitions[part].fs_info_block_number = AS_WORD(cache, 0x30);


	mfat_partitions[part].fat1_start = AS_WORD(cache, 0xE);	// FAT starts after the reserved sectors area
	mfat_partitions[part].fat2_start = AS_WORD(cache, 0xE) + AS_DWORD(cache, 0x24);
	Uint32 a                         = AS_WORD(cache, 0xE) + AS_DWORD(cache, 0x24) * 2;
	mfat_partitions[part].clusters = ((mfat_partitions[part].blocks - a) / mfat_partitions[part].cluster_size_in_blocks) + 2;
	FATDEBUG("FATFS: INFO: Number of clusters CALCULATED (incl. 0,1): %d" NL, mfat_partitions[part].clusters);
	FATDEBUG("FATFS: INFO: Number of clusters from FAT size: %d" NL, AS_DWORD(cache, 0x24) * 128);	// FIXME: decide what number of clusters data is valid [obviously, the smaller value of both ...]

	mfat_partitions[part].data_area_fake_ofs = a - (mfat_partitions[part].cluster_size_in_blocks * 2);


	if (mfat_read_part_blk(mfat_partitions[part].fat1_start, cache))
		goto error;
	FATDEBUG("FATFS: INFO: FAT1 marks #0: $%08X" NL, AS_DWORD(cache, 0));
	mfat_partitions[part].eoc_marker = AS_DWORD(cache, 0);
	if ((mfat_partitions[part].eoc_marker & 0x0FFFFFFFU) < 0x0FFFFFF8U)
		mfat_partitions[part].eoc_marker = 0x0FFFFFF8U;
	FATDEBUG("FATFS: EOC marker will be: %X" NL, mfat_partitions[part].eoc_marker);
	FATDEBUG("FATFS: INFO: FAT1 marks #1: $%08X" NL, AS_DWORD(cache, 4));
	if (mfat_read_part_blk(mfat_partitions[part].fat2_start, cache))
		goto error;
	FATDEBUG("FATFS: INFO: FAT2 marks #0: $%08X" NL, AS_DWORD(cache, 0));
	FATDEBUG("FATFS: INFO: FAT2 marks #1: $%08X" NL, AS_DWORD(cache, 4));
	//if (mfat_read_
	//luster(mfat_partitions[part].root_dir_cluster, 0, cache))
	//	goto error;
	//hexdump(cache, 16, "Root directory");
	a = mfat_partitions[part].root_dir_cluster;
	do {
		for (int b = 0; b < mfat_partitions[part].cluster_size_in_blocks; b++)
			if (mfat_read_cluster(a, b, cache)) {
				FATDEBUGPRINT("FATFS: Error following the root directory!" NL);
				goto error;
			} else {
				FATDEBUG("FATFS: INFO: Root directory, cluster=%d/block=%d" NL, a, b);
				hexdump(cache, 16, NULL);
				for (int c = 0; c < 512; c += 32) {
					if (cache[c] >= 32 && cache[c] != 0xE5 && (cache[c + 0xB] & 8)) {
						cache[c + 0xB] = 0;
						FATDEBUG("FATFS: INFO: VOLUME is: \"%s\"" NL, cache + c);
					}
					if (cache[c] == 0)
						goto end_of_directory;
				}
			}
		a = mfat_read_fat_chain(a);
		if (a == 1)
			goto error;
	} while (a);
end_of_directory:

	// just for fun ...
	//for (a = 2; a < mfat_partitions[part].

	// FS information sector
	if (mfat_partitions[part].fs_info_block_number > 0 && mfat_partitions[part].fs_info_block_number != 0xFFFF) {
		if (mfat_read_part_blk(mfat_partitions[part].fs_info_block_number, cache))
			goto error;
		hexdump(cache, 16, "FS information sector");
		FATDEBUG("FATFS: INFO: FS info signature @0=$%08X @1E4=$%08X @1FC=$%08X" NL,
				AS_DWORD(cache, 0), AS_DWORD(cache, 0x1E4), AS_DWORD(cache, 0x1FC)
		);
		if (AS_DWORD(cache, 0) != 0x41615252 || AS_DWORD(cache, 0x1E4) != 0x61417272 || AS_DWORD(cache, 0x1FC) != 0xAA550000U) {
			FATDEBUGPRINT("FATFS: *** BAD FS info sector signature(s)" NL);
			goto error;
		}
	} else {
		FATDEBUGPRINT("FATFS: No FS information sector!" NL);
		goto error;
	}
	FATDEBUGPRINT("FATFS: GOOD, FS seems to be intact! :-)" NL);
	mfat_partitions[part].fs_validated = 1;
	return 0;
error:
	disk.part = previous_part;
	return -1;
}


/* ---- FILESYSTEM like functions, generic, even directories are just "files" with special format though ---- */


void mfat_open_stream ( mfat_stream_t *p, Uint32 cluster )
{
	p->cluster = cluster;
	p->in_cluster_block = 0;
	p->in_block_pos = 0;
	p->eof = 0;
	p->size_constraint = -1;
	p->file_pos = 0;
	p->partition = &mfat_partitions[disk.part];
	p->start_cluster = cluster;	// for rewind() like function?
}


void mfat_open_rootdir ( mfat_stream_t *p )
{
	mfat_open_stream(p, mfat_partitions[disk.part].root_dir_cluster);	// we can't use p->partition here since mfat_open_stream is what sets it!!!
}


void mfat_rewind_stream ( mfat_stream_t *p )
{
	p->cluster = p->start_cluster;
	p->in_cluster_block = 0;
	p->in_block_pos = 0;
	p->eof = 0;
	p->file_pos = 0;
}


// Return value: 0=error, otherwise file size determined by fat chains converted to bytes, not the one given in the directory entry!
// It also fills fragmented (if it's not NULL) with the fact, that file is fragmented or not, unless return value is zero (error)
Uint32 mfat_get_real_size ( mfat_stream_t *p, int *fragmented )
{
	Uint32 clusters = 0;
	Uint32 cluster = p->start_cluster;
	if (fragmented)
		*fragmented = 0;
	while (cluster >= 2 && cluster < p->partition->clusters) {
		clusters++;
		Uint32 next_cluster = mfat_read_fat_chain(cluster);
		if (next_cluster == 1)
			return 0;	// error
		if (next_cluster >= 2 && next_cluster != (cluster + 1) && fragmented)
			*fragmented = 1;
		cluster = next_cluster;
	}
	return clusters * p->partition->cluster_size_in_blocks * 512;
}

// Warning: uses own cache! That is, it's not possible to deal with more than ONE stream at the same time!!
static struct {
	Uint8	buf[512];
	Uint32	cluster;
	int	cluster_block;
	int	part;
} stream_cache = {
	.part = -1,
	.cluster_block = -1,
	.cluster = -1
};


// Warning: uses own cache! That is, it's not possible to deal with more than ONE stream at the same time!!
// Currently do NOT mix with mfat_write_stream()!
int mfat_read_stream ( mfat_stream_t *p, void *buf, int size )
{
	//static Uint8 cache[512];
	//static Uint32 cached_cluster;
	//static int cached_cluster_block = -1;
	//static int cached_part = -1;
	int ret = 0;
	if (p->eof)
		return 0;
	if (p->size_constraint >= 0 && p->file_pos + size > p->size_constraint)
		size = p->size_constraint - p->file_pos;
	while (size > 0) {
		if (p->cluster != stream_cache.cluster || p->in_cluster_block != stream_cache.cluster_block || disk.part != stream_cache.part) {
			if (mfat_read_cluster(p->cluster, p->in_cluster_block, stream_cache.buf))
				goto error;
			stream_cache.cluster = p->cluster;
			stream_cache.cluster_block = p->in_cluster_block;
			stream_cache.part = disk.part;
		} else
			FATDEBUG("FATFS: INFO: WOW, data block has been cached for cluster %d, block %d within cluster" NL, stream_cache.cluster, stream_cache.cluster_block);
		int piece = 512 - p->in_block_pos;
		if (size < piece)
			piece = size;
		if (piece == 0)
			goto eof;
		memcpy(buf, stream_cache.buf + p->in_block_pos, piece);
		ret += piece;
		p->file_pos += piece;
		size -= piece;
		buf = (Uint8*)buf + piece;
		p->in_block_pos += piece;
		if (p->in_block_pos == 512) {
			p->in_block_pos = 0;
			p->in_cluster_block++;
			if (p->in_cluster_block == p->partition->cluster_size_in_blocks) {
				p->cluster = mfat_read_fat_chain(p->cluster);
				if (p->cluster == 1)
					goto error;
				if (p->cluster == 0)
					goto eof;
				p->in_cluster_block = 0;
			}
		} else if (p->in_block_pos > 512) {
			FATAL("FATFS: in_block_pos is greater than 512");
		}
	}
	return ret;
eof:
	p->eof = 1;
	return ret;
error:
	p->eof = 1;
	return -1;
}


#if 0
// Warning: uses own cache! That is, it's not possible to deal with more than ONE stream at the same time!!
// Currently do NOT mix with mfat_read_stream()!
int mfat_write_stream ( mfat_stream_t *p, void *buf, int size )
{
	static Uint8 cache[512];
	static Uint32 cached_cluster;
	static int cached_cluster_block = -1;
	int ret = 0;
	if (p->eof)
		return 0;
	if (p->size_constraint >= 0 && p->file_pos + size > p->size_constraint)
		size = p->size_constraint - p->file_pos;
	while (size > 0) {
		if (p->cluster != cached_cluster || p->in_cluster_block != cached_cluster_block) {
			if (mfat_read_cluster(p->cluster, p->in_cluster_block, cache))
				goto error;
			cached_cluster = p->cluster;
			cached_cluster_block = p->in_cluster_block;
		} else
			FATDEBUG("WOW, data block has been cached for cluster %d, block %d within cluster" NL, cached_cluster, cached_cluster_block);
		int piece = 512 - p->in_block_pos;
		if (size < piece)
			piece = size;
		if (piece == 0)
			goto eof;
		memcpy(buf, cache + p->in_block_pos, piece);
		ret += piece;
		p->file_pos += piece;
		size -= piece;
		buf = (Uint8*)buf + piece;
		p->in_block_pos += piece;
		if (p->in_block_pos == 512) {
			p->in_block_pos = 0;
			p->in_cluster_block++;
			if (p->in_cluster_block == p->partition->cluster_size_in_blocks) {
				p->cluster = mfat_read_fat_chain(p->cluster);
				if (p->cluster == 1)
					goto error;
				if (p->cluster == 0)
					goto eof;
				p->in_cluster_block = 0;
			}
		} else if (p->in_block_pos > 512) {
			FATAL("FATFS: in_block_pos is greater than 512");
		}
	}
	return ret;
eof:
	p->eof = 1;
	return ret;
error:
	p->eof = 1;
	return -1;
}
#endif


/* ---- functions for handling directories ---- */

static mfat_stream_t dir_cur_item_stream;


static void repos_cur_dirent ( mfat_stream_t *p )
{
	memcpy(p, &dir_cur_item_stream, sizeof(mfat_stream_t));
}


int mfat_read_directory ( mfat_dirent_t *p, int type_filter )
{
	Uint8 buf[32];
	do {
		// FIXME: below, check if this really works on abnormal direcotry as well when it's not closed with a null entry!
		memcpy(&dir_cur_item_stream, &p->stream, sizeof(mfat_stream_t));
		int ret = mfat_read_stream(&p->stream, buf, 32);
		if (ret <= 0) {
			FATDEBUG("FATFS: INFO: %s getting ret %d" NL, __func__, ret);
			return ret;
		}
		if (ret != 32) {
			FATAL("FATFS: dirent read 32 is not 32" NL);
		}
		if (buf[0] == 0) {		// marks end of directory, thus returning with no entry found at this point
			FATDEBUG("FATFS: INFO: %s end of stream" NL, __func__);
			p->stream.eof = 1;
			return 0;
		}
	} while (
		((buf[0xB] &  0x0F) == 0x0F) ||	// LFN piece is ignored for now, not supported
		( buf[0x0] <= 0x20) ||		// strange character as the first char is not supported, and ignored, even space
		( buf[0x0] &  0x80) ||		// ... or any high-bit set byte (that includes 0xE5 too, which is deleted file)
		// if it's a volume label but we haven't requested for volume label to be found, then ignore it
		((buf[0xB] &  0x08) &&      !(type_filter & MFAT_FIND_VOL )) ||
		// if it's a directory but we haven't requested for directory to be found, then ignore it
		((buf[0xB] &  0x10) &&      !(type_filter & MFAT_FIND_DIR )) ||
		// if it's a regular file (ie, not volume label, not directory) but we haven't requested for regular file to be found, then ignore it
		((buf[0xB] &  0x18) == 0 && !(type_filter & MFAT_FIND_FILE))
	);
	// Convert name into "string" format with BASE8.EXT3 ...
	// Technically it's kinda valid if space is part of file name, but we don't support such a scenario and simply ignore the problem ...
	int i = 0;
	char *d = p->name;
	while (buf[i] != 0x20 && i < 8)
		*d++ = buf[i++];
	if (buf[8] != 0x20) {
		i = 0;
		*d++ = '.';
		while (buf[8 + i] != 0x20 && i < 3)
			*d++ = buf[8 + (i++)];
	}
	*d = '\0';
	// end of filename stuff. Also store (just in case) the FAT name as-is
	memcpy(p->fat_name, buf, 8 + 3);
	p->fat_name[8 + 3] = '\0';
	// Also store start cluster, size, file type and date!
	p->cluster = AS_WORD(buf, 0x1A) + (AS_WORD(buf, 0x14) << 16);
	p->size = AS_DWORD(buf, 0x1C);
	p->type = buf[0xB];
	struct tm time;
	time.tm_sec   = (AS_WORD(buf, 0x16) &  31) <<  1;
	time.tm_min   = (AS_WORD(buf, 0x16) >>  5) &  63;
	time.tm_hour  = (AS_WORD(buf, 0x16) >> 11) &  31;
	time.tm_mday  =  AS_WORD(buf, 0x18)        &  31;
	time.tm_mon   = (AS_WORD(buf, 0x18) >>  5) &  15;
	time.tm_year  = (AS_WORD(buf, 0x18) >>  9) +  80;
	time.tm_wday  = 0;
	time.tm_yday  = 0;
	time.tm_isdst = -1;
	p->time = mktime(&time);
	return 1;
}


// int mfat_append_directory  FIXME

// "name" can be NULL to return with the first valid one according to type_filter!
int mfat_search_in_directory ( mfat_dirent_t *p, const char *name, int type_filter )
{
	// normalize the search name, ie shorten to 8+3, all capitals, also checking for invalid chars, etc
	char normalized_search_name[8 + 1 + 3 + 1];
	if (name && mfat_normalize_name(normalized_search_name, name)) {	// only do normalization if name != NULL, "short circuit" kind of solution
		p->name[0] = 0;
		return -1;	// bad search name!
	}
	mfat_rewind_stream(&p->stream);
	for (;;) {
		int ret = mfat_read_directory(p, type_filter);
		if (ret == 1) {
			FATDEBUG("FATFS: INFO: %s considering file [%s] as [%s]" NL, __func__, p->name, name ? normalized_search_name : "<first>");
			// FIXME: probably, later we want to allow joker characters?
			if (!name)
				return 1;
			if (!strcmp(p->name, normalized_search_name))
				return 1;
		} else {
			FATDEBUG("FATFS: INFO: %s returns because of ret being %d" NL, __func__, ret);
			return ret;
		}
	}
}


// about the details how it returns, please reas the comments near the end of this functions. Zero return value = error
Uint32 mfat_overwrite_file_with_direct_linear_device_block_write ( mfat_dirent_t *dirent, const char *name, Uint32 size )
{
	char fat_name[8 + 3 + 1];
	mfat_fatize_name(fat_name, name);
	int write_null_entry = 0;
	int ret = mfat_search_in_directory(dirent, name, MFAT_FIND_FILE | MFAT_FIND_DIR);	// we also want to find dirs so we avoid the collosion between the same name
	if (ret == 1) {
		// found the file
		if (IS_MFAT_DIR(dirent->type)) {
			// PROBLEM: the found item is a _DIRECTORY_, we can't overwrite that with a file!
			ERROR_WINDOW("Problem: file %s already exists as a directory on the image\nNot possible to overwrite with a file\nSource file: %s", dirent->name, name);
			return 0;	// error
		}
		// let's be cheap and wasteful. We're just DELETE the old FAT chain, and allocate a new one, even if file existed before and would be enough for us, and also non-fragmented
		// if the scenario above is true, probably it will allocate the same space then, so no harm is done, just slower ...
		// however these stuffs are Xemu init time tasks, so does not matter a lot, to be slower a bit ...
		mfat_free_fat_chain(dirent->cluster);
	} else if (ret == 0) {
		// not found the file
		// ... so we should extend directory and allocate new chain of FAT as well
		write_null_entry = 1;	// TODO: this should be handled!
	} else if (ret == -1) {
		// some error occured
		return 0;	// ERROR?
	} else {
		FATAL("Unknown error code of %d in %s", ret, __func__);
	}
	// We assume that *NO* stream operation was done, so stream cache STILL holds the directory entry!!!!
	// that is, we just modify the cache and write back ...
	repos_cur_dirent(&dirent->stream);	// sets "filepos" stuffs the current entry back
	Uint8 *p = stream_cache.buf + dirent->stream.in_block_pos;
	// Allocate FAT!!!!
	dirent->cluster = mfat_allocate_linear_fat_chunk(size);
	if (!dirent->cluster) {
		// ERROR: could not allocate chain!!!!
		// We should delete the file (since its old chain is free'd ...) and give up :(
		*p = 0xE5;
		// if this one does not work, we can't do anything too much, anyway ...
		mfat_write_cluster(stream_cache.cluster, stream_cache.cluster_block, stream_cache.buf);
		return 0;
	}
	memcpy(p, fat_name, 8 + 3);	// copy FAT style file name in
	p[0x1A] =  dirent->cluster        & 0xFF;
	p[0x1B] = (dirent->cluster >>  8) & 0xFF;
	p[0x14] = (dirent->cluster >> 16) & 0xFF;
	p[0x15] = (dirent->cluster >> 24) & 0xFF;
	p[0x1C] =  size        & 0xFF;
	p[0x1D] = (size >>  8) & 0xFF;
	p[0x1E] = (size >> 16) & 0xFF;
	p[0x1F] = (size >> 24) & 0xFF;
	p[0x0B] = 0;	// file type
	time_t ts = time(NULL);
	if (ts) {
		struct tm *tm = localtime(&ts);
		if (tm) {
			p[0x16] = (tm->tm_sec >> 1) + ((tm->tm_min & 0xF) << 5);
			p[0x17] = (tm->tm_min >> 3) + (tm->tm_hour << 3);
			p[0x18] = tm->tm_mday + (((tm->tm_mon + 1) & 7) << 5);
			p[0x19] = ((tm->tm_mon + 1) >> 3) + ((tm->tm_year - 80) << 1);
			memcpy(p + 0x0E, p + 0x16, 4);
		}
	}
	if (mfat_write_cluster(stream_cache.cluster, stream_cache.cluster_block, stream_cache.buf)) {
		return 0;	// ERROR
	}
	// RETURN VALUE:
	// just calculate a DEVICE dependent block offset of the cluster.
	// Now it's the caller responsibility to simply copy anything (do NOT exceed the specified size this function was called with!)
	FATDEBUG("FAT32: allocated cluster chain from cluster %u for file %s" NL, dirent->cluster, name);
	return dirent->cluster * mfat_partitions[disk.part].cluster_size_in_blocks + mfat_partitions[disk.part].data_area_fake_ofs + mfat_partitions[disk.part].first_block;
}



int mfat_open_file_by_dirent ( mfat_dirent_t *p )
{
	if (p->cluster < 2 || p->cluster >= p->stream.partition->clusters)
		return -1;
	mfat_open_stream(&p->stream, p->cluster);
	if (!(p->type & (0x10|0x08|0x40|0x80)))
		p->stream.size_constraint = p->size;
	return 0;
}


int mfat_open_file_by_name ( mfat_dirent_t *p, const char *name, int type_filter )
{
	int ret = mfat_search_in_directory(p, name, type_filter);
	if (ret != 1)
		return ret;
	return mfat_open_file_by_dirent(p);
}


int mfat_read_file ( mfat_dirent_t *p, void *buf, int size )
{
	return mfat_read_stream(&p->stream, buf, size);
}


/* ----- UTILS ------- */

static const char *allowed_dos_name_chars = "!#$%&'()-@^_`{}~0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";	// we don't allow space, even if it's technically not illegal

#if 0
static char fat_dirent_name[8 + 3 + 1];
#endif

int mfat_fatize_name ( char *d, const char *s )
{
	int len = 8;
	int in_ext = 0;
	//if (!d)
	//	d = fat_dirent_name;
	memset(d, 0x20, 8 + 3);
	d[8 + 3] = 0;
	for (;;) {
		char c = *s++;
		if (c == 0) {
			return 0;
		} else if (c == '.') {
			if (in_ext)
				return -1;	// more than one dot in FN
			if (len == 8)
				return -1;	// file name begins with dot (we don't allow '.' and '..' reference in this func, btw)
			in_ext = 1;
			d += len;
			len = 3;
		} else {
			if (len) {
				if (c >= 'a' && c <= 'z')
					c -= 'a' - 'A';
				if (!strchr(allowed_dos_name_chars, c))
					return -1;	// not allowd character in filename
				*d++ = c;
				len--;
			}
		}
	}
}


// Again this function does not support scenario, where name contains a space
int mfat_normalize_name ( char *d, const char *s )
{
	int len = 8;
	int in_ext = 0;
	char *d_orig = d;
	for (;;) {
		char c = *s++;
		if (c == 0) {
			*d = '\0';
			return 0;
		} else if (c == '.') {
			if (in_ext)
				break;	// more than one dot in FN
			if (len == 8)
				break;	// file name begins with dot (we don't allow '.' and '..' reference in this func, btw)
			in_ext = 1;
			*d++ = '.';
			len = 3;
		} else {
			if (len) {
				if (c >= 'a' && c <= 'z')
					c -= 'a' - 'A';
				if (!strchr(allowed_dos_name_chars, c))
					break;	// not allowd character in filename
				*d++ = c;
				len--;
			}
		}
	}
	d_orig[0] = '?';
	d_orig[1] = '\0';
	return -1;
}


#ifndef XEMU_BUILD
/* ----- TEST SUITE ----- */
static int fd;

static int raw_reader ( Uint32 block, Uint8 *buf )
{
	off_t ofs = (off_t)block << 9;	// convert to byte offset
	if (lseek(fd, ofs, SEEK_SET) != ofs) {
		perror("Host seek error");
		return -1;
	}
	if (read(fd, buf, 512) != 512) {
		perror("Host read error");
		return -1;
	}
	return 0;
}

int main ( int argc, char **argv )
{
	// static const char default_fn[] = "/home/lgb/.local/share/xemu-lgb/mega65/mega65.img";
	static const char default_fn[] = "hyppo.disk";
	const char *fn = (argc > 1) ? argv[1] : default_fn;
	FATDEBUG("Disk image: %s" NL, fn);
	fd = open(fn, O_RDONLY);
	if (fd < 0) {
		perror("Open disk image");
		return 1;
	}
	off_t devsize = lseek(fd, 0, SEEK_END);
	if (devsize == OFF_T_ERROR) {
		perror("Host lseek to get device size");
		close(fd);
		return 1;
	}
	if (devsize & 511) {
		FATDEBUGPRINT("FATFS: WARNING: Host size error: size is not 512 byte aligned" NL);
		close(fd);
		return 1;
	}
	if (devsize < 16*1024*1024) {
		FATDEBUGPRINT("FATFS: WARNING: Host size error: image is too small (<16Mbytes)" NL);
		close(fd);
		return 1;
	}
	devsize >>= 9;
	if (devsize > 0x2000000) {
		FATDEBUGPRINT("FATFS: WARNING: Host size error: image is too large (>16Gbytes)" NL);
		close(fd);
		return 1;
	}
	mfat_init( raw_reader, NULL, devsize );
	int part = mfat_init_mbr();
	if (part < 0) {
		FATDEBUGPRINT("FATFS: WARNING: No partition could be detected in MBR for usage" NL);
		close(fd);
		return 1;
	}
	if (mfat_use_part(part)) {
		FATDEBUGPRINT("FATFS: WARNING: Cannot make partition #%d to be selected" NL, part);
		close(fd);
		return 1;
	}
	// STREAM TEST
	//Uint8 dirent[32];
	mfat_dirent_t dirent;
	//mfat_open_stream(&dirent.stream, mfat_partitions[part].root_dir_cluster);
	mfat_open_rootdir(&dirent.stream);
	int ret;
	int fragmented;
	ret = mfat_get_real_size(&dirent.stream, &fragmented);
	FATDEBUG("GET REAL SIZE = %d, fragmented = %d" NL, ret, fragmented);
	ret = mfat_search_in_directory(&dirent, NULL, MFAT_FIND_VOL);
	FATDEBUG("VOLUME NAME: \"%s\"" NL, ret == 1 ? dirent.fat_name : "???");
	for (int x = 0; x < 2; x++) {
		mfat_rewind_stream(&dirent.stream);
	for (;;) {
		int ret = mfat_read_directory(&dirent, 0xFF);
		if (ret != 1)
			break;
		FATDEBUG("FILE: \"%s\" size = %u cluster=%u" NL, dirent.name, dirent.size, dirent.cluster);
	}
	}
	// SEARCH TEST
	ret = mfat_search_in_directory(&dirent, "FOOD", 0xFF);
	FATDEBUG("SEARCH FOR FOOD: %d" NL, ret);
	if (!ret) {
		ret = mfat_search_in_directory(&dirent, "MEGA65.ROM", 0xFF);
		FATDEBUG("RESULT of searching file : %d" NL, ret);
		ret = mfat_search_in_directory(&dirent, "MEGA65.ROM", 0xFF);
		FATDEBUG("RESULT of searching file (AGAIN!): %d" NL, ret);
	}
	close(fd);
	puts("WOW :-)");
	return 0;
}
#endif

#endif
