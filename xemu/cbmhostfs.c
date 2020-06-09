/* Test-case for a very simple, inaccurate, work-in-progress Commodore 65 / MEGA65 emulator,
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


#include "xemu/emutools.h"
#include "xemu/cbmhostfs.h"

#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <time.h>
#include <dirent.h>
#include <fcntl.h>
#include <errno.h>
#include <stdlib.h>


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
#define CBM_MAX_DIR_ENTRIES	144


struct hostfs_channels_st {
	Uint8 read_buffer[READ_BUFFER_SIZE];
	Uint8 write_buffer[WRITE_BUFFER_SIZE];
	int   read_used, write_used;
	DIR  *dir;
	int   fd, id;
	int   allow_write, allow_read, eof;
	off_t file_size, trans_bytes;
};

static struct hostfs_channels_st hostfs_channels[16];
static Uint8 hfs_status;
static Uint8 spec_filling[SPEC_BUFFER_SIZE];
static Uint8 spec_used[SPEC_BUFFER_SIZE];
static int spec_filling_index;
static int last_command;
static struct hostfs_channels_st *use_channel;
static char hostfs_directory[PATH_MAX];



static Uint8 set_error ( int status, int errorcode, int tracknum, int sectnum, const char *description )
{
	const char *p;
	hfs_status = status;
	switch (errorcode) {
		case  0: p = "OK"; break;
		case 73: p = "XEMU SOFTWARE CBM-DOS SUBSET"; break;
		default:
			p = "UNHANDLED XEMU ERROR";
			fprintf(stderr, "HOSTFS DOS: error code %02d is not defined, please report this." NL, errorcode);
			break;
	}
	hostfs_channels[15].read_used = sprintf((char*)hostfs_channels[15].read_buffer, "%02d, %s,%02d,%02d", errorcode, p, tracknum, sectnum);
	DEBUG_HOSTFS("HOSTFS: error (host_status=%d) is set to \"%s\": %s." NL, status, (char*)hostfs_channels[15].read_buffer, description ? description : "-");
	return 0xFF;
}

#define NO_ERROR()	set_error(0, 0, 0, 0, NULL)



void hostfs_init ( const char *basedir, const char *subdir )
{
	DIR *dir;
	int a;
	if (subdir) {
		sprintf(hostfs_directory, "%s" DIRSEP_STR "%s" DIRSEP_STR, basedir, subdir);
		MKDIR(hostfs_directory);
	} else {
		sprintf(hostfs_directory, "%s" DIRSEP_STR, basedir);
	}
	// opendir() here is only for testing ...
	dir = opendir(hostfs_directory);
	if (!dir) {
		ERROR_WINDOW("Warning, specified hostFS directory (%s) cannot be used, you will have problems: %s", hostfs_directory, strerror(errno));
	} else
		closedir(dir);
	// intialize channels to a known state
	for (a = 0; a < 16; a++) {
		hostfs_channels[a].id = a;
		hostfs_channels[a].dir = NULL;
		hostfs_channels[a].fd = -1;
		hostfs_channels[a].allow_write = 0;
		hostfs_channels[a].allow_read = 0;
		hostfs_channels[a].read_used = 0;
		hostfs_channels[a].write_used = 0;
		hostfs_channels[a].file_size = 0;
		hostfs_channels[a].trans_bytes = 0;
	}
	set_error(0, 73, 0, 0, "init");	// set status reg, and DOS "error" ...
	last_command = -1;
	use_channel = NULL;
	printf("HOSTFS: init @ %s" NL, hostfs_directory);
	atexit(hostfs_close_all);	// registering function for exit, otherwise un-flushed write buffers may not be written on exit or panic!
}


static void hostfs_flush ( struct hostfs_channels_st *channel )
{
	int pos;
	if (channel->fd < 0 || !channel->write_used)
		return;
	pos = 0;
	DEBUG_HOSTFS("HOSTFS: flusing write cache on channel #%d for %d bytes ..." NL, channel->id, channel->write_used);
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
	if (channel->allow_write || channel->allow_read)
		DEBUG_HOSTFS("HOSTFS: closing channel #%d" NL, channel->id);
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



// Converts host filename to PETSCII, used by directory reading. Both of them are NUL terminated strings.
// Returns zero if conversion cannot be done (long filename, non-PETSCII representable char, etc)
// In case of zero value, the converted buffer is invalid, and should be not used! [zero length input is also invalid]
// Returns non-zero if success when it means the length of the file name.
// "out" buffer must be maxlen+1 bytes at least (for the plus NULL byte)
static const char banned_hostfilename_chars[] = ":;,/\\#$=*?@";
static int filename_host2cbm ( const Uint8 *in, Uint8 *out, int maxlen)
{
	int len = 0;
	if (*in == '.')
		return 0;
	for (;;) {
		Uint8 c = *(in++);
		if (!c)
			break;
		if (++len > maxlen || c < 32 || c > 126 || strchr(banned_hostfilename_chars, c))
			return 0;
		if (c >= 97 && c <= 122)
			c -= 32;
		*(out++) = c;
	}
	*out = 0;
	return len;
}


static int xemu_stat_at ( const char *dirname, const char *filename, struct stat *st )
{
	char namebuffer[PATH_MAX];
	if (snprintf(namebuffer, sizeof namebuffer, "%s" DIRSEP_STR "%s", dirname, filename) >= sizeof namebuffer) {
		errno = ENAMETOOLONG;
		return -1;
	}
	return stat(namebuffer, st);
}


static int xemu_open_at ( const char *dirname, const char *filename, int flags, mode_t mode )
{
	char namebuffer[PATH_MAX];
	if (snprintf(namebuffer, sizeof namebuffer, "%s" DIRSEP_STR "%s", dirname, filename) >= sizeof namebuffer) {
		errno = ENAMETOOLONG;
		return -1;
	}
	return open(namebuffer, flags, mode);
}


// Note: CBM DOS standard for pseudo-BASIC directory to use load address $0401, this is because of the PET-era.
// PETs were incapable of relocting BASIC programs, so directory still use their BASIC load addr, newer CBM
// machines can relocate, so they have no problem with this address, at the other hand.
#define CBM_DIR_LOAD_ADDRESS	0x0401


static const Uint8 dirheadline[] = {
	CBM_DIR_LOAD_ADDRESS & 0xFF, CBM_DIR_LOAD_ADDRESS >> 8,				// load address (not the part of the basic program for real)
	1, 1,		// link to the next line, simple version, just say something non-zero ...
	0, 0,		// line number
	0x12, '"',	// REVS ON and quote mark
	'X','E','M','U','-','C','B','M',' ','H','O','S','T','-','F','S',
	'"',' ','X','E',' ','M','U',0
};
static const Uint8 dirtailline[] = {
	1, 1,	// link addr
	0xFF,0xFF,	// free blocks number as line number, FIXME: later it should be set, if host OS has disk with less free space than 0xFFFF * 254 bytes ?!
	'B','L','O','C','K','S',' ','F','R','E','E','.',
	' ',' ',' ',' ',' ',' ',' ',' ',' ',' ',' ',' ',' ',
	0, 0, 0
};



static int cbm_read_directory ( struct hostfs_channels_st *channel )
{
	// channel->allow_read is also used to track the memory address in the generated BASIC program for directory listing,
	// since it's basically a boolean value, so any non-zero value has the same "boolean value".
	if (channel->allow_read == 1) {
		// first item ... not a real directory entry, but the "header"
		memcpy(channel->read_buffer, dirheadline, sizeof dirheadline);
		channel->allow_read = 2;
		channel->read_used = sizeof dirheadline;
		return 0;
	}
	if (!channel->dir) {
		channel->eof = 1;
		return 64;
	}
	for (;;) {
		struct dirent *entry = readdir(channel->dir);	// read host directory
		if (entry) {
			Uint8 *p = channel->read_buffer;
			Uint8 filename[17];
			int namelength, i;
			struct stat st;
			if (!(namelength = filename_host2cbm((Uint8*)entry->d_name, filename, 16)))
				continue;	// cannot be represented name, skip it!
			if (xemu_stat_at(hostfs_directory, entry->d_name, &st))
				continue;
			if (!S_ISREG(st.st_mode) && !S_ISDIR(st.st_mode))
				continue;
			if (channel->allow_read - 2 == CBM_MAX_DIR_ENTRIES)
				goto too_many_entries;
			channel->allow_read++;
			// Ok, it seems to be OK, let's present a directory line for the entry!
			*(p++) = 1;	// link address, lo
			*(p++) = 1;	// link address, hi
			st.st_size = (st.st_size + 253) / 254;	// convert size to "blocks"
			i = st.st_size > 0xFFFF ? 0xFFFF : st.st_size;
			*(p++) = i & 0xFF;
			*(p++) = i >> 8;
			if (i < 10)	*(p++) = ' ';
			if (i < 100)	*(p++) = ' ';
			if (i < 1000)	*(p++) = ' ';
			*(p++) = '"';
			for (i = 0; i < 16; i++)
				*(p++) = i < namelength ? filename[i] : (i == namelength ? '"' : ' ');
			if (S_ISDIR(st.st_mode)) {
				*(p++) = ' ';
				*(p++) = 'D';
				*(p++) = 'I';
				*(p++) = 'R';
			} else {
				*(p++) = st.st_size ? ' ' : '*';
				*(p++) = 'P';
				*(p++) = 'R';
				*(p++) = 'G';
			}
			*(p++) = ' ';	// later: read-only files with '<' TODO
			*(p++) = 0;
			channel->read_used = p - channel->read_buffer;
		} else {
		too_many_entries:
			closedir(channel->dir);
			channel->dir = NULL;
			// Put tail basic line ("BLOCKS FREE")!
			// Note, we always give back 65535 BLOCK FREE, but since about ~16Mbyte is usually have on host-FS nowadays,
			// I guess it simple does not worth to invoke a statfs() call just for this ...
			memcpy(channel->read_buffer, dirtailline, sizeof dirtailline);
			channel->read_used = sizeof dirtailline;
		}
		return 0;
	}
}



static Uint8 char_cbm2ascii ( Uint8 c )
{
	if (c >= 65 && c <= 90)
		return c + 32;
	if (c >= 193 && c <= 218)
		return c - 96;
	return c;
}



static void hostfs_open ( struct hostfs_channels_st *channel )
{
	DEBUG_HOSTFS("HOSTFS: opening channel #%d ..." NL, channel->id);
	if (channel->id == 15) {
		FATAL("Sorry, channel#15 is not supported yet, and you must not use it!");
	}
	hostfs_close(channel);	// close channel (maybe it was used before)
	if (spec_used[0] == 0) {	// empty name/command is not valid
		hfs_status = 64;
		return;
	}
	channel->write_used = 0;
	channel->read_used = 0;
	channel->eof = 0;
	channel->file_size = 0;
	channel->trans_bytes = 0;
	if (spec_used[0] == '$') {
		channel->dir = opendir(hostfs_directory);
		if (!channel->dir) {
			hfs_status = 128;
			DEBUG_HOSTFS("HOSTFS: cannot open directory '%s': %s" NL, hostfs_directory, strerror(errno));
			return;
		}
		channel->allow_write = 0;
		channel->allow_read  = 1;
		hfs_status = cbm_read_directory(channel);
	} else {
		/* normal file must be open ... */
		int flags, len;
		Uint8 *p0, *p1;
		DIR *dir;
		struct dirent *entry;
		char filename[PATH_MAX];
		p1 = (Uint8*)strchr((char*)spec_used, ',');
		if (p1) {
			Uint8 *p2;
			flags = char_cbm2ascii(p1[1]);
			DEBUG_HOSTFS("HOSTFS: file type character is '%c'" NL, flags);
			if (flags != 'p' && flags != 'u' && flags != 's') {
				DEBUG_HOSTFS("HOSTFS: unknown file type character!" NL);
				hfs_status = 128;
				return;
			}
			p2 = (Uint8*)strchr((char*)p1 + 1, ',');
			len = (int)(p1 - spec_used);
			if (p2) {
				if (strchr((char*)p2 + 1, ',')) {
					hfs_status = 128;
					return;
				}
				flags = char_cbm2ascii(p2[1]);
			} else
				flags = channel->id == 1 ? 'w' : 'r';
		} else {
			len = strlen((char*)spec_used);
			flags = channel->id == 1 ? 'w' : 'r';	// TODO: is it correct? channel-1 is write by default. OR ONLY?! FIXME
		}
		if (len >= PATH_MAX) {
			hfs_status = 64;
			return;
		}
		// first char of file name can be '@' to signal overwriting situation!
		if (spec_used[0] == '@') {
			p0 = spec_used + 1;
			len--;
			if (flags == 'w') flags = 'o'; // overwrite mode
		} else
			p0 = spec_used;
		// Skip the possible part of drive assignment (ie "0:FILENAME...")
		if (len > 1 && p0[1] == ':') {
			if (p0[0] != '0') {
				hfs_status = 128;
				return;
			}
			p0  += 2;
			len -= 2;
		}
		// check if we have an invalid, empty file name
		if (!len) {
			hfs_status = 128;
			return;
		}
		DEBUG_HOSTFS("HOSTFS: file mode char: '%c'" NL, flags);
		switch (flags) {
			case 'a':	// append mode
				channel->allow_write = 1;
				channel->allow_read  = 0;
				flags = O_WRONLY | O_APPEND | O_BINARY; // FIXME: append mode allows _creation_ of file?! TODO
				DEBUG_HOSTFS("HOSTFS: file is selected for append" NL);
				break;
			case 'o':	// overwrite mode ("fake" mode, created with 'w' mode and '@' prefix above)
				channel->allow_write = 1;
				channel->allow_read  = 0;
				flags = O_WRONLY | O_CREAT | O_TRUNC | O_BINARY;
				DEBUG_HOSTFS("HOSTFS: file is selected for overwrite" NL);
				break;
			case 'w':	// write mode
				channel->allow_write = 1;
				channel->allow_read  = 0;
				flags = O_WRONLY | O_CREAT | O_EXCL | O_BINARY;
				DEBUG_HOSTFS("HOSTFS: file is selected for write" NL);
				break;
			case 'r':	// read mode
			case 'm':	// no idea what it is, "modify" but strangely it seems also to be 'read' ...
				channel->allow_write = 0;
				channel->allow_read  = 1;
				flags = O_RDONLY | O_BINARY;
				DEBUG_HOSTFS("HOSTFS: file is selected for read" NL);
				break;
			default:
				hfs_status = 128;
				DEBUG_HOSTFS("HOSTFS: invalid file mode char!" NL);
				return;
		}
		memcpy(filename, p0, len);
		filename[len] = 0;
		p0 = (Uint8*)filename;
		while (*p0) {	// convert filename
			*p0 = (*p0 == '/' || *p0 == '\\') ? '.' : char_cbm2ascii(*p0);
			p0++;
		}
		dir = opendir(hostfs_directory);
		if (!dir) {
			hfs_status = 128;
			DEBUG_HOSTFS("HOSTFS: cannot open directory '%s': %s" NL, hostfs_directory, strerror(errno));
			return;
		}
		DEBUG_HOSTFS("HOSTFS: trying to open file %s" NL, filename);
		/* Note: we have to walk the directory, as host OS may have case sensitive, or case-insensitive
		   file names, also there will be PETSCII-ASCII file name conversion later, etc ...
		   So we try to find a matching file, even if file name can't contain "joker" character for now ... */
		while ((entry = readdir(dir))) {
			DEBUG_HOSTFS("HOSTFS: considering file %s" NL, entry->d_name);
			if (!strcasecmp(entry->d_name, filename)) {
				struct stat st;
				DEBUG_HOSTFS("HOSTFS: file name accepted." NL);
				closedir(dir);
				if (xemu_stat_at(hostfs_directory, entry->d_name, &st)) {
					DEBUG_HOSTFS("HOSTFS: cannot stat file!" NL);
					hfs_status = 128;
					return;
				}
				if (!S_ISREG(st.st_mode)) {
					// TODO: maybe in the future directory entries are "fake-loadable" to be able to change hostFS directory?
					DEBUG_HOSTFS("HOSTFS: not a regular file!" NL);
					hfs_status = 128;
					return;
				}
				channel->file_size = st.st_size;
				channel->fd = xemu_open_at(hostfs_directory, entry->d_name, flags, 0666);
				if (channel->fd < 0) {
					DEBUG_HOSTFS("HOSTFS: could not open host file '%s" DIRSEP_STR "%s': %s" NL, hostfs_directory, entry->d_name, strerror(errno));
					hfs_status = 128;
				} else {
					DEBUG_HOSTFS("HOSTFS: great, file is open as '%s'" NL, filename);
					hfs_status = 0;
				}
				return;
			}
		}
		closedir(dir);
		// on write, if we don't have matching entry (we are here ...), we want to create new file here!!!
		if (flags & O_CREAT) {
			channel->fd = xemu_open_at(hostfs_directory, filename, flags, 0666);
			if (channel->fd < 0) {
				DEBUG_HOSTFS("HOSTFS: could not create new host file entry '%s" DIRSEP_STR "%s': %s" NL, hostfs_directory, filename, strerror(errno));
				hfs_status = 128;
			} else {
				DEBUG_HOSTFS("HOSTFS: great, new host file is created as '%s'" NL, filename);
				hfs_status = 0;
			}
		} else {
			hfs_status = 128;
			DEBUG_HOSTFS("HOSTFS: no matching pattern found for opening file" NL);
		}
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
		case 0:	// start specification of channel, low nibble has no meaning
			spec_filling_index = 0;
			break;
		case 1:	// open/execute specified file/command, low nibble is the channel number
			if (spec_filling_index >= SPEC_BUFFER_SIZE) {
				hfs_status = 64;
			} else {
				spec_filling[spec_filling_index] = 0;
				strcpy((char*)spec_used, (char*)spec_filling);
				spec_filling_index = 0;
				DEBUG_HOSTFS("HOSTFS: command=\"%s\" on channel #%d" NL, (char*)spec_used, channel->id);
				hostfs_open(channel);
			}
			break;
		case 2: // re-execute/re-open last issued specification for channel in low-nibble
			hostfs_open(channel);
			break;
		case 3:	// close channel for low-nibble
			hostfs_close(channel);
			break;
		case 4: // set channel (in low nibble) to be used for data register R/W
			use_channel = channel;
			break;
		case 5:	// get number of bytes info [special command, the answer is sent back via the status register!]
			if (use_channel) {
				last_command = 4;
				hfs_status = ((data & 8 ? use_channel->trans_bytes : use_channel->file_size) >> ((data & 3) << 3)) & 0xFF;
				if (data & 4)
					use_channel->trans_bytes = 0;
			}
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
		hfs_status = cbm_read_directory(use_channel);
		if (!hfs_status)
			goto ret_one;
		return 0xFF;
	}
	if (use_channel->fd < 0) {
		hfs_status = 64;	// not open channel
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
	use_channel->trans_bytes++;
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
	if (use_channel->fd < 0) {
		hfs_status = 64;
		return;
	}
	if (use_channel->write_used == WRITE_BUFFER_SIZE)
		hostfs_flush(use_channel);
	use_channel->write_buffer[use_channel->write_used++] = data;
	use_channel->trans_bytes++;
	hfs_status = 0;
}
