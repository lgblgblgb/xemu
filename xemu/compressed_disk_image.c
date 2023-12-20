/* Part of the Xemu project, please visit: https://github.com/lgblgblgb/xemu
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


// Disk images can be compressed with the build/compress_sd_image.py script.
// Note, only images can be compressed and used this way, which are prepared
// to use this API! Compressed disk images are read-only!


#ifdef RLE_COMPRESSED_DISK_IMAGE_SUPPORT

#include "xemu/emutools.h"
#include "xemu/compressed_disk_image.h"
#include "xemu/emutools_files.h"
#include <sys/types.h>
#include <unistd.h>
#include <errno.h>


static Uint8 *unpack_buffer = NULL;
static int unpack_buffer_allocated = 0;
static const char default_image_debug_name[] = "COMPRESSED-DISK-IMAGE";
static const char compressed_marker[] = "XemuBlockCompressedImage001";	// Do not change this, this is used to identify the compressed format!


void compressed_diskimage_free ( struct compressed_diskimage_st *info )
{
	if (!info)
		return;
	free(info->pagedir);
	info->pagedir = NULL;
	free(info->page_cache);
	info->page_cache = NULL;
	free(info->name);
	info->name = NULL;
	info->cached_page = UINT_MAX;
	info->size_in_blocks = 0;
	info->fd = -1;
}


int compressed_diskimage_detect ( struct compressed_diskimage_st *info, const int fd, const char *name )
{
	if (!name)
		name = default_image_debug_name;
	info->pagedir = NULL;
	info->page_cache = NULL;
	info->cached_page = UINT_MAX;
	info->fd = -1;
	info->name = NULL;
	Uint8 hdr[sizeof(compressed_marker) + 64];
	if (lseek(fd, 0, SEEK_SET) != (off_t)0 || xemu_safe_read(fd, hdr, sizeof(hdr)) != sizeof(hdr))
		return -1;
	// Check for compressed signature
	if (memcmp(hdr, compressed_marker, sizeof compressed_marker))
		return 0;
	// Process header info
	const unsigned int pages =		xemu_u8p_to_u32le(hdr + sizeof(compressed_marker)     );
	const unsigned int pagedir_base =	xemu_u8p_to_u32le(hdr + sizeof(compressed_marker) +  4);
	info->unpack_buffer_size =		xemu_u8p_to_u32le(hdr + sizeof(compressed_marker) +  8);
	const unsigned int pagedir_length =	xemu_u8p_to_u32le(hdr + sizeof(compressed_marker) + 12);
	unsigned int data_offset =		xemu_u8p_to_u32le(hdr + sizeof(compressed_marker) + 16);
	info->size_in_blocks = pages << 7;
	if (!pages || !pagedir_length || pagedir_length % 3 || !info->unpack_buffer_size)
		return -1;
	if (pages > 0x10000) {
		DEBUGPRINT("%s: too large image, more than 64K x 64K pages (%u of them)" NL, name, pages);
		return -1;
	}
	// Process the page offsets
	if (lseek(fd, pagedir_base, SEEK_SET) != (off_t)pagedir_base)
		return -1;
	Uint8 *buf = xemu_malloc(pagedir_length + 1);
	if (xemu_safe_read(fd, buf, pagedir_length + 1) != pagedir_length) {	// we try to read one byte MORE to check if there are no unknown extra bytes at the end
		free(buf);
		return -1;
	}
	info->pagedir = xemu_malloc(sizeof(Uint32) * (pages + 1));
	for (unsigned int i = 0, o = 0; i != pagedir_length || o != pages; i += 3) {
		if (i + 2 >= pagedir_length)
			goto unpack_error;
		const unsigned int n = buf[i] + (buf[i + 1] << 8) + (buf[i + 2] << 16);
		if (!(n & 0x800000U)) {
			if (o >= pages)
				goto unpack_error;
			info->pagedir[o++] = data_offset;
			data_offset += n;
		} else {
			for (unsigned int j = 0; j < (n & 0xFFFF); j++) {
				if (o >= pages)
					goto unpack_error;
				info->pagedir[o++] = data_offset;
				data_offset += (n >> 16) & 0x7F;
			}
		}
	}
	free(buf);
	info->pagedir[pages]  = data_offset;	// we need pages+1 elements in the array, see read_compressed_block() later
	info->page_cache = xemu_malloc(0x10000);
	info->fd = fd;
	info->name = xemu_strdup(name);
	DEBUGPRINT("%s: compressed image with %u 64K-pages, max packed page size is %u bytes, compressed page directory is %u entries long" NL, name, pages, info->unpack_buffer_size, pagedir_length / 3);
	if (unpack_buffer_allocated < info->unpack_buffer_size) {
		DEBUGPRINT("%s: resizing unpack buffer from %d to %d bytes" NL, name, unpack_buffer_allocated, info->unpack_buffer_size);
		unpack_buffer = xemu_realloc(unpack_buffer, info->unpack_buffer_size);
		unpack_buffer_allocated = info->unpack_buffer_size;
	}
	return 1;
unpack_error:
	free(buf);
	compressed_diskimage_free(info);
	ERROR_WINDOW("%s: **ERROR** compressed disk image page directory unpacking error" NL, name);
	return -1;
}


int compressed_diskimage_read_block ( struct compressed_diskimage_st *info, const Uint32 block, Uint8 *buffer )
{
	if (XEMU_UNLIKELY(!info || !info->pagedir || !info->page_cache || info->fd < 0))
		FATAL("%s(): invalid \"info\" structure", __func__);
	if (XEMU_UNLIKELY(block >= info->size_in_blocks))
		return -1;
	const unsigned int page_no = block >> 7;	// "block" is 512 byte based, so to get 64K based page, we need 7 more shifts
	if (info->cached_page != page_no) {
		const unsigned int img_ofs = info->pagedir[page_no];
		const unsigned int pck_siz = info->pagedir[page_no + 1] - img_ofs;
		if (XEMU_UNLIKELY(pck_siz > info->unpack_buffer_size))
			FATAL("%s: Compressed disk image unpack fatal error: too large unpack request", info->name);
		if (!pck_siz) {
			memset(info->page_cache, 0, 0x10000);
			//DEBUGPRINT("%s: CACHE: miss-zeroed" NL, info->name);
		} else {
			//DEBUGPRINT("%s: CACHE: miss" NL, info->name);
			if (lseek(info->fd, img_ofs, SEEK_SET) != (off_t)img_ofs)
				goto seek_error;
			if (xemu_safe_read(info->fd, unpack_buffer, pck_siz) != pck_siz)
				goto read_error;
			memset(info->page_cache, unpack_buffer[0], 0x10000);
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
						memset(info->page_cache + o, v, n);
					o += n;
				} else
					info->page_cache[o++] = c;
			}
		}
		info->cached_page = page_no;
	} else {
		//DEBUGPRINT("%s: CACHE: hit!" NL, info->name);
	}
	memcpy(buffer, info->page_cache + ((block & 127) << 9), 512);
	return 0;
seek_error:
	FATAL("%s: SEEK: compressed image seek host-OS failure: %s", info->name, strerror(errno));
	return -1;
read_error:
	FATAL("%s: READ: compressed image read host-OS failure: %s", info->name, strerror(errno));
	return -1;
}

#endif
