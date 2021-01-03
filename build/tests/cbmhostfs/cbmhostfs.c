/* Test program for Xemu/CBMhostFS, use it with CC65 compiler.

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



#include <stdio.h>

typedef unsigned char BYTE;

#define POKE(a,d) *(BYTE*)(a) = (d)
#define PEEK(a)	  (*(BYTE*)(a))

#define HOSTFSR0	0xD0FEU
#define HOSTFSR1	0xD0FFU


#define HOSTFS_READ	0
#define HOSTFS_OVERWRITE	1
#define HOSTFS_WRITE	2

/* Open a HostFS file
   channel: channel number (0-15), for future compatibility, values 2-14 should be used (15 will be the command/status channel as with CBM DOS)
   filename: guess what ... currently there is no PETSCII-ASCII conversion, so be careful!
   mode: HOSTFS_READ, HOSTFS_WRITE, HOSTFS_OVERWRITE, only one! Note: write access does not work (at all or too well ...) yet.
   Return value: 0 = ok, non-zero = error
*/
static BYTE hostfs_open ( BYTE channel, const char *filename, BYTE mode )
{
	channel &= 0xF;
	POKE(HOSTFSR0,0x00);	// signal start transmission of "channel specification" (ie: file name), low nibble is not used
	// TODO: we don't check status on transmitting file name ... That should be a mistake, however if filename is not too long it should be no problem.
	if (mode == HOSTFS_OVERWRITE) {
		POKE(HOSTFSR1,'@');	// send the '@' prefix if overwrite is instructed
		mode = HOSTFS_WRITE;
	}
	while (*filename)
		POKE(HOSTFSR1,*(filename++));	// transmit filename
	if (mode == HOSTFS_WRITE) {
		POKE(HOSTFSR1,',');
		POKE(HOSTFSR1,'p');	// this would be "P" as "PRG" file, though it's currently ignored (but needed!)
		POKE(HOSTFSR1,',');
		POKE(HOSTFSR1,'w');	// Write access ...
	}
	POKE(HOSTFSR0,0x10 | channel);	// open "call", low nibble is channel number to open the desired filename at.
	return PEEK(HOSTFSR0);
}


static BYTE hostfs_close ( BYTE channel )
{
	POKE(HOSTFSR0, 0x30 | (channel & 0xF));	// close function
	return PEEK(HOSTFSR0);
}


static int hostfs_read ( BYTE channel, void *buffer_spec, int num )
{
	int numread = 0;
	BYTE *buffer = buffer_spec;
	POKE(HOSTFSR0, 0x40 | (channel & 0xF));	// set channel (already open channel) number for communication
	while (numread < num) {
		BYTE data = PEEK(HOSTFSR1);	// try to read one byte, this must be the FIRST, and status must check after this, if this byte is valid at all!
		// check status
		BYTE status = PEEK(HOSTFSR0);
		if (status & 64)	// EOF?
			break;
		if (status & 128)
			return -1;	// some kind of hostFS I/O error?
		numread++;
		*(buffer++) = data;
	}
	return numread;
}


static int hostfs_write ( BYTE channel, void *buffer_spec, int num )
{
	int numwrite = 0;
	BYTE *buffer = buffer_spec;
	POKE(HOSTFSR0, 0x40 | (channel & 0xF)); // set channel (already open channel) number for communication
	while (numwrite < num) {
		POKE(HOSTFSR1, *(buffer++));
		if (PEEK(HOSTFSR0))
			return -1;
		numwrite++;
	}
	return numwrite;
}




static const char *TEST_FILE_NAME = "testfile"; // filename. BEWARE! currently emulator does not do conversion PETSII<->ASCII etc ......

static BYTE buffer[256];



int main ( void )
{
	int ret,n;
	/* Use C65 I/O mode */
	POKE(0xD02FU,0xA5);
	POKE(0xD02FU,0x96);
	/* whatever ... */
	POKE(53281U,0);

	/* trying to open a file */
	printf("Trying to open file: %s\n", TEST_FILE_NAME);
	if (hostfs_open(2, TEST_FILE_NAME, HOSTFS_READ)) {
		printf("Could not open hostfs file!\n");
		printf("Continue with test #2\n");
		goto test2;
	}
	printf("OK, file is open\n");
	ret = hostfs_read(2, buffer, sizeof buffer);
	printf("File read returned with = %d\n", ret);
	if (ret < 0)
		return 1;
	printf("Dumping read data ...\n");
	n = 0;
	while (n < ret) {
		putchar(buffer[n++]);
	}
	putchar('\n');

	printf("Closing file\n");
	hostfs_close(2);

	/* test-2: try to open directory, dump that into a new file, overwriting it */

test2:
	printf("Test#2: dump dir to dirfile\n");

	if (hostfs_open(3, "$", HOSTFS_READ)) {
		printf("Cannot open file $\n");
		return 1;
	}
	if (hostfs_open(4, "dirlist", HOSTFS_OVERWRITE)) {
		printf("Cannot create file dirlist\n");
		return 1;
	}
	for (;;) {
		int r = hostfs_read(3, buffer, sizeof buffer);
		printf("Read retval: %d\n", r);
		if (r < 0) {
			printf("I/O error while reading $\n");
			hostfs_close(3);
			hostfs_close(4);
			return 1;
		}
		if (r == 0)
			break;
		if (hostfs_write(4, buffer, r) != r) {
			printf("Error writing file dirlist\n");
			hostfs_close(3);
			hostfs_close(4);
			return 1;
		}
	}
	hostfs_close(3);
	hostfs_close(4);

	printf("END :-)\n");
	return 0;
}
