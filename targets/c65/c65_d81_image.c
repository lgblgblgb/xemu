/* Test-case for a very simple, inaccurate, work-in-progress Commodore 65 / Mega-65 emulator,
   within the Xemu project.
   Copyright (C)2016,2017 LGB (Gábor Lénárt) <lgblgblgb@gmail.com>

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
#include "xemu/f011_core.h"
#include "c65_d81_image.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>


static int disk_fd = -1, disk_inserted = 0, read_only = 1;


int fdc_cb_rd_sec ( Uint8 *buffer, int offset )
{
	int ret;
	if (offset < 0 || offset > D81_SIZE - 512 || (offset & 511))
		FATAL("D81: invalid read offset: %d", offset);
	if (disk_fd < 0)
		FATAL("D81: not open");
	if (!disk_inserted)
		FATAL("D81: not inserted");
	if (lseek(disk_fd, offset, SEEK_SET) != offset)
		FATAL("D81: host-OS seek failure to %d: %s", offset, strerror(errno));
	ret = xemu_safe_read(disk_fd, buffer, 512);
	if (ret < 0)
		FATAL("D81: host-OS read failure: %s", strerror(errno));
	if (ret != 512)
		FATAL("D81: host-OS didn't read 512 bytes but %d", ret);
	return 0;
}




int fdc_cb_wr_sec ( Uint8 *buffer, int offset )
{
	int ret;
	if (offset < 0 || offset > D81_SIZE - 512 || (offset & 511))
		FATAL("D81: invalid read offset: %d", offset);
	if (disk_fd < 0)
		FATAL("D81: not open");
	if (!disk_inserted)
		FATAL("D81: not inserted");
	if (read_only)
		FATAL("D81: read-only disk");
	if (lseek(disk_fd, offset, SEEK_SET) != offset)
		FATAL("D81: host-OS seek failure to %d: %s", offset, strerror(errno));
	ret = xemu_safe_write(disk_fd, buffer, 512);
	if (ret < 0)
		FATAL("D81: host-OS write failure: %s", strerror(errno));
	if (ret != 512)
		FATAL("D81: host-OS didn't write 512 bytes but %d", ret);
	return 0;
}



static void c65_d81_shutdown ( void )
{
	if (disk_fd >= 0) {
		printf("D81: disk image has been closed." NL);
		close(disk_fd);
	}
	disk_fd = -1;
}



void c65_d81_init ( const char *dfn )
{
	atexit(c65_d81_shutdown);
	fdc_init();
	disk_inserted = 0;
	read_only = 1;
	if (disk_fd >= 0)
		close(disk_fd);
	disk_fd = -1;
	if (dfn) {
		read_only = O_RDONLY;
		disk_fd = xemu_open_file(dfn, O_RDWR, &read_only, NULL);
		if (disk_fd < 0) {
			ERROR_WINDOW("Couldn't open disk image \"%s\": %s", dfn, strerror(errno));
		} else {
			disk_inserted = 1;
			if (read_only)
				WARNING_WINDOW("Disk image \"%s\" could be open only in read-only mode", dfn);
			else
				DEBUGPRINT("DISK: disk image \"%s\" opened in read-write mode" NL, dfn);
		}
	}
	if (disk_inserted) {
		off_t size = lseek(disk_fd, 0, SEEK_END);
		if (size < 0)
			FATAL("D81: host-OS error while testing image size: %s", strerror(errno));
		if (size != D81_SIZE) {
			ERROR_WINDOW("Image size is wrong, got %d, should be %d", (int)size, D81_SIZE);
			disk_inserted = 0;
		}
	}
	if (!disk_inserted) {
		read_only = 1;
		if (disk_fd >= 0) {
			close(disk_fd);
			disk_fd = -1;
		}
		ERROR_WINDOW("No valid D81 found, or given (in command line). There will be no available disk emulated.");
	}
	fdc_set_disk(disk_inserted, !read_only);
}
