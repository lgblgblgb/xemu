/* A work-in-progess Mega-65 (Commodore-65 clone origins) emulator
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

#if defined(MEGA65_BUILD) || !defined(XEMU_BUILD) || (defined(XEMU_BUILD) && !defined(XEMU_ARCH_HTML))

#ifndef MEGA65_BUILD
#	define	FDISK_SUPPORT
#endif

#ifdef XEMU_BUILD
#	include "xemu/emutools.h"
#else
#	define NL		"\n"
#	define DEBUG		printf
#	define DEBUGPRINT	printf
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

#include "fat32.h"


#define FS_MINIMAL_SIZE_IN_BLOCKS 10


static struct {
	//int (*reader)(Uint32 block, void *data);
	//int (*writer)(Uint32 block, void *data);
	mfat_io_callback_func_t reader, writer;
	Uint32	blocks;
	int	part;	// selected partition number
} disk;


struct mfat_part_st {
	Uint32	first_block;
	Uint32	last_block;
	Uint32	blocks;
	//Uint32	blocks_per_cluster;
	int	part_type;
	int	valid;			// partition seems to be valid by part_type and size and layout requirements
	int	fs_validated;		// FAT32 is validated already, all data below is filled
	Uint32	clusters;		// total number of clusters (don't forget, the two first clusters cannot be used, but it counts here!)
	Uint32	data_area_fake_ofs;	// the 'fake' offset (taking account the cluster<2 invalidity) which must be added to the converted cluster->block value
	Uint32	cluster_size_in_blocks;	// cluster size in blocks
	Uint32	fat1_start, fat2_start;
	Uint32	root_dir_cluster;
	Uint32	fs_info_block_number;
	Uint32	eoc_marker;
};


struct mfat_part_st mfat_partitions[4];


static int mfat_read_DEVICE_blk ( Uint32 block, void *buf )
{
	if (block >= disk.blocks) {
		fprintf(stderr, "Host FS: reading outside of the device\n");
		return -1;
	}
	return disk.reader(block, buf);
}

static int mfat_read_part_blk ( Uint32 block, void *buf )
{
	if (disk.part < 0) {
		fprintf(stderr, "read partition block: invalid partition is selected\n");
		return -1;
	}
	if (block >= mfat_partitions[disk.part].blocks) {
		fprintf(stderr, "read partition block: trying to read block outside of partition!\n");
		return -1;
	}
	return mfat_read_DEVICE_blk(block + mfat_partitions[disk.part].first_block, buf);
}

static int mfat_read_cluster ( Uint32 cluster, Uint32 block_in_cluster, void *buf )
{
	cluster &= 0x0FFFFFFFU;	// some well known fact, that FAT32 is actually FAT28, and the highest 4 bits should not be used!
	if (cluster < 2 || cluster >= mfat_partitions[disk.part].clusters) {
		fprintf(stderr, "read cluster: invalid cluster number %d\n", cluster);
		return -1;
	}
	if (block_in_cluster >= mfat_partitions[disk.part].cluster_size_in_blocks) {
		fprintf(stderr, "read cluster: invalid in-cluster-block data %d\n", block_in_cluster);
		return -1;
	}
	return mfat_read_part_blk(cluster * mfat_partitions[disk.part].cluster_size_in_blocks + mfat_partitions[disk.part].data_area_fake_ofs + block_in_cluster, buf);
}

#define AS_WORD(p,o)	(p[o] + (p[o+1] << 8))
#define AS_DWORD(p,o)	(p[o] + (p[o+1] << 8) + (p[o+2] << 16) + (p[o+3] << 24))


// Return value:
// 0 = end of chain [regardless of the real used EOC]
// 1 = fatal error [like I/O layer read problem ...]
// other = cluster number from the chained cluster
static Uint32 mfat_read_fat_chain ( Uint32 cluster )
{
	static Uint8 cache[512];
	static Uint32 cached_block = 0;
	cluster &= 0x0FFFFFFFU;	// some well known fact, that FAT32 is actually FAT28, and the highest 4 bits should not be used!
	if (cluster < 2 || cluster >= mfat_partitions[disk.part].clusters) {
		fprintf(stderr, "read fat: invalid cluster number %d\n", cluster);
		return 1;
	}
	Uint32 block = mfat_partitions[disk.part].fat1_start + cluster / 128;
	Uint32 ofs   = (cluster & 127) * 4;
	if (block != cached_block) {
		if (mfat_read_part_blk(block, cache))
			return 1;
		cached_block = block;
		printf("UNCACHED block: %d\n", block);
	} else
		printf("COOL, fat block is cached for %d\n", block);
	cluster = AS_DWORD(cache, ofs) & 0x0FFFFFFFU;
	printf("DEBUG: mfat_read_chain: got cluster: $%08X\n", AS_DWORD(cache, ofs));
	// in theory there is some "official" end-of-chain marker, but in reality it seems anything which is outside of normal
	// cluster number on the FS "should" be considered as end-of-chain marker
	if (cluster < 2 || cluster >= mfat_partitions[disk.part].clusters)
		return 0;
	return cluster;
}


void mfat_init ( mfat_io_callback_func_t reader, mfat_io_callback_func_t writer, Uint32 device_size )
{
	disk.reader = reader;
	disk.writer = writer;
	disk.blocks = device_size;
	disk.part = -1;
}


// Reads and parse MBR. Fills mfat_partitions array
// Returns with a canidate of the first usable partition [also partiton type], though the FS itself is NOT checked.
// returns with -1, if there is some error, or no usable partition candidate found.
// Caller must use mfat_use_part() after this function to really select a partition at FS level, by using the retval, or decide at your own.
// mfat_init() must be called before this function!
int mfat_init_mbr ( void )
{
	int first_valid = -1;
	Uint8 cache[512];
	if (mfat_read_DEVICE_blk(0, cache))	// read MBR
		return -1;
	// parse partition entries
	memset(mfat_partitions, 0, sizeof mfat_partitions);
	Uint8 *p = cache + 0x1BE;
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
		printf("Part#%d: %08X - %08X (size=%08X), type = %02X, valid=%d size=%d Mbytes\n",
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
		printf("*** %s\n", title);
	for (int b = 0 ; b < lines ; b ++) {
		printf("%03X  ", b * 32);
		for (int a = 0; a < 32; a++)
			printf("%02X ", p[a]);
		for (int a = 0; a < 32; a++)
			printf("%c", p[a] >=32 && p[a] < 127 ? p[a] : '?');
		puts("");
		p += 32;
	}
}




// Use this function to select a partition to be used. It also does a check if a partition is a FAT32 one.
// Requires: mfat_init() then mfat_init_mbr() functions before.
int mfat_use_part ( int part )
{
	int previous_part = disk.part;
	disk.part = part;
	if (part < 0 || part > 3 || !mfat_partitions[part].valid)
		goto error;
	if (mfat_partitions[part].fs_validated)	// if filesystem was validated before, we returns ...
		return 0;
	// try to validate filesystem now, if it's really FAT32, ALSO it puts the needed information into mfat_partitions[] used by the actual FS code!!!
	Uint8 cache[512];
	if (mfat_read_part_blk(0, cache))
		goto error;
	printf("Bytes per logical sector: %d\n", AS_WORD(cache,0xB));
	if (AS_WORD(cache, 0xB) != 512) {
		fprintf(stderr, "Only 512 bytes / logical sector is supported!\n");
		goto error;
	}
	printf("Logical sectors per cluster: %d (cluster size = %d bytes)\n", cache[0xD], cache[0xD] * 512);
	if (cache[0xD] == 0) {	// FIXME: better check, should be some non-zero & power of two ...
		fprintf(stderr, "Invalid logical sectors per cluster information!\n");
		goto error;
	}
	mfat_partitions[part].cluster_size_in_blocks = cache[0xD];
	printf("Reserved logical sectors: %d\n", AS_WORD(cache, 0xE));
	printf("FAT copies: %d\n", cache[0x10]);
	if (cache[0x10] != 2) {
		fprintf(stderr, "Invalid number of FAT copies!\n");
		goto error;
	}
	printf("Logical sectors by FAT: %d\n", AS_DWORD(cache, 0x24));
	printf("Cluster of ROOT dir: %d\n", AS_DWORD(cache, 0x2C));
	mfat_partitions[part].root_dir_cluster = AS_DWORD(cache, 0x2C);

	printf("FS information sector at block: %d\n", AS_WORD(cache, 0x30));
	mfat_partitions[part].fs_info_block_number = AS_WORD(cache, 0x30);


	mfat_partitions[part].fat1_start = AS_WORD(cache, 0xE);	// FAT starts after the reserved sectors area
	mfat_partitions[part].fat2_start = AS_WORD(cache, 0xE) + AS_DWORD(cache, 0x24);
	Uint32 a                         = AS_WORD(cache, 0xE) + AS_DWORD(cache, 0x24) * 2;
	mfat_partitions[part].clusters = ((mfat_partitions[part].blocks - a) / mfat_partitions[part].cluster_size_in_blocks) + 2;
	printf("Number of clusters CALCULATED (incl. 0,1): %d\n", mfat_partitions[part].clusters);
	printf("Number of clusters from FAT size: %d\n", AS_DWORD(cache, 0x24) * 128);	// FIXME: decide what number of clusters data is valid [obviously, the smaller value of both ...]

	mfat_partitions[part].data_area_fake_ofs = a - (mfat_partitions[part].cluster_size_in_blocks * 2);


	if (mfat_read_part_blk(mfat_partitions[part].fat1_start, cache))
		goto error;
	printf("FAT1 marks #0: $%08X\n", AS_DWORD(cache, 0));
	mfat_partitions[part].eoc_marker = AS_DWORD(cache, 0);
	printf("FAT1 marks #1: $%08X\n", AS_DWORD(cache, 4));
	if (mfat_read_part_blk(mfat_partitions[part].fat2_start, cache))
		goto error;
	printf("FAT2 marks #0: $%08X\n", AS_DWORD(cache, 0));
	printf("FAT2 marks #1: $%08X\n", AS_DWORD(cache, 4));
	//if (mfat_read_
	//luster(mfat_partitions[part].root_dir_cluster, 0, cache))
	//	goto error;
	//hexdump(cache, 16, "Root directory");
	a = mfat_partitions[part].root_dir_cluster;
	do {
		for (int b = 0; b < mfat_partitions[part].cluster_size_in_blocks; b++)
			if (mfat_read_cluster(a, b, cache)) {
				printf("Error following the root directory!\n");
				goto error;
			} else {
				printf("Root directory, cluster=%d/block=%d\n", a, b);
				hexdump(cache, 16, NULL);
				for (int c = 0; c < 512; c += 32) {
					if (cache[c] >= 32 && cache[c] != 0xE5 && (cache[c + 0xB] & 8)) {
						cache[c + 0xB] = 0;
						printf("VOLUME is: \"%s\"\n", cache + c);
					}
				}
			}
		a = mfat_read_fat_chain(a);
		if (a == 1)
			goto error;
	} while (a);

	// just for fun ...
	//for (a = 2; a < mfat_partitions[part].

	// FS information sector
	if (mfat_partitions[part].fs_info_block_number > 0 && mfat_partitions[part].fs_info_block_number != 0xFFFF) {
		if (mfat_read_part_blk(mfat_partitions[part].fs_info_block_number, cache))
			goto error;
		hexdump(cache, 16, "FS information sector");
		printf("FS info signature @0=$%08X @1E4=$%08X @1FC=$%08X\n",
				AS_DWORD(cache, 0), AS_DWORD(cache, 0x1E4), AS_DWORD(cache, 0x1FC)
		);
		if (AS_DWORD(cache, 0) != 0x41615252 || AS_DWORD(cache, 0x1E4) != 0x61417272 || AS_DWORD(cache, 0x1FC) != 0xAA550000U) {
			printf("*** BAD FS info sector signature(s)\n");
			goto error;
		}
	} else {
		printf("No FS information sector!\n");
		goto error;
	}
	printf("GOOD, FS seems to be intact! :-)\n");
	mfat_partitions[part].fs_validated = 1;
	return 0;
error:
	disk.part = previous_part;
	return -1;
}


/* ---- FILESYSTEM like functions, generic, even directories are just "files" with special format though ---- */


typedef struct {
	Uint32	cluster;
	int	in_cluster_block;
	int	in_block_pos;
	int	eof;
	int	size_constraint;
	int	file_pos;
	struct mfat_part_st *partition;
	Uint32	start_cluster;
} mfat_stream_t;



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


void mfat_rewind_stream ( mfat_stream_t *p )
{
	p->cluster = p->start_cluster;
	p->in_cluster_block = 0;
	p->in_block_pos = 0;
	p->eof = 0;
	p->file_pos = 0;
}


int mfat_read_stream ( mfat_stream_t *p, void *buf, int size )
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
			printf("WOW, data block has been cached for cluster %d, block %d within cluster\n", cached_cluster, cached_cluster_block);
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
			fprintf(stderr, "FATAL ERROR.\n");
			exit(1);
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


/* ---- functions for handling directories ---- */


typedef struct {
	mfat_stream_t stream;
	char	name[8+3+1];
	Uint32	cluster;
	Uint32	size;
	Uint8	type;
} mfat_dirent_t;

int mfat_read_directory ( mfat_dirent_t *p )
{
	Uint8 buf[32];
	for (;;) {
		int ret = mfat_read_stream(&p->stream, buf, 32);
		if (ret <= 0)
			return ret;
		if (ret != 32) {
			fprintf(stderr, "FATAL ERROR: dirent read 32 is not 32\n");
			exit(1);
		}
		if (buf[0] == 0) {	// marks end of directory
			p->stream.eof = 1;
			return 0;
		}
		if (buf[0] == 0xE5)	// deleted file
			continue;
		if (buf[0] < 0x20 || buf[0] > 127)	// some random stuff?
			continue;
		if ((buf[0xB] & 0xF) == 0xF)	// LFN piece? currently not supported
			continue;
		break;
	}
	memcpy(p->name, buf, 8+3);
	p->name[8+3] = 0;
	p->cluster = AS_WORD(buf, 0x1A) + (AS_WORD(buf, 0x14) << 16);
	p->size = AS_DWORD(buf, 0x1C);
	p->type = buf[0xB];
	return 1;
}


int mfat_search_in_directory ( mfat_dirent_t *p, const char *name )
{
	mfat_rewind_stream(&p->stream);
	for (;;) {
		int ret = mfat_read_directory(p);
		if (ret == 1) {
			if (!strcmp(name, p->name))
				return 1;
		} else
			return ret;
	}
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


int mfat_open_file_by_name ( mfat_dirent_t *p, const char *name )
{
	int ret = mfat_search_in_directory(p, name);
	if (ret != 1)
		return ret;
	return mfat_open_file_by_dirent(p);
}


int mfat_read_file ( mfat_dirent_t *p, void *buf, int size )
{
	return mfat_read_stream(&p->stream, buf, size);
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

int main ( void )
{
	fd = open("hyppo.disk", O_RDONLY);
	//fd = open("/home/lgb/.local/share/xemu-lgb/mega65/mega65.img", O_RDONLY);
	if (fd < -1) {
		perror("Open disk image");
		return 1;
	}
	off_t devsize = lseek(fd, 0, SEEK_END);
	if (devsize == (off_t)-1) {
		perror("Host lseek to get device size");
		close(fd);
		return 1;
	}
	if (devsize & 511) {
		fprintf(stderr, "Host size error: size is not 512 byte aligned\n");
		close(fd);
		return 1;
	}
	if (devsize < 16*1024*1024) {
		fprintf(stderr, "Host size error: image is too small (<16Mbytes)\n");
		close(fd);
		return 1;
	}
	devsize >>= 9;
	if (devsize > 0x2000000) {
		fprintf(stderr, "Host size error: image is too large (>16Gbytes)\n");
		close(fd);
		return 1;
	}
	mfat_init( raw_reader, NULL, devsize );
	int part = mfat_init_mbr();
	if (part < 0) {
		fprintf(stderr, "No partition could be detected in MBR for usage\n");
		close(fd);
		return 1;
	}
	if (mfat_use_part(part)) {
		fprintf(stderr, "Cannot make partition #%d to be selected\n", part);
		close(fd);
		return 1;
	}
	// STREAM TEST
	//Uint8 dirent[32];
	mfat_dirent_t dirent;
	mfat_open_stream(&dirent.stream, mfat_partitions[part].root_dir_cluster);
	for (;;) {
		int ret = mfat_read_directory(&dirent);
		if (ret != 1)
			break;
		printf("FILE: \"%s\"\n", dirent.name);
	}
	close(fd);
	puts("WOW :-)");
	return 0;
}
#endif

#endif
