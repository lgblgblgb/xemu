/* A work-in-progess MEGA65 (Commodore 65 clone origins) emulator
   Part of the Xemu project, please visit: https://github.com/lgblgblgb/xemu
   Copyright (C)2016-2023 LGB (Gábor Lénárt) <lgblgblgb@gmail.com>

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

#include "xemu/emutools.h"
#include "xemu/emutools_files.h"
#include "sdcard.h"
#include "xemu/f011_core.h"
#include "xemu/d81access.h"
#include "mega65.h"
#include "xemu/cpu65.h"
#include "io_mapper.h"
#include "sdcontent.h"
#include "memcontent.h"
#include "hypervisor.h"
#include "vic4.h"
#include "configdb.h"
#include "xemu/emutools_config.h"

#include <sys/types.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>

#define COMPRESSED_SD
#define USE_KEEP_BUSY

#ifdef USE_KEEP_BUSY
#	define KEEP_BUSY(n)	keep_busy=n
#else
#	define KEEP_BUSY(n)
#endif

#define SD_ST_SDHC	0x10

// FIXME: invent same sane value here (the perfect solution would be to use sdcontent/mfat subsystem to query the boundaries of the FAT partitions)
#define MIN_MOUNT_SECTOR_NO 10

#define SD_BUFFER_POS 0x0E00
#define FD_BUFFER_POS 0x0C00


static Uint8	sd_regs[0x30];
static int	sdfd;			// SD-card controller emulation, UNIX file descriptor of the open image file
Uint8		sd_status;		// SD-status byte
static int	sdhc_mode = 1;
static Uint32	sdcard_size_in_blocks;	// SD card size in term of number of 512 byte blocks
static int	sd_fill_mode = 0;
static Uint8	sd_fill_value = 0;
#ifdef COMPRESSED_SD
static int	sd_compressed = 0;
static Uint32	*sd_pagedir = NULL;
static int	sd_unpack_buffer_size;
#endif
static int	sd_is_read_only;
#ifdef USE_KEEP_BUSY
static int	keep_busy = 0;
#endif
// 4K buffer space: Actually the SD buffer _IS_ inside this, also the F011 buffer should be (FIXME: that is not implemented yet right now!!)
Uint8		disk_buffers[0x1000];
static Uint8	sd_fill_buffer[512];	// Only used by the sd fill mode write command
Uint8		*disk_buffer_cpu_view;
// FIXME/TODO: unfortunately it seems I/O mapping of SD-buffer is buggy even on
// real MEGA65 and the new code to have mapping allowing FD _and_ SD buffer mount
// causes problems. So let's revert back to a fixed "SD-only" solution for now.
Uint8		*disk_buffer_io_mapped = disk_buffers + SD_BUFFER_POS;
// "External mount area". Will be initialized later in sdcard_set_external_mount_pool(). This range should be enough for _two_ D65 images.
// If outside of SD-card size, then it's simply "virtual" only.
Uint32		sd_external_mount_area_start = 0;
Uint32		sd_external_mount_area_end = 0;

static const char	*default_d81_basename[2]	= { "mega65.d81",	"mega65_9.d81"    };
static const char	*default_d81_disk_label[2]	= { "XEMU EXTERNAL",	"XEMU EXTERNAL 9" };
const char		xemu_external_d81_signature[]	= "\xFF\xFE<{[(XemuExternalDiskMagic)]}>";

#if (D65_SIZE & 511) || (D81_SIZE & 511) || (D71_SIZE & 511) || !(D64_SIZE & 511) || (D64_SIZE & 255)
#error "Bad Dxx_SIZE"
#endif

// No need to specify D81ACCESS_D81, that's always "included"
// Used by do_external_mount() to tell the d81access layer, what formats we want to support
// TODO: D81ACCESS_D65 is NOT included here since even the d81access layer would need finished support for that, etc.
#define EXTERNAL_MOUNT_FORMAT_SUPPORT_FLAGS	D81ACCESS_D64|D81ACCESS_D71

#define MOUNT_TYPE_EMPTY	0
#define MOUNT_TYPE_INTERNAL	1
#define MOUNT_TYPE_EXTERNAL	2

// Do NOT modify the numbers, other than IDs, they are used like bitfields etc too, must be axactly these values!
// 2 bit value for real
#define MOUNT_ACM_D81		0
#define MOUNT_ACM_D64		1
#define MOUNT_ACM_D65		2
#define MOUNT_ACM_D71		3
// Must be the same order as the MOUNT_ACM_* numerical values are!
static const char  *acm_names[4]	= { "D81",                  "D64",               "D65",         "D71" };
static const int    acm_d81consts[4]	= { 0,             D81ACCESS_D64,       D81ACCESS_D65, D81ACCESS_D71  };
static const Uint32 acm_sectors[4]	= { D81_SIZE >> 9, (D64_SIZE >> 9) + 1, D65_SIZE >> 9, D71_SIZE >> 9  };	// D64 image size is not divisible by 512!

// Disk image mount information for the two units
static struct {
	int	type;		// mount type, see MOUNT_TYPE_* defines above
	char	*desc;		// descriptive info on mount not used directly in emulation but only for information query purposes
	char	*last_ext_fn;	// last (maybe not current mount!) successfull external mount image filename
	Uint32	sector;		// valid only if "internal", SD sector of the mount
	Uint32	sector_initial;	// initial disk image at SD-sector, if non-zero
	Uint32	sector_fake;	// fake sector number for external mount
	int	acm;		// MOUNT_ACM_* constants, "access mode"
	int	acm_initial;	// the same as above for the initial disk image, which should be MOUNT_ACM_D81 normally very much
	int	read_only;	// mount is read-only
	int	image_size;	// mount size in bytes
	Uint32	first_sector;	// first SD sector the mount belongs to
	Uint32	last_sector;	// last SD sector the mount belongs to
	Uint32	partial_sector;	// partial SD sector the mount belongs to (ie, mount size - D64 - is not divisible by 512)
} mount_info[2];

#ifdef VIRTUAL_DISK_IMAGE_SUPPORT

#define VIRTUAL_DISK_BLOCKS_PER_CHUNK	2048
// Simulate a 4Gbyte card for virtual disk (the number is: number of blocks). Does not matter since only the actual written blocks stored in memory from it
#define VIRTUAL_DISK_SIZE_IN_BLOCKS	8388608U

#define RANGE_MAP_SIZE 256

struct virtdisk_chunk_st {
	struct	virtdisk_chunk_st *next;	// pointer to the next chunk, or NULL, if it was the last (in that case vdisk.tail should point to this chunk)
	int	used_blocks;			// number of used blocks in this chunk (up to vidsk.blocks_per_chunk)
	Uint32	block_no_min;			// optimization: lowest numbered block inside _this_ chunk of blocks
	Uint32	block_no_max;			// optimization: highest numbered block inside _this_ chunk of blocks
	Uint8	*base;				// base pointer of the data area on this chunk, 512 bytes per block then
	Uint32	list[];				// block number list for this chunk (see: used_blocks) [C99/C11 flexible array members syntax]
};
struct virtdisk_st {
	struct	virtdisk_chunk_st *head, *tail;	// pointers to the head and tail of the linked list of chunks, or NULL for both, if there is nothing yet
	int	blocks_per_chunk;		// number of blocks handled by a chunk
	int	allocation_size;		// pre-calculated to have number of bytes allocated for a chunk, including the data area + struct itself
	int	base_offset;			// pre-calculated to have the offset to the data area inside a chunk
	int	all_chunks;			// number of all currently allocated chunks
	int	all_blocks;			// number of all currently USED blocks within all chunks
	Uint8	range_map[RANGE_MAP_SIZE];	// optimization: store here if a given range is stored in memory, eliminating the need to search all chunks over, if not
	Uint32	range_map_divisor;		// optimization: for the above, to form range number from block number
	int	mode;				// what mode is used, 0=no virtual disk
};

static struct virtdisk_st vdisk = { .head = NULL };

static void virtdisk_destroy ( void )
{
	if (vdisk.head) {
		struct virtdisk_chunk_st *v = vdisk.head;
		int ranges = 0;
		for (unsigned int a = 0; a < RANGE_MAP_SIZE; a++)
			for (Uint8 m = 0x80; m; m >>= 1)
				if ((vdisk.range_map[a] & m))
					ranges++;
		DEBUGPRINT("SDCARD: VDISK: destroying %d chunks (active data: %d blocks, %dKbytes, %d%%, ranges: %d/%d) of storage." NL,
			vdisk.all_chunks, vdisk.all_blocks, vdisk.all_blocks >> 1,
			100 * vdisk.all_blocks / (vdisk.all_chunks * vdisk.blocks_per_chunk),
			ranges, RANGE_MAP_SIZE * 8
		);
		while (v) {
			struct virtdisk_chunk_st *next = v->next;
			free(v);
			v = next;
		}
	}
	vdisk.head = NULL;
	vdisk.tail = NULL;
	vdisk.all_chunks = 0;
	vdisk.all_blocks = 0;
	memset(vdisk.range_map, 0, RANGE_MAP_SIZE);
}


static void virtdisk_init ( int blocks_per_chunk, Uint32 total_number_of_blocks )
{
	virtdisk_destroy();
	vdisk.blocks_per_chunk = blocks_per_chunk;
	vdisk.base_offset = sizeof(struct virtdisk_chunk_st) + (sizeof(Uint32) * blocks_per_chunk);
	vdisk.allocation_size = (blocks_per_chunk << 9) + vdisk.base_offset;
	vdisk.range_map_divisor = (total_number_of_blocks / (RANGE_MAP_SIZE * 8)) + 1;
	DEBUGPRINT("SDCARD: VDISK: %d blocks (%dKbytes) per chunk, range-divisor is %u" NL, blocks_per_chunk, blocks_per_chunk >> 1, vdisk.range_map_divisor);
}


// Note: you must take care to call this function with "block" not outside of the desired capacity or some logic (range_map) will cause crash.
// That's not a problem, as this function is intended to use as storage backend, thus boundary checking should be done BEFORE you call this!
static Uint8 *virtdisk_search_block ( Uint32 block, int do_allocate )
{
	struct virtdisk_chunk_st *v;
	Uint32 range_index = block / vdisk.range_map_divisor;
	Uint8  range_mask  = 1U << (range_index & 7U);
	range_index >>= 3U;
	if (XEMU_LIKELY(vdisk.head && (vdisk.range_map[range_index] & range_mask))) {
		v = vdisk.head;
		do {
			if (block >= v->block_no_min && block <= v->block_no_max)
				for (unsigned int a = 0; a < v->used_blocks; a++)
					if (v->list[a] == block)
						return v->base + (a << 9);
			v = v->next;
		} while (v);
	}
	// if we can't found the block, and do_allocate is false, we return with zero
	// otherwise we continue to allocate a block
	if (!do_allocate)
		return NULL;
	//DEBUGPRINT("RANGE-INDEX=%d RANGE-MASK=%d" NL, range_index, range_mask);
	// We're instructed to allocate block if cannot be found already elsewhere
	// This condition checks if we have room in "tail" or there is any chunk already at all (tail is not NULL)
	if (vdisk.tail && (vdisk.tail->used_blocks < vdisk.blocks_per_chunk)) {
		v = vdisk.tail;
		// OK, we had room in the tail, so put the block there
		vdisk.all_blocks++;
		if (block < v->block_no_min)
			v->block_no_min = block;
		if (block > v->block_no_max)
			v->block_no_max = block;
		vdisk.range_map[range_index] |= range_mask;
		v->list[v->used_blocks] = block;
		return v->base + ((v->used_blocks++) << 9);
	}
	// we don't have room in the tail, or not any chunk yet AT ALL!
	// Let's allocate a new chunk, and use the first block from it!
	v = xemu_malloc(vdisk.allocation_size);	// xemu_malloc() is safe, it malloc()s space or abort the whole program if it cannot ...
	v->next = NULL;
	v->base = (Uint8*)v + vdisk.base_offset;
	vdisk.all_chunks++;
	if (vdisk.tail)
		vdisk.tail->next = v;
	vdisk.tail = v;
	if (!vdisk.head)
		vdisk.head = v;
	v->list[0] = block;
	v->used_blocks = 1;
	v->block_no_min = block;
	v->block_no_max = block;
	vdisk.range_map[range_index] |= range_mask;
	vdisk.all_blocks++;
	return v->base;	// no need to add block offset in storage, always the first block is served in a new chunk!
}


static inline void virtdisk_write_block ( Uint32 block, Uint8 *buffer )
{
	// Check if the block is all zero. If yes, we can omit write if the block is not cached
	Uint8 *vbuf = virtdisk_search_block(block, has_block_nonzero_byte(buffer));
	if (vbuf)
		memcpy(vbuf, buffer, 512);
	// TODO: Like with the next function, this whole strategy needs to be changed when mixed operation is used!!!!!
}


static inline void virtdisk_read_block ( Uint32 block, Uint8 *buffer )
{
	Uint8 *vbuf = virtdisk_search_block(block, 0);
	if (vbuf)
		memcpy(buffer, vbuf, 512);
	else
		memset(buffer, 0, 512);	// if not found, we fake an "all zero" answer (TODO: later in mixed operation, image+vdisk, this must be modified!)
}
#endif


static void sdcard_shutdown ( void )
{
	d81access_close_all();
	if (sdfd >= 0) {
		close(sdfd);
		sdfd = -1;
	}
#ifdef VIRTUAL_DISK_IMAGE_SUPPORT
	virtdisk_destroy();
#endif
}


static XEMU_INLINE Uint32 U8A_TO_U32 ( const Uint8 *a )
{
	return ((Uint32)a[0]) | ((Uint32)a[1] << 8) | ((Uint32)a[2] << 16) | ((Uint32)a[3] << 24);
}


static XEMU_INLINE void U32_TO_U8A ( Uint8 *a, const Uint32 d )
{
	a[0] =  d        & 0xFF;
	a[1] = (d >>  8) & 0xFF;
	a[2] = (d >> 16) & 0xFF;
	a[3] = (d >> 24) & 0xFF;
}


#ifdef COMPRESSED_SD
static int detect_compressed_image ( const int fd )
{
	static const char compressed_marker[] = "XemuBlockCompressedImage001";
	Uint8 hdr[sizeof(compressed_marker) + 64];
	if (lseek(fd, 0, SEEK_SET) != (off_t)0 || xemu_safe_read(fd, hdr, sizeof(hdr)) != sizeof(hdr))
		return -1;
	// Check for compressed signature
	if (memcmp(hdr, compressed_marker, sizeof compressed_marker)) {
		DEBUGPRINT("SDCARD: image is not compressed" NL);
		return 0;
	}
	// Process header info
	const unsigned int pages =		U8A_TO_U32(hdr + sizeof(compressed_marker)     );
	const unsigned int pagedir_base =	U8A_TO_U32(hdr + sizeof(compressed_marker) +  4);
	sd_unpack_buffer_size =			U8A_TO_U32(hdr + sizeof(compressed_marker) +  8);
	const unsigned int pagedir_length =	U8A_TO_U32(hdr + sizeof(compressed_marker) + 12);
	unsigned int data_offset =		U8A_TO_U32(hdr + sizeof(compressed_marker) + 16);
	sdcard_size_in_blocks = pages << 7;
	// Process the page offsets
	if (lseek(fd, pagedir_base, SEEK_SET) != (off_t)pagedir_base)
		return -1;
	Uint8 *buf = xemu_malloc(pagedir_length + 1);
	if (xemu_safe_read(fd, buf, pagedir_length + 1) != pagedir_length) {	// we try to read one byte MORE to check if there are no unknown extra bytes at the end
		free(buf);
		return -1;
	}
	sd_pagedir = xemu_realloc(sd_pagedir, sizeof(Uint32) * (pages + 1));
	for (unsigned int i = 0, o = 0; i != pagedir_length || o != pages; i += 3) {
		if (i + 2 >= pagedir_length)
			goto unpack_error;
		const unsigned int n = buf[i] + (buf[i + 1] << 8) + (buf[i + 2] << 16);
		if (!(n & 0x800000U)) {
			if (o >= pages)
				goto unpack_error;
			sd_pagedir[o++] = data_offset;
			data_offset += n;
		} else {
			for (unsigned int j = 0; j < (n & 0xFFFF); j++) {
				if (o >= pages)
					goto unpack_error;
				sd_pagedir[o++] = data_offset;
				data_offset += (n >> 16) & 0x7F;
			}
		}
	}
	sd_pagedir[pages]  = data_offset;	// we need pages+1 elements in the array, see read_compressed_block() later
	free(buf);
	// Done
	DEBUGPRINT("SDCARD: compressed image with %u 64K-pages, max packed page size is %u bytes." NL, pages, sd_unpack_buffer_size);
	sd_is_read_only = O_RDONLY;	// set mode to R/O!
	return 1;
unpack_error:
	free(buf);
	ERROR_WINDOW("SDCARD: **ERROR** compressed SD image page directory unpacking error" NL);
	return -1;
}
#endif


Uint32 sdcard_get_size ( void )
{
	return sdcard_size_in_blocks;
}


static XEMU_INLINE void set_disk_buffer_cpu_view ( void )
{
	disk_buffer_cpu_view =  disk_buffers + ((sd_regs[9] & 0x80) ? SD_BUFFER_POS : FD_BUFFER_POS);
}


static void card_init_done ( void )
{
	DEBUGPRINT("SDCARD: card init done, size=%u Mbytes (%s), virtsd_mode=%s, default_D81_from_sd=%d" NL,
		sdcard_size_in_blocks >> 11,
		sd_is_read_only ? "R/O" : "R/W",
#ifdef VIRTUAL_DISK_IMAGE_SUPPORT
		vdisk.mode ? "IN-MEMORY-VIRTUAL" : "image-file",
#else
		"NOT-SUPPORTED",
#endif
		configdb.defd81fromsd
	);
	sdcard_set_external_mount_pool(sdcard_size_in_blocks);
}


int sdcard_init ( const char *fn, const int virtsd_flag )
{
	memset(sd_regs, 0, sizeof sd_regs);			// reset all registers
	memcpy(D6XX_registers + 0x80, sd_regs, sizeof sd_regs);	// be sure, this is in sync with the D6XX register backend (used by io_mapper which also calls us ...)
	set_disk_buffer_cpu_view();	// make sure to initialize disk_buffer_cpu_view based on current sd_regs[9] otherwise disk_buffer_cpu_view may points to NULL when referenced!
	for (int a = 0; a < 2; a++) {
		mount_info[a].type = MOUNT_TYPE_EMPTY;
		mount_info[a].desc = NULL;
		mount_info[a].last_ext_fn = NULL;
		mount_info[a].sector = 0;
		mount_info[a].sector_initial = 0;
		mount_info[a].acm = MOUNT_ACM_D81;
		mount_info[a].acm_initial = MOUNT_ACM_D81;
		mount_info[a].read_only = 0;
		mount_info[a].image_size = 0;
		mount_info[a].image_size = 0;
		mount_info[a].first_sector = 1;	// making first sector larger than last (impossible)
		mount_info[a].last_sector = 0;
		mount_info[a].partial_sector = 0;
	}
	int just_created_image_file =  0;	// will signal to format image automatically for the user (if set, by default it's clear, here)
	char fnbuf[PATH_MAX + 1];
#ifdef VIRTUAL_DISK_IMAGE_SUPPORT
	if (virtsd_flag) {
		virtdisk_init(VIRTUAL_DISK_BLOCKS_PER_CHUNK, VIRTUAL_DISK_SIZE_IN_BLOCKS);
		vdisk.mode = 1;
	} else {
		vdisk.mode = 0;
	}
#endif
	d81access_init();
	atexit(sdcard_shutdown);
	fdc_init(disk_buffers + FD_BUFFER_POS);	// initialize F011 emulation
	KEEP_BUSY(0);
	sd_status = 0;
	memset(sd_fill_buffer, sd_fill_value, 512);
#ifdef VIRTUAL_DISK_IMAGE_SUPPORT
	if (vdisk.mode) {
		sdfd = -1;
		sd_is_read_only = 0;
		sdcard_size_in_blocks = VIRTUAL_DISK_SIZE_IN_BLOCKS;
#ifdef COMPRESSED_SD
		sd_compressed = 0;
#endif
		card_init_done();
#ifdef SD_CONTENT_SUPPORT
		sdcontent_handle(sdcard_size_in_blocks, fn, SDCONTENT_FORCE_FDISK);
#endif
		return 0;
	}
#endif
retry:
	sd_is_read_only = O_RDONLY;
	sdfd = xemu_open_file(fn, O_RDWR, &sd_is_read_only, fnbuf);
	sd_is_read_only = (sd_is_read_only != XEMU_OPEN_FILE_FIRST_MODE_USED);
	if (sdfd < 0) {
		int r = errno;
		ERROR_WINDOW("Cannot open SD-card image %s, SD-card access won't work! ERROR: %s", fnbuf, strerror(r));
		DEBUG("SDCARD: cannot open image %s" NL, fn);
		if (r == ENOENT && !strcmp(fn, SDCARD_NAME)) {
			r = QUESTION_WINDOW(
				"No|Yes"
				,
				"Default SDCARD image does not exist. Would you like me to create one for you?\n"
				"Note: it will be a 4Gbytes long file, since this is the minimal size for an SDHC card,\n"
				"what MEGA65 needs. Do not worry, it's a 'sparse' file on most modern OSes which does\n"
				"not takes as much disk space as its displayed size suggests.\n"
				"This is unavoidable to emulate something uses an SDHC-card."
			);
			if (r) {
				r = xemu_create_large_empty_file(fnbuf, 4294967296UL, 1);
				if (r) {
					ERROR_WINDOW("Couldn't create SD-card image file (hint: do you have enough space?)\nError message was: %s", strerror(r));
				} else {
					just_created_image_file = 1;	// signal the rest of the code, that we have a brand new image file, which can be safely auto-formatted even w/o asking the user
					goto retry;
				}
			}
		}
	} else {
		if (sd_is_read_only)
			INFO_WINDOW("SDCARD: image file %s could be open only in R/O mode!", fnbuf);
		else
			DEBUG("SDCARD: image file re-opened in RD/WR mode, good" NL);
		// Check size!
		DEBUG("SDCARD: cool, SD-card image %s (as %s) is open" NL, fn, fnbuf);
		off_t size_in_bytes = xemu_safe_file_size_by_fd(sdfd);
		if (size_in_bytes == OFF_T_ERROR) {
			ERROR_WINDOW("Cannot query the size of the SD-card image %s, SD-card access won't work! ERROR: %s", fn, strerror(errno));
			close(sdfd);
			sdfd = -1;
			return sdfd;
		}
		sdcard_size_in_blocks = size_in_bytes >> 9;	// sdcard_size_in_blocks will be overwritten later in detect_compressed_image(), if it's a compressed image!
#ifdef COMPRESSED_SD
		sd_compressed = detect_compressed_image(sdfd);
		if (sd_compressed < 0) {
			ERROR_WINDOW("Error while trying to detect compressed SD-image");
			sdcard_size_in_blocks = 0; // just cheating to trigger error handling later
		} else if (sd_compressed > 0)
			goto no_check_compressed_card;
#endif
		DEBUG("SDCARD: detected size in Mbytes: %d" NL, (int)(size_in_bytes >> 20));
		if (size_in_bytes < 67108864UL) {
			ERROR_WINDOW("SD-card image is too small! Min required size is 64Mbytes!");
			close(sdfd);
			sdfd = -1;
			return sdfd;
		}
		if (size_in_bytes & (off_t)511) {
			ERROR_WINDOW("SD-card image size is not multiple of 512 bytes!!");
			close(sdfd);
			sdfd = -1;
			return sdfd;
		}
		if (size_in_bytes > 34359738368UL) {
			ERROR_WINDOW("SD-card image is too large! Max allowed size is 32Gbytes!");
			close(sdfd);
			sdfd = -1;
			return sdfd;
		}
	}
no_check_compressed_card:
	if (sdfd >= 0) {
		card_init_done();
		//sdcontent_handle(sdcard_size_in_blocks, NULL, SDCONTENT_ASK_FDISK | SDCONTENT_ASK_FILES);
		if (just_created_image_file) {
			just_created_image_file = 0;
			// Just created SD-card image file by Xemu itself! So it's nice if we format it for the user at this point!
#ifdef SD_CONTENT_SUPPORT
			if (!sdcontent_handle(sdcard_size_in_blocks, NULL, SDCONTENT_FORCE_FDISK)) {
				INFO_WINDOW("Your just created SD-card image file has\nbeen auto-fdisk/format'ed by Xemu. Great :).");
				sdcontent_write_rom_stub();
			}
#endif
		}
	}
#ifdef SD_CONTENT_SUPPORT
	if (!virtsd_flag && sdfd >= 0) {
		static const char msg[] = " on the SD-card image.\nPlease use UI menu: Disks -> SD-card -> Update files ...\nUI can be accessed with right mouse click into the emulator window.";
		int r = sdcontent_check_xemu_signature();
		if (r < 0) {
			ERROR_WINDOW("Warning! Cannot read SD-card to get Xemu signature!");
		} else if (r == 0) {
			INFO_WINDOW("Cannot find Xemu's signature%s", msg);
		} else if (r < MEMCONTENT_VERSION_ID) {
			INFO_WINDOW("Xemu's singature is too old%s to upgrade", msg);
		} else if (r > MEMCONTENT_VERSION_ID) {
			INFO_WINDOW("Xemu's signature is too new%s to DOWNgrade", msg);
		}
	}
#endif
	return sdfd;
}


static int host_seek ( const Uint32 block )
{
	if (sdfd < 0)
		FATAL("host_seek is called with invalid sdfd!");	// FIXME: this check can go away, once we're sure it does not apply!
	off_t offset = (off_t)block << 9;
	if (lseek(sdfd, offset, SEEK_SET) != offset)
		FATAL("SDCARD: SEEK: image seek host-OS failure: %s", strerror(errno));
	return 0;
}


// static int status_read_counter = 0;

// This tries to emulate the behaviour, that at least another one status query
// is needed to BUSY flag to go away instead of with no time. DUNNO if it is needed at all.
static Uint8 sdcard_read_status ( void )
{
	Uint8 ret = sd_status;
	DEBUG("SDCARD: reading SD status $D680 result is $%02X PC=$%04X" NL, ret, cpu65.pc);
//	if (status_read_counter > 20) {
//		sd_status &= ~(SD_ST_BUSY1 | SD_ST_BUSY0);
//		status_read_counter = 0;
//		DEBUGPRINT(">>> SDCARD resetting status read counter <<<" NL);
//	}
//	status_read_counter++;
	// Suggested by @Jimbo on MEGA65/Xemu Discord: a workaround to report busy status
	// if external SD bus is used, always when reading status. It seems to be needed now
	// with newer hyppo, otherwise it misinterprets the SDHC detection method on the external bus!
	if (ret & SD_ST_EXT_BUS)
		ret |= SD_ST_BUSY1 | SD_ST_BUSY0;
#ifdef USE_KEEP_BUSY
	if (!keep_busy)
		sd_status &= ~(SD_ST_BUSY1 | SD_ST_BUSY0);
#endif
	//ret |= 0x10;	// FIXME? according to Paul, the old "SDHC" flag stuck to one always from now
	return ret;
}


// TODO: later we need to deal with buffer selection, whatever
static XEMU_INLINE Uint8 *get_buffer_memory ( const int is_write )
{
	// Currently the only buffer available in Xemu is the SD buffer, UNLESS it's a write operation and "fill mode" is used
	// (sd_fill_buffer is just filled with a single byte value)
	return (is_write && sd_fill_mode) ? sd_fill_buffer : (disk_buffers + SD_BUFFER_POS);
}


#ifdef COMPRESSED_SD
static int read_compressed_block ( const Uint32 block, Uint8 *buffer )
{
	const unsigned int page_no = block >> 7;	// "block" is 512 byte based, so to get 64K based page, we need 7 more shifts
	static unsigned int cached_page = UINT_MAX;
	static Uint8 *page_cache = NULL;
	static Uint8 *unpack_buffer = NULL;
	static int unpack_buffer_allocated = -1;
	if (XEMU_UNLIKELY(!page_cache))
		page_cache = xemu_malloc(0x10000);
	if (XEMU_UNLIKELY(unpack_buffer_allocated != sd_unpack_buffer_size)) {
		DEBUGPRINT("SDCARD: allocating unpack buffer for %d bytes" NL, sd_unpack_buffer_size);
		unpack_buffer = xemu_realloc(unpack_buffer, sd_unpack_buffer_size);
		unpack_buffer_allocated = sd_unpack_buffer_size;
	}
	if (cached_page != page_no) {
		const unsigned int img_ofs = sd_pagedir[page_no];
		const unsigned int pck_siz = sd_pagedir[page_no + 1] - img_ofs;
		if (XEMU_UNLIKELY(pck_siz > sd_unpack_buffer_size))
			FATAL("Compressed SD unpack fatal error: too large unpack request");
		if (!pck_siz) {
			memset(page_cache, 0, 0x10000);
			DEBUGPRINT("SDCARD: CACHE: miss-zeroed" NL);
		} else {
			DEBUGPRINT("SDCARD: CACHE: miss" NL);
			if (lseek(sdfd, img_ofs, SEEK_SET) != (off_t)img_ofs)
				goto seek_error;
			if (xemu_safe_read(sdfd, unpack_buffer, pck_siz) != pck_siz)
				goto read_error;
			memset(page_cache, unpack_buffer[0], 0x10000);
			for (unsigned int i = 1, o = 0; i < pck_siz;) {
				const Uint8 c = unpack_buffer[i++];
				if (c == unpack_buffer[0]) {
					const Uint8 v = unpack_buffer[i++];
					unsigned int n = unpack_buffer[i++];
					if (!n) {
						n = unpack_buffer[i] + (unpack_buffer[i + 1] << 8);
						i += 2;
					}
					if (v != c)
						memset(page_cache + o, v, n);
					o += n;
				} else
					page_cache[o++] = c;
			}
		}
		cached_page = page_no;
	} else {
		DEBUGPRINT("SDCARD: CACHE: hit!" NL);
	}
	memcpy(buffer, page_cache + ((block & 127) << 9), 512);
	return 0;
seek_error:
	FATAL("SDCARD: SEEK: compressed image seek host-OS failure: %s", strerror(errno));
	return -1;
read_error:
	FATAL("SDCARD: READ: compressed image read host-OS failure: %s", strerror(errno));
	return -1;
}
#endif


int sdcard_read_block ( const Uint32 block, Uint8 *buffer )
{
	if (block >= sdcard_size_in_blocks) {
		DEBUGPRINT("SDCARD: SEEK: invalid block was requested to READ: block=%u (max_block=%u) @ PC=$%04X" NL, block, sdcard_size_in_blocks, cpu65.pc);
		return -1;
	}
	if (XEMU_UNLIKELY(block >= sd_external_mount_area_start && block <= sd_external_mount_area_end)) {
		for (int unit = 0; unit < 2; unit++)
			if (mount_info[unit].type == MOUNT_TYPE_EXTERNAL && block >= mount_info[unit].first_sector && block <= mount_info[unit].last_sector)
				return d81access_read_sect_raw(unit, buffer, (block - mount_info[unit].first_sector) << 9, 512, block == mount_info[unit].partial_sector ? 256 : 512);
		memset(buffer, 0xFF, 512);
		return 0;
	}
#ifdef COMPRESSED_SD
	if (sd_compressed)
		return read_compressed_block(block, buffer);
#endif
#ifdef VIRTUAL_DISK_IMAGE_SUPPORT
	if (vdisk.mode) {
		virtdisk_read_block(block, buffer);
		return 0;
	}
#endif
	if (host_seek(block))
		return -1;
	if (xemu_safe_read(sdfd, buffer, 512) == 512)
		return 0;
	else
		return -1;
}


int sdcard_write_block ( const Uint32 block, Uint8 *buffer )
{
	if (block >= sdcard_size_in_blocks) {
		DEBUGPRINT("SDCARD: SEEK: invalid block was requested to WRITE: block=%u (max_block=%u) @ PC=$%04X" NL, block, sdcard_size_in_blocks, cpu65.pc);
		return -1;
	}
	if (XEMU_UNLIKELY(block >= sd_external_mount_area_start && block <= sd_external_mount_area_end)) {
		for (int unit = 0; unit < 2; unit++)
			if (mount_info[unit].type == MOUNT_TYPE_EXTERNAL && block >= mount_info[unit].first_sector && block <= mount_info[unit].last_sector)
				return d81access_write_sect_raw(unit, buffer, (block - mount_info[unit].first_sector) << 9, 512, block == mount_info[unit].partial_sector ? 256 : 512);
		return 0;
	}
	if (sd_is_read_only)	// on compressed SD image, it's also set btw
		return -1;	// read-only SD-card
#ifdef VIRTUAL_DISK_IMAGE_SUPPORT
	if (vdisk.mode) {
		virtdisk_write_block(block, buffer);
		return 0;
	}
#endif
	if (host_seek(block))
		return -1;
	if (xemu_safe_write(sdfd, buffer, 512) == 512)
		return 0;
	else
		return -1;
}


/* Lots of TODO's here:
 * + study M65's quite complex error handling behaviour to really match ...
 * + In general: SD emulation is "too fast" done in zero emulated CPU time, which can affect the emulation badly if an I/O-rich task is running on Xemu/M65
 * */
static void sdcard_block_io ( const Uint32 block, const int is_write )
{
	static int protect_important_blocks = 1;
	DEBUG("SDCARD: %s block #%u @ PC=$%04X" NL,
		is_write ? "writing" : "reading",
		block, cpu65.pc
	);
#ifdef SD_CONTENT_SUPPORT
	if (XEMU_UNLIKELY(is_write && (block == 0 || block == XEMU_INFO_SDCARD_BLOCK_NO) && sdfd >= 0 && protect_important_blocks)) {
#else
	if (XEMU_UNLIKELY(is_write &&  block == 0                                        && sdfd >= 0 && protect_important_blocks)) {
#endif
		if (protect_important_blocks == 2) {
			goto error;
		} else {
			char msg[128];
			sprintf(msg, "Program tries to overwrite SD sector #%d!\nUnless you fdisk/format your card, it's not something you want.", block);
			switch (QUESTION_WINDOW("Reject this|Reject all|Allow this|Allow all", msg)) {
				case 0:
					goto error;
				case 1:
					protect_important_blocks = 2;
					goto error;
				case 2:
					break;
				case 3:
					protect_important_blocks = 0;
					break;
			}
		}
	}
	if (XEMU_UNLIKELY(sd_status & SD_ST_EXT_BUS)) {
		DEBUGPRINT("SDCARD: bus #1 is empty" NL);
		// FIXME: what kind of error we should create here?????
		sd_status |= SD_ST_ERROR | SD_ST_FSM_ERROR | SD_ST_BUSY1 | SD_ST_BUSY0;
		KEEP_BUSY(1);
		return;
	}
	Uint8 *buffer = get_buffer_memory(is_write);
	int ret = is_write ? sdcard_write_block(block, buffer) : sdcard_read_block(block, buffer);
	if (ret || !sdhc_mode) {
	error:
		sd_status |= SD_ST_ERROR | SD_ST_FSM_ERROR; // | SD_ST_BUSY1 | SD_ST_BUSY0;
			sd_status |= SD_ST_BUSY1 | SD_ST_BUSY0;
			//KEEP_BUSY(1);
		return;
	}
	sd_status &= ~(SD_ST_ERROR | SD_ST_FSM_ERROR);
}


static void sdcard_command ( const Uint8 cmd )
{
	static Uint32 multi_io_block;
	static Uint8 sd_last_ok_cmd;
	DEBUG("SDCARD: writing command register $D680 with $%02X PC=$%04X" NL, cmd, cpu65.pc);
	sd_status &= ~(SD_ST_BUSY1 | SD_ST_BUSY0);	// ugly hack :-@
	KEEP_BUSY(0);
//	status_read_counter = 0;
	switch (cmd) {
		case 0x00:	// RESET SD-card
		case 0x10:	// RESET SD-card with flags specified [FIXME: I don't know what the difference is ...]
			sd_status = SD_ST_RESET | (sd_status & SD_ST_EXT_BUS);	// clear all other flags, but not the bus selection, FIXME: bus selection should not be touched?
			memset(sd_regs + 1, 0, 4);	// clear SD-sector 4 byte register. FIXME: what should we zero/reset other than this, still?
			sdhc_mode = 1;
			break;
		case 0x01:	// END RESET
		case 0x11:	// ... [FIXME: again, I don't know what the difference is ...]
			sd_status &= ~(SD_ST_RESET | SD_ST_ERROR | SD_ST_FSM_ERROR);
			break;
		case 0x57:	// write sector gate
			break;	// FIXME: implement this!!!
		case 0x02:	// read block
			sdcard_block_io(U8A_TO_U32(sd_regs + 1), 0);
			break;
		case 0x03:	// write block
			sdcard_block_io(U8A_TO_U32(sd_regs + 1), 1);
			break;
		case 0x53:	// FLASH read! (not an SD-card command!)
			memset(disk_buffers + SD_BUFFER_POS, 0xFF, 0x200);	// Not so much a real read khmm ...
			break;
		case 0x04:	// multi sector write - first sector
			if (sd_last_ok_cmd != 0x04) {
				multi_io_block = U8A_TO_U32(sd_regs + 1);
				sdcard_block_io(multi_io_block, 1);
			} else {
				DEBUGPRINT("SDCARD: bad multi-command sequence command $%02X after command $%02X" NL, cmd, sd_last_ok_cmd);
				sd_status |= SD_ST_ERROR | SD_ST_FSM_ERROR;
			}
			break;
		case 0x05:	// multi sector write - not the first, neither the last sector
			if (sd_last_ok_cmd == 0x04 || sd_last_ok_cmd == 0x05 || sd_last_ok_cmd == 0x57) {
				multi_io_block++;
				sdcard_block_io(multi_io_block, 1);
			} else {
				DEBUGPRINT("SDCARD: bad multi-command sequence command $%02X after command $%02X" NL, cmd, sd_last_ok_cmd);
				sd_status |= SD_ST_ERROR | SD_ST_FSM_ERROR;
			}
			break;
		case 0x06:	// multi sector write - last sector
			if (sd_last_ok_cmd == 0x04 || sd_last_ok_cmd == 0x05 || sd_last_ok_cmd == 0x57) {
				multi_io_block++;
				sdcard_block_io(multi_io_block, 1);
			} else {
				DEBUGPRINT("SDCARD: bad multi-command sequence command $%02X after command $%02X" NL, cmd, sd_last_ok_cmd);
				sd_status |= SD_ST_ERROR | SD_ST_FSM_ERROR;
			}
			break;
		case 0x0C:	// request flush of the SD-card [currently does nothing in Xemu ...]
			break;
		case 0x40:	// SDHC mode OFF - Not supported on newer M65s!
			sd_status &= ~SD_ST_SDHC;
			sdhc_mode = 0;
			break;
		case 0x41:	// SDHC mode ON - Not supported on newer M65s!
			sd_status |= SD_ST_SDHC;
			sdhc_mode = 1;
			break;
		case 0x44:	// sd_clear_error <= '0'	FIXME: what is this?
			break;
		case 0x45:	// sd_clear_error <= '1'	FIXME: what is this?
			break;
		case 0x81:	// map SD-buffer
			sd_status |= SD_ST_MAPPED;
			sd_status &= ~(SD_ST_ERROR | SD_ST_FSM_ERROR);
			break;
		case 0x82:	// unmap SD-buffer
			sd_status &= ~(SD_ST_MAPPED | SD_ST_ERROR | SD_ST_FSM_ERROR);
			break;
		case 0x83:	// SD write fill mode set
			sd_fill_mode = 1;
			break;
		case 0x84:	// SD write fill mode clear
			sd_fill_mode = 0;
			break;
		case 0xC0:	// select internal SD-card bus
			sd_status &= ~SD_ST_EXT_BUS;
			break;
		case 0xC1:	// select external SD-card bus
			sd_status |= SD_ST_EXT_BUS;
			break;
		default:
			sd_status |= SD_ST_ERROR;
			DEBUGPRINT("SDCARD: warning, unimplemented SD-card controller command $%02X" NL, cmd);
			break;
	}
	if (XEMU_UNLIKELY(sd_status & (SD_ST_ERROR | SD_ST_FSM_ERROR))) {
		sd_last_ok_cmd = 0xFF;
	} else {
		sd_last_ok_cmd = cmd;
	}
}


// Here we implement F011 core's callbacks using d81access (and yes, F011 uses 512 bytes long sectors for real)
int fdc_cb_rd_sec ( const int which, Uint8 *buffer, const Uint8 side, const Uint8 track, const Uint8 sector )
{
	const int ret = d81access_read_sect(which, buffer, side, track, sector, 512);
	DEBUG("SDCARD: FDC: reading sector at (side,track,sector)=(%u,%u,%u), return value=%d" NL, side, track, sector, ret);
	return ret;
}


int fdc_cb_wr_sec ( const int which, Uint8 *buffer, const Uint8 side, const Uint8 track, const Uint8 sector )
{
	const int ret = d81access_write_sect(which, buffer, side, track, sector , 512);
	DEBUG("SDCARD: FDC: writing sector at (side,track,sector)=(%u,%u,%u), return value=%d" NL, side, track, sector, ret);
	return ret;
}


// define the callback, d81access call this, we can dispatch the change in FDC config to the F011 core emulation this way, automatically.
// So basically these stuff binds F011 emulation and d81access so the F011 emulation used the d81access framework.
void d81access_cb_chgmode ( const int which, const int mode ) {
	const int have_disk = ((mode & 0xFF) != D81ACCESS_EMPTY);
	const int can_write = !(mode & D81ACCESS_RO) || !have_disk;
	if (which < 2) {
		// Using the d81access layer "mode" info to tell the image size, so we can set the bits 6 or 7 (drive 0/1) in $D68A and $D68B above
		Uint8 acm = MOUNT_ACM_D81;
		if (have_disk) {
			switch (mode & (D81ACCESS_D64 | D81ACCESS_D71 | D81ACCESS_D65)) {
				case D81ACCESS_D64: acm = MOUNT_ACM_D64; break;
				case D81ACCESS_D71: acm = MOUNT_ACM_D71; break;
				case D81ACCESS_D65: acm = MOUNT_ACM_D65; break;
			}
		}
		mount_info[which].acm = acm;
		mount_info[which].read_only = !can_write;
		if (have_disk) {
			const int size = d81access_get_size(which);
			mount_info[which].image_size = size;
			if (mount_info[which].type == MOUNT_TYPE_EXTERNAL)
				mount_info[which].sector = mount_info[which].sector_fake;
			mount_info[which].first_sector = mount_info[which].sector;
			mount_info[which].last_sector = mount_info[which].sector + (Uint32)(size >> 9) - 1U;
			if ((size & 511)) {
				mount_info[which].last_sector++;
				mount_info[which].partial_sector = mount_info[which].last_sector;
			} else {
				mount_info[which].partial_sector = 0;
			}
		} else {
			mount_info[which].image_size = 0;
			mount_info[which].first_sector = 1;	// making first sector larger than last (impossible)
			mount_info[which].last_sector = 0;
			mount_info[which].partial_sector = 0;
		}
		// TODO+FIXME: seriously review the following part ...
		if (!which) {	// drive/image 0 (bits 6 of D68A and D68B)
			if (have_disk) {
				U32_TO_U8A(sd_regs + 0x0C, mount_info[0].sector);
				sd_regs[0xA] = (sd_regs[0xA] & (~0x40)) | ((acm & 1) << 6);
				sd_regs[0xB] = (sd_regs[0xB] & (~0x44)) | ((acm & 2) << 5) | 0x03 | (mount_info[0].read_only ?  4 : 0);
			} else {
				sd_regs[0xB] &= ~0x07;
			}
		} else {	// drive/image 1 (bits 7 of D68A and D68B)
			if (have_disk) {
				U32_TO_U8A(sd_regs + 0x10, mount_info[1].sector);
				sd_regs[0xA] = (sd_regs[0xA] & (~0x80)) | ((acm & 1) << 7);
				sd_regs[0xB] = (sd_regs[0xB] & (~0xA0)) | ((acm & 2) << 6) | 0x18 | (mount_info[1].read_only ? 32 : 0);
			} else {
				sd_regs[0xB] &= ~0x38;
			}
		}
		DEBUGPRINT("SDCARD: configuring F011 FDC (#%d) with have_disk=%d, can_write=%d, image_size=%d, d81_access=\"%s\"(M:%d,A=$%02X,B=$%02X)" NL, which, have_disk, can_write, mount_info[which].image_size, acm_names[acm], acm, sd_regs[0xA], sd_regs[0xB]);
	}
	fdc_set_disk(which, have_disk, can_write);
}


void sdcard_set_external_mount_pool ( Uint32 sector )
{
	if (sd_external_mount_area_start == sector)
		return;
	sd_external_mount_area_start = sector;
	for (Uint32 unit = 0; unit < 2; unit++) {
		mount_info[unit].sector_fake = sector;
		if (mount_info[unit].type == MOUNT_TYPE_EXTERNAL) {
			mount_info[unit].sector = sector;
			U32_TO_U8A(sd_regs + (!unit ? 0x0C : 0x10), sector);
		}
		sector += (Uint32)(D65_SIZE >> 9);
	}
	sd_external_mount_area_end = sector - 1U;
	DEBUGPRINT("SDCARD: external mount pool is #%u...%u (card size: %u)" NL, sd_external_mount_area_start, sd_external_mount_area_end, sdcard_size_in_blocks);
}


static inline int get_mounted ( const int unit )
{
	return !unit ? ((sd_regs[0xB] & 0x03) == 0x03) : ((sd_regs[0xB] & 0x18) == 0x18);
}

static inline int get_ro ( const int unit )
{
	return !!(sd_regs[0xB] & (!unit ? 4 : 32));
}

static inline int get_acm ( const int unit )
{
	return !unit ?
		((sd_regs[0xA] & 0x40) >> 6) + ((sd_regs[0xB] & 0x40) >> 5) :	// (bits 6 of D68A and D68B)
		((sd_regs[0xA] & 0x80) >> 7) + ((sd_regs[0xB] & 0x80) >> 6)	// (bits 7 of D68A and D68B)
	;
}

static inline Uint32 get_sector ( const int unit )
{
	return U8A_TO_U32(sd_regs + (!unit ? 0x0C : 0x10));
}


static int do_external_mount ( const int unit, const char *fn, int read_only )
{
	const int old_type = mount_info[unit].type;
	DEBUGPRINT("SDCARD: MOUNT: external mount #%d from file %s (%s)" NL, unit, fn, read_only ? "R/O" : "R/W");
	if (old_type == MOUNT_TYPE_EXTERNAL && !strcmp(fn, mount_info[unit].desc) && !!mount_info[unit].read_only == !!read_only) {
		DEBUGPRINT("SDCARD: MOUNT: (external mount) already mounted, no change" NL);
		return 0;	// no change, report success though
	}
	mount_info[unit].type = MOUNT_TYPE_EXTERNAL;
	read_only = read_only ? D81ACCESS_RO : 0;
	if (d81access_attach_fsobj(unit, fn, read_only | D81ACCESS_IMG | D81ACCESS_PRG | D81ACCESS_DIR | D81ACCESS_AUTOCLOSE | EXTERNAL_MOUNT_FORMAT_SUPPORT_FLAGS)) {
		mount_info[unit].type = old_type;	// failed, restore previous state!
		return 1;
	}
	xemu_restrdup(&mount_info[unit].desc, fn);
	xemu_restrdup(&mount_info[unit].last_ext_fn, fn);
	return 0;
}


// Meant to check the legitimity of INTERNAL mounts
static inline int is_bad_mount_sector ( const Uint32 sector, const int acm )
{
	const Uint32 till_sector = sector + acm_sectors[acm] - 1U;
	return
		 sector      <  MIN_MOUNT_SECTOR_NO   ||
		till_sector  >= sdcard_size_in_blocks ||
		(sector      >= sd_external_mount_area_start && till_sector <= sd_external_mount_area_end) ||
		(till_sector >= sd_external_mount_area_start && till_sector <= sd_external_mount_area_end) ||
		(sector      <  sd_external_mount_area_start && till_sector >  sd_external_mount_area_end)
	;
}


static int do_internal_mount ( const int unit, const Uint32 sector, const int acm, int read_only )
{
	if (mount_info[unit].type == MOUNT_TYPE_INTERNAL && mount_info[unit].sector == sector && mount_info[unit].acm == acm && !!mount_info[unit].read_only == !!read_only) {
		DEBUGPRINT("SDCARD: MOUNT: (internal mount) already mounted on #%d, no change" NL, unit);
		return 0;	// no change
	}
	if (sector >= sd_external_mount_area_start && sector <= sd_external_mount_area_end) {
		if (sector == mount_info[unit].sector_fake) {
			if (mount_info[unit].last_ext_fn) {
				DEBUGPRINT("SDCARD: MOUNT: (internal mount) fake register mount using previous EXTERNAL mount now on #%d for: %s" NL, unit, mount_info[unit].last_ext_fn);
				if (do_external_mount(unit, mount_info[unit].last_ext_fn, 0))
					sdcard_unmount(unit);
			} else {
				DEBUGPRINT("SDCARD: MOUNT: (internal mount) fake register mount is INVALID on #%d" NL, unit);
				goto invalid_internal;
			}
		} else {
			DEBUGPRINT("SDCARD: MOUNT: (internal mount) external mount reference problem on #%d" NL, unit);
			goto invalid_internal;
		}
	}
	if (is_bad_mount_sector(sector, acm)) {
		DEBUGPRINT("SDCARD: MOUNT: (internal mount) INVALID mount sector (#%u) on #%d, refusing to change config!" NL, sector, unit);
		goto invalid_internal;
	}
	read_only = (sd_is_read_only || read_only) ? D81ACCESS_RO : 0;
	char desc[16];
	snprintf(desc, sizeof desc, "<%s@%u>", acm_names[acm], sector);
	xemu_restrdup(&mount_info[unit].desc, desc);
	mount_info[unit].type = MOUNT_TYPE_INTERNAL;
	mount_info[unit].sector = sector;
	DEBUGPRINT("SDCARD: MOUNT: internal mount #%d from SD sector $%X (%s)" NL, unit, sector, read_only ? "R/O" : "R/W");
	d81access_attach_fd(unit, sdfd, (off_t)sector << 9, D81ACCESS_IMG | read_only | acm_d81consts[acm]);
	return 0;
invalid_internal:
	OSD(-1, -1, "INVALID FDC MOUNT FROM SD%s", mount_info[unit].type == MOUNT_TYPE_EMPTY ? "" : "\n<emergency unmounting>");
	sdcard_unmount(unit);
	return 1;
}


static int is_hyppo_reset = -1;
#define MOUNT_REG_BACKUP_LEN (0x13 - 0x0A + 1)


static void clear_mount_registers ( void )
{
	memset(sd_regs + 0x0A, 0, MOUNT_REG_BACKUP_LEN);
}


static void backup_or_restore_mount_registers ( const int is_backup )
{
	static Uint8 backup[MOUNT_REG_BACKUP_LEN];
	static int has_backup = 0;
	if (is_backup) {
		if (!has_backup) {
			memcpy(backup, sd_regs + 0x0A, sizeof backup);
			has_backup = 1;
		}
	} else {
		if (!has_backup)
			FATAL("%s(): restore without prior backup!", __func__);
		memcpy(sd_regs + 0x0A, backup, sizeof backup);
		has_backup = 0;
	}
}


// These are called from hdos.c as part of the beginning/end of the machine
// initialization (ie, trap reset) _AND_ in case of a DOS trap (Hyppo may
// do mount/umount by request in this case).
void sdcard_notify_hyppo_enter ( const int _is_hyppo_reset )
{
	is_hyppo_reset = _is_hyppo_reset;
	if (is_hyppo_reset) {
		backup_or_restore_mount_registers(1);
		clear_mount_registers();
	}
}


void sdcard_notify_hyppo_leave ( void )
{
	if (is_hyppo_reset < 0)
		FATAL("%s() with is_hyppo_reset < 0", __func__);
	const int was_hyppo_reset = is_hyppo_reset;
	is_hyppo_reset = -1;
	if (was_hyppo_reset == 1) {
		for (int unit = 0; unit < 2; unit++) {
			// See, if HYPPO mounts any default disk image at this point. If so, book as "default disk image" so we can reuse this info later
			Uint32 sector = 0;
			const int acm = get_acm(unit);
			if (get_mounted(unit))
				sector = get_sector(unit);
			if (is_bad_mount_sector(sector, acm))
				sector = 0;
			if (sector) {
				mount_info[unit].sector_initial = sector;
				mount_info[unit].acm_initial = acm;
				DEBUGPRINT("SDCARD: MOUNT: default SD-internal image for unit #%d is at sector #%u (%s)" NL, unit, sector, acm_names[acm]);
			} else {
				mount_info[unit].sector_initial = 0;
				DEBUGPRINT("SDCARD: MOUNT: default SD-internal image for unit #%d could not be determined" NL, unit);
			}
		}
		backup_or_restore_mount_registers(0);
		static int is_first_reset = 1;
		if (is_first_reset) {
			is_first_reset = 0;
			clear_mount_registers();
		        if (configdb.disk8) {
				if (sdcard_external_mount(0, configdb.disk8, "Mount failure on CLI/CFG requested drive-8"))
					xemucfg_set_str(&configdb.disk8, NULL); // In case of error, unset configDB option
			}
			if (!configdb.disk8) {
				if (!configdb.defd81fromsd) {
					// mount default D81 on U8 if no (successfull) mount was done AND no "default disk image from SD-card" option is used
					sdcard_default_external_d81_mount(0);
				} else if (configdb.defd81fromsd && mount_info[0].sector_initial) {
					sdcard_default_internal_d81_mount(0);
				}
			}
			if (configdb.disk9) {
				if (sdcard_external_mount(1, configdb.disk9, "Mount failure on CLI/CFG requested drive-9"))
					xemucfg_set_str(&configdb.disk9, NULL); // In case of error, unset configDB option
			}
		}
		return;
	}
	// Check if there is change in mounting AND it's not the RESET phase, so it must be done by some HYPPO mount/umount call at this point!
	for (int unit = 0; unit < 2; unit++) {
		const int is_mounted = get_mounted(unit);
		const Uint32 sector = get_sector(unit);
		const int acm = get_acm(unit);
		const int ro = get_ro(unit);
		if (mount_info[unit].type != MOUNT_TYPE_EMPTY && !is_mounted) {
			DEBUGPRINT("SDCARD: MOUNT: unmount scenario detected during-HDOS-trap on #%d, doing so" NL, unit);
			sdcard_unmount(unit);
		} else if (is_mounted && (
			mount_info[unit].type == MOUNT_TYPE_EMPTY ||
			mount_info[unit].sector != sector ||
			mount_info[unit].acm != acm ||
			!mount_info[unit].read_only != !ro
		)) {
			if (sector == mount_info[unit].sector_initial && !configdb.defd81fromsd) {
				if (sdcard_default_external_d81_mount(unit))
					sdcard_unmount(unit);
			} else {
				if (do_internal_mount(unit, sector, acm, ro))
					sdcard_unmount(unit);
			}
		}
	}
}


int sdcard_default_external_d81_mount ( const int unit )
{
	static char *default_d81_path[2] = { NULL, NULL };
	if (!default_d81_path[unit]) {
		// Prepare to determine the full path of the default external d81 image, if we haven't got it yet
		const char *hdosroot;
		(void)hypervisor_hdos_virtualization_status(-1, &hdosroot);
		const int len = strlen(hdosroot) + strlen(default_d81_basename[unit]) + 1;
		default_d81_path[unit] = xemu_malloc(len);
		snprintf(default_d81_path[unit], len, "%s%s", hdosroot, default_d81_basename[unit]);
	}
	DEBUGPRINT("SDCARD: MOUNT: trying to mount DEFAULT external image instead of internal default one as %s on unit #%d" NL, default_d81_path[unit], unit);
	// we want to create the default image file if does not exist (note the "0" for d81access_create_image_file, ie do not overwrite exisiting image)
	// d81access_create_image_file() returns with 0 if image was created, -2 if image existed before, and -1 for other errors
	if (d81access_create_image_file(default_d81_path[unit], default_d81_disk_label[unit], 0, NULL) == -1)
		return -1;
	// ... so, the file existed before case allows to reach this point, since then retval is -2 (and not -1 as a generic error)
	return sdcard_external_mount(unit, default_d81_path[unit], "Cannot mount default external D81");
}


int sdcard_default_internal_d81_mount ( const int unit )
{
	if (!mount_info[unit].sector_initial) {
		ERROR_WINDOW("No default disk image on SD-card for unit #%d", unit);
		return 1;
	}
	DEBUGPRINT("SDCARD: MOUNT: mounting DEFAULT on-SD card (at sector %u) default image on unit #%d" NL, mount_info[unit].sector_initial, unit);
	return do_internal_mount(unit, mount_info[unit].sector_initial, mount_info[unit].acm_initial, 0);
}


const char *sdcard_get_mount_info ( const int unit, int *is_internal )
{
	if (is_internal)
		*is_internal = (mount_info[unit & 1].type == MOUNT_TYPE_INTERNAL);
	static const char *str_empty = "<EMPTY>";
	return mount_info[unit & 1].desc ? mount_info[unit & 1].desc : str_empty;
}


int sdcard_external_mount ( const int unit, const char *filename, const char *cry )
{
	DEBUGPRINT("SDCARD: MOUNT: %s(%d, \"%s\", \"%s\");" NL, __func__, unit, filename, cry);
	if (!filename || !*filename) {
		ERROR_WINDOW("Calling %s() with NULL or empty str?!", __func__);	// FIXME: remove this!
		return -1;
	}
	if (do_external_mount(unit, filename, 0)) {
		if (cry) {
			ERROR_WINDOW("%s\nCould not mount requested file as unit #%d:\n%s", cry, unit, filename);
		}
		return -1;
	}
	return 0;
}


int sdcard_external_mount_with_image_creation ( const int unit, const char *filename, const int do_overwrite, const char *cry )
{
	if (d81access_create_image_file(filename, NULL, do_overwrite, "Cannot create D81"))
		return -1;
	return sdcard_external_mount(unit, filename, cry);
}



void sdcard_unmount ( const int unit )
{
	if (mount_info[unit].type == MOUNT_TYPE_EMPTY)
		return;
	DEBUGPRINT("SDCARD: MOUNT: unmounting #%d" NL, unit);
	mount_info[unit].type = MOUNT_TYPE_EMPTY;
	free(mount_info[unit].desc);
	mount_info[unit].desc = NULL;
	d81access_close(unit);
}


void sdcard_write_register ( const int reg, const Uint8 data )
{
	const Uint8 prev_data = sd_regs[reg];
	if (!in_hypervisor && reg >= 0x0A && reg <= 0x13) {
		// TODO/FIXME: this is probably wrong if MEGA65 allows FDC-mount operations outside of hypervisor as well
		// However this is much safer/more simple this way as we can use the fact of hypervisor leave gate to "evaluate" the resulf of
		// register changes than allowing any program to do random modifications here and there (also I don't know any MEGA65 titles
		// which would do direct mount reg modifications anyway). If it's a serious limitation of emulation, I should rewrote these things.
		// Must be placed here, since if written from the "user-space" then we must avoid "sd_regs" to be updated.
		// --- HOWEVER ---
		// ROM seems to manipulate register 0xB directly for umount (not via hyppo!)? Let's see if we can handle that here ourself.
		// Honestly, there should be a FIXME/TODO here to also handle _any_ kind of direct FDC mount reg operation as well.
		if (reg == 0x0B) {
			if (mount_info[0].type != MOUNT_TYPE_EMPTY && (data & 0x03) != 0x03) {
				DEBUGPRINT("SDCARD: user-space unmount on unit #0" NL);
				sdcard_unmount(0);
			}
			if (mount_info[1].type != MOUNT_TYPE_EMPTY && (data & 0x18) != 0x18) {
				DEBUGPRINT("SDCARD: user-space unmount on unit #1" NL);
				sdcard_unmount(1);
			}
			return;
		}
		DEBUGPRINT("SDCARD: *REFUSING* to modify FDC-mount registers ($%02X of $0B-$13, data: $%02X, prev_value: $%02X) while not in hypervisor mode @ PC=$%04X" NL, reg, data, prev_data, cpu65.pc);
		return;
	}
	sd_regs[reg] = data;
	// Note: don't update D6XX backend as it's already done in the I/O decoder
	switch (reg) {
		case 0x00:		// command/status register
			sdcard_command(data);
			break;
		case 0x01:		// sector address
		case 0x02:		// sector address
		case 0x03:		// sector address
		case 0x04:		// sector address
			DEBUG("SDCARD: writing sector number register $%04X with $%02X PC=$%04X" NL, reg + 0xD680, data, cpu65.pc);
			break;
		case 0x06:		// write-only register: byte value for SD-fill mode on SD write command
			sd_fill_value = data;
			if (sd_fill_value != sd_fill_buffer[0])
				memset(sd_fill_buffer, sd_fill_value, 512);
			break;
		case 0x09:
			set_disk_buffer_cpu_view();	// update disk_buffer_cpu_view pointer according to sd_regs[9] just written
			break;
		case 0x0A:	// mount info for upper two bits (other bits seem to be R/O!)
		case 0x0B:	// mount register
		case 0x0C:	// first D81 disk image starting sector registers
		case 0x0D:
		case 0x0E:
		case 0x0F:
		case 0x10:	// second D81 disk image starting sector registers
		case 0x11:
		case 0x12:
		case 0x13:
			break;	// do nothing! for now, mounting is done via hyppo trap/leave callbacks. also, read the comment near of the current function
		default:
			DEBUGPRINT("SDCARD: unimplemented register: $%02X tried to be written with data $%02X" NL, reg, data);
			break;
	}
}


int sdcard_is_writeable ( void )
{
	return !sd_is_read_only;
}


Uint8 sdcard_read_register ( const int reg )
{
	Uint8 data = sd_regs[reg];	// default answer
	switch (reg) {
		case 0:
			return sdcard_read_status();
		case 1:
		case 2:
		case 3:
		case 4:
			break;	// allow to read back SD sector address
		case 6:
			break;
		case 8:
			return fdc_get_buffer_disk_address() & 0xFF;
		case 9:
			return
				(fdc_get_buffer_disk_address() >> 8) |	// $D689.0 - High bit of F011 buffer pointer (disk side) (read only)
				((fdc_get_status_a(-1) & (64 + 32)) == (64 + 32) ? 2 : 0) |	// $D689.1 - Sector read from SD/F011/FDC, but not yet read by CPU (i.e., EQ and DRQ)
				(data & 0x80);				// $D689.7 - Memory mapped sector buffer select: 1=SD-Card, 0=F011/FDC
			break;
		case 0xA:
			// FIXME: ethernet I/O mode should be a disctict I/O mode??
			// bits 2 and 3 is always zero in Xemu (no drive virtualization for drive 0 and 1)
			return
				(vic_registers[0x30] & 1) |		// $D68A.0 SD:CDC00 (read only) Set if colour RAM at $DC00
				(vic_iomode & 2)  |			// $D68A.1 SD:VICIII (read only) Set if VIC-IV or ethernet IO bank visible [same bit pos as in vic_iomode for mode-4 and mode-ETH!]
				(data & (128 + 64));			// size info for disk mounting
			break;
		case 0xB:
			break;
		case 0xC:
		case 0xD:
		case 0xE:
		case 0xF:
			break;
		case 0x10:
		case 0x11:
		case 0x12:
		case 0x13:
			break;
		default:
			DEBUGPRINT("SDCARD: unimplemented register: $%02X tried to be read, defaulting to the back storage with data $%02X" NL, reg, data);
			break;
	}
	return data;
}


/* --- SNAPSHOT RELATED --- */


#ifdef XEMU_SNAPSHOT_SUPPORT

#include <string.h>

#define SNAPSHOT_SDCARD_BLOCK_VERSION	0
#define SNAPSHOT_SDCARD_BLOCK_SIZE	(0x100 + sizeof(disk_buffers))

int sdcard_snapshot_load_state ( const struct xemu_snapshot_definition_st *def, struct xemu_snapshot_block_st *block )
{
	Uint8 buffer[SNAPSHOT_SDCARD_BLOCK_SIZE];
	int a;
	if (block->block_version != SNAPSHOT_SDCARD_BLOCK_VERSION || block->sub_counter || block->sub_size != sizeof buffer)
		RETURN_XSNAPERR_USER("Bad SD-Card block syntax");
	a = xemusnap_read_file(buffer, sizeof buffer);
	if (a) return a;
	/* loading state ... */
	memcpy(sd_sector_registers, buffer, 4);
	memcpy(sd_d81_img1_start, buffer + 4, 4);
	fd_mounted = (int)P_AS_BE32(buffer + 8);
	sd_is_read_only = (int)P_AS_BE32(buffer + 16);
	//d81_is_read_only = (int)P_AS_BE32(buffer + 20);
	//use_d81 = (int)P_AS_BE32(buffer + 24);
	sd_status = buffer[0xFF];
	memcpy(disk_buffers, buffer + 0x100, sizeof disk_buffers);
	return 0;
}


int sdcard_snapshot_save_state ( const struct xemu_snapshot_definition_st *def )
{
	Uint8 buffer[SNAPSHOT_SDCARD_BLOCK_SIZE];
	int a = xemusnap_write_block_header(def->idstr, SNAPSHOT_SDCARD_BLOCK_VERSION);
	if (a) return a;
	memset(buffer, 0xFF, sizeof buffer);
	/* saving state ... */
	memcpy(buffer, sd_sector_registers, 4);
	memcpy(buffer + 4,sd_d81_img1_start, 4);
	U32_AS_BE(buffer + 8, fd_mounted);
	U32_AS_BE(buffer + 16, sd_is_read_only);
	//U32_AS_BE(buffer + 20, d81_is_read_only);
	//U32_AS_BE(buffer + 24, use_d81);
	buffer[0xFF] = sd_status;
	memcpy(buffer + 0x100, disk_buffers, sizeof disk_buffers);
	return xemusnap_write_sub_block(buffer, sizeof buffer);
}

#endif
