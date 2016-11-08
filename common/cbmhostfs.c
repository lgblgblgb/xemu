/* Test-case for a very simple, inaccurate, work-in-progress Commodore 65 / Mega-65 emulator,
   within the Xemu project.
   Copyright (C)2016 LGB (Gábor Lénárt) <lgblgblgb@gmail.com>

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


#include "emutools.h"
#include "cbmhostfs.h"

#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <time.h>
#include <dirent.h>
#include <fcntl.h>
#include <errno.h>


#define DEBUG_HOSTFS	printf
//#define DEBUG_HOSTFS	DEBUG


/* 

Future plans to have some KERNAL-DOS-access-alike functions to make easy to patch ROM
to use HostFS instead of DOS/IEC/whatever routines for a given device number.

http://www.pagetable.com/docs/Inside%20Commodore%20DOS.pdf
http://www.zimmers.net/anonftp/pub/cbm/programming/serial-bus.pdf


Current quick'n'dirty usage:

	C65 I/O mode, $D0FE = reg0, $D0FF = reg1


	reg0: read access: status register (low level, emulator specific error singaling)
              write access: command register (low level, emulator specific command selection)
        reg1: read/write data (depending on the last command used, may be invalid to be used, see command descriptions!)

	Status register:
		zero byte = no error, and no EOF
		bit6 = EOF condition, NOT only for the next I/O, but the previous! That is, the last
		       read didn't fetched anything for real, so the read byte should not be used, as it's
		       some dummy byte, and not a real fetched data! This bit is also set, if data port
		       read/written after a command/etc which does not allow that, since there is no buffer to use (End of Buffer/ No buffer)
		       also, non-existing command generates this
                       basically this bit means that nothing to do with the written/read data or command code, because non-available data, storage or command
		bit7 = error occured (other than EOF), it means actual hostFS task could be done but resulted error
		!!STATUS REGISTER IS RESET AFTER A READ!!

		Should be checked after each R/W of data register.

	Command register (low-level protocol commands only):
		High nibble of the written byte specifies the command (low nibble may specify channel, but see descriptions):
			0 = Start to send specification for open
				low nibble has no purpose. Data write register must be used to transfer bytes
				for the specification, surely data register can be written multiple times to
				transfer a strin g. After last write of data register, command 1 must be used.
				status register *MAY* have indicate "EOF" if too long string is given, there can
				be no other kind of error.
			1 = terminates and "finalizes" the string sent after issuing command 0, open / execute!
				the stored string remains intact till the next received command 1.
				Low nibble MUST specifies a "secondary channel" the specification is refeered to
				that is (like with CBM DOS stuffs):
				0 = prg reading
				1 = prg writing
				2 - 14 = custom open files
				15 = command/status channel
				Status register may inidicate error on command interpreting the specification.
				(but not "EOF"). In case of an error, command channel can be open/read to
				describe the error. If EOF is given during write data register, you will not able to
				terminate your string, since there is not place in buffer.
				It opens the given channel! If the channel was already open, it will be closed first.
			2 = repeat cmd 1
				It's like cmd-1, however there is no need for cmd0 + specification send,
				the same spec is used again (with possible close the channel if it was open before).
				The low nibble must specify a channel, and can be different as with cmd1 (however this is
				the situation of "do this only if you really know what you are doing")
				Otherwise the same rules.
			3 = close channel
				low nibble must specify an already open channel.
				Command closes the given channel
			4 = use channel
				low nibble must specify an already open channel
				sets the "target/source" of data port. NOTE: depending on the channel, it's possible
				that either of read or write of data register is not supported.
				NOTE: WITHOUT THIS COMMAND, YOU CAN'T WRITE OR READ DATA REGISTER (for other purpose
				that after cmd-0 for writing, which is valid, of course, but even then read is not supported)
				You can have more open channel, and use cmd-4 to "switch" between them.
			Other not implemented command codes result in EOF in status register.

	Currently the implementation is rather primitive. the "specification string" cannot specify commands, just file names.
	Though, if first character of file name is "$" (the others are not checked!) it means directory reading, and "standard"
	pseudo-BASIC listing can be read (as with CBM DOS). Otherwise, the string interpreted as file name to open, in the
	following syntax (ONLY!!!!):

	@FILENAME,P,R

	@: this can be omitted, and have only meaning if write access is requested, then it will overwrite possible existing file
	P: this is the file type, like S(SEQ), P(PRG), etc, but currently NOT CHECKED AT ALL
	R: this can be "R" or "W" for read or write
           for "R" it can be omitted (also the whole ,P,R like part)

	All directory entries are handled/shown as "PRG" files. File names are "normalized" upon directory listing and also
	on opening files, as there is fundamental problem with PETSCII-ASCII conversion and also the differences with hostFS
	(on Windows, FS is case insensitive, where with UNIX it's case sensitive). Non-conforming file names (too long, not
	allowed CBM DOS chars, cannot be converted to PETSCII, etc) may not appear in directory listings. If multiple files
	have the same name (ie, case sensitive names like "readme" and "README" in hostFS, normalized to the same name)
	will be shown, however you may have interesting problem if you try to access one (ie "wrong" file is used or even
	overwritten, etc).

	Channel number a freely choosable channel, so you can use multiple open files. Channel numbers are 0-15, though 15 cannot
	be used (in the future it will be the CBM DOS status/command channel), also 0,1 can have special meaning (load/save
	PRG file) in the future. The future usage of this channel number will be the same as the 3rd parameter of BASIC OPEN statement, also
	called "SECONDARY CHANNEL", "SECONDARY ADDRESS" or "SA".

*/


#define WRITE_BUFFER_SIZE	256
#define READ_BUFFER_SIZE	256
#define SPEC_BUFFER_SIZE	4096


struct hostfs_channels_st {
	Uint8 read_buffer[READ_BUFFER_SIZE];
	Uint8 write_buffer[WRITE_BUFFER_SIZE];
	int  read_used;
	int  write_used;
	DIR *dir;	// if it's non-NULL, it has precedence over fd
	int  fd;
	int allow_write, allow_read,eof;
};

static struct hostfs_channels_st hostfs_channels[16];
static Uint8 hfs_status;
static Uint8 spec_filling[SPEC_BUFFER_SIZE];
static Uint8 spec_used[SPEC_BUFFER_SIZE];
static int spec_filling_index;
static int last_command;
static struct hostfs_channels_st *use_channel;
static char hostfs_directory[PATH_MAX];



void hostfs_init ( const char *basedir, const char *subdir )
{
	int a;
	sprintf(hostfs_directory, "%s" DIRSEP_STR "%s" DIRSEP_STR, basedir, subdir);
	MKDIR(hostfs_directory);
	for (a = 0; a < 16; a++) {
		hostfs_channels[a].dir = NULL;
		hostfs_channels[a].fd = -1;
		hostfs_channels[a].allow_write = 0;
		hostfs_channels[a].allow_read = 0;
	}
	hfs_status = 0;
	last_command = -1;
	use_channel = NULL;
	printf("HOSTFS: init @ %s" NL, hostfs_directory);
}


static void hostfs_flush ( struct hostfs_channels_st *channel )
{
	int pos;
	if (channel->fd < 0)
		return;
	pos = 0;
	while (channel->write_used) {
		int ret = write(channel->fd, channel->write_buffer + pos, channel->write_used);
		switch (ret) {
			case -1:
				FATAL("HOSTFS: cannot write file, error got: %s", strerror(errno));
				break;
			case  0:
				FATAL("HOSTFS: cannot write file, zero bytes could written!");
				break;
			default:
				channel->write_used -= ret;
				pos += ret;
				break;
		}
	}
}


static void hostfs_close ( struct hostfs_channels_st *channel )
{
	if (channel->dir)
		closedir(channel->dir);
	if (channel->fd >= 0) {
		hostfs_flush(channel);
		close(channel->fd);
	}
	channel->dir = NULL;
	channel->fd = -1;
	channel->allow_write = 0;
	channel->allow_read = 0;
	hfs_status = 0;
}


void hostfs_close_all ( void )
{
	int channel;
	for (channel = 0; channel < 16; channel++)
		hostfs_close(hostfs_channels + channel);
}


void hostfs_flush_all ( void )
{
	int channel;
	for (channel = 0; channel < 16; channel++)
		hostfs_flush(hostfs_channels + channel);
}


static void hostfs_open ( struct hostfs_channels_st *channel )
{
	hostfs_close(channel);	// close channel if it was used before ...
	if (spec_used[0] == 0) {
		hfs_status = 64;
		return;
	}
	channel->write_used = 0;
	channel->read_used = 0;
	channel->eof = 0;
	if (spec_used[0] == '$') {
		channel->dir = opendir(hostfs_directory);
		if (!channel->dir) {
			hfs_status = 64;
			return;
		}
		// TODO: prepare directory, first item ...
		hfs_status = 0;
		channel->allow_write = 0;
		channel->allow_read  = 1;
		FATAL("Directory reading is not implemented yet :(");
	} else {
		/* normal file must be open ... */
		int flags, len;
		Uint8 *p0,*p1,*p2;
		DIR *dir;
		struct dirent *entry;
		char filename[20];
		p1 = (Uint8*)strchr((char*)spec_used, ',');
		if (p1) {
			p2 = (Uint8*)strchr((char*)p1 + 1, ',');
			len = (int)(p1 - spec_used + 1);
		} else {
			p2 = NULL;
			len = strlen((char*)spec_used);
		}
		if (len > 16) {
			hfs_status = 64;
			return;
		}
		if (p2 && (p2[1] == 'w' || p2[1] == 'W')) {
			channel->allow_write = 1;
			channel->allow_read  = 0;
			if (spec_used[0] == '@') {
				flags = O_WRONLY | O_CREAT | O_TRUNC | O_BINARY;
				p0 = spec_used + 1;
				len--;
			} else {
				flags = O_WRONLY | O_CREAT | O_EXCL | O_BINARY;
				p0 = spec_used;
			}
		} else {
			flags = O_RDONLY | O_BINARY;
			channel->allow_write = 0;
			channel->allow_read  = 1;
			if (spec_used[0] == '@') {
				p0 = spec_used + 1;
				len--;
			} else
				p0 = spec_used;
		}
		memcpy(filename, p0, len);
		filename[len] = 0;
		dir = opendir(hostfs_directory);
		if (!dir) {
			hfs_status = 128;
			return;
		}
		DEBUG_HOSTFS("HOSTFS: trying to open file %s" NL, filename);
		while ((entry = readdir(dir))) {
			DEBUG_HOSTFS("HOSTFS: considering file %s" NL, entry->d_name);
			if (!strcasecmp(entry->d_name, filename)) {
				DEBUG_HOSTFS("HOSTFS: file name accepted." NL);
				closedir(dir);
				channel->fd = open(entry->d_name, flags, 0666);
				if (channel->fd < 0)
					hfs_status = 128;
				else
					hfs_status = 0;
				return;
			}
		}
		closedir(dir);
		hfs_status = 128;
	}
}




Uint8 hostfs_read_reg0 ( void )
{
	Uint8 ret = hfs_status;
	hfs_status = 0;
	return ret;
}


void hostfs_write_reg0 ( Uint8 data )
{
	struct hostfs_channels_st *channel = hostfs_channels + (data & 15);
	last_command = data >> 4;
	hfs_status = 0;
	switch (last_command) {
		case 0:
			spec_filling_index = 0;
			break;
		case 1:
			if (spec_filling_index >= SPEC_BUFFER_SIZE) {
				hfs_status = 64;
			} else {
				spec_filling[spec_filling_index] = 0;
				strcpy((char*)spec_used, (char*)spec_filling);
				spec_filling_index = 0;
				hostfs_open(channel);
			}
			break;
		case 2:
			hostfs_open(channel);
			break;
		case 3:	// close channel
			hostfs_close(channel);
			break;
		case 4:
			use_channel = channel;
			break;
		default:
			hfs_status = 64;
			break;
	}
}


Uint8 hostfs_read_reg1 ( void )
{
	Uint8 result;
	if (last_command == 0) {
		hfs_status = 64;
		return 0xFF;
	}
	if (!use_channel) {
		hfs_status = 64;
		return 0xFF;
	}
	if (!use_channel->allow_read) {
		hfs_status = 64;
		return 0xFF;
	}
	if (use_channel->eof) {
		hfs_status = 64;
		return 0xFF;
	}
	if (use_channel->read_used)
		goto ret_one;
	/* empty buffer, must read more bytes ... */
	if (use_channel->dir) {
		FATAL("Directory reading is not yet supported :-(");
		return 0xFF;
	}
	use_channel->read_used = read(use_channel->fd, use_channel->read_buffer, READ_BUFFER_SIZE);
	if (use_channel->read_used == 0) {
		hfs_status = 64;
		use_channel->eof = 1;
		return 0xFF;	// dummy byte!
	}
	if (use_channel->read_used < 0) {
		FATAL("read error @ hostFS!");
		return 0xFF;
	}
ret_one:
	result = use_channel->read_buffer[0];
	memmove(use_channel->read_buffer, use_channel->read_buffer + 1, --use_channel->read_used);
	hfs_status = 0;
	return result;
}


void hostfs_write_reg1 ( Uint8 data )
{
	if (last_command == 0) {
		if (spec_filling_index >= SPEC_BUFFER_SIZE - 1) {
			hfs_status = 64;
		} else {
			spec_filling[spec_filling_index++] = data;
			hfs_status = 0;
		}
		return;
	}
	if (!use_channel) {
		hfs_status = 64;
		return;
	}
	if (!use_channel->allow_write) {
		hfs_status = 64;
		return;
	}
	if (use_channel->write_used == WRITE_BUFFER_SIZE)
		hostfs_flush(use_channel);
	use_channel->write_buffer[use_channel->write_used++] = data;
	hfs_status = 0;
}
