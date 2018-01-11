/* Xemu - Somewhat lame emulation (running on Linux/Unix/Windows/OSX, utilizing
   SDL2) of some 8 bit machines, including the Commodore LCD and Commodore 65
   and some Mega-65 features as well.
   Copyright (C)2018 LGB (Gábor Lénárt) <lgblgblgb@gmail.com>

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


/* http://backreference.org/2010/03/26/tuntap-interface-tutorial/

Ether-TAP interface handling for Linux. Currently, I don't know
if other OSes has some kind of TAP interface as well (MacOS
- AFAIK - supports only TUN, but not TAP, what we need here,
I have no idea about Windows).



Assuming that you have Linux which is needed:

   First, please set the TAP device up, like:

lgb@oxygene:~$ sudo ip tuntap add mode tap user lgb mega65
lgb@oxygene:~$ sudo ip addr add 10.10.10.1/24 dev mega65
lgb@oxygene:~$ sudo ip link set mega65 up

Here, I choose 'mega65' as the interface name, and 'lgb' is my
user name. The user name is needed so an unprivileged process
then (this program, or later Xemu) can use the TAP inteface
without needing to run the program as root, which would be
very dangerous especially for a program not designed to be
security aware, like an emulator (Xemu) otherwise. Also, the
10.10.10.1 is set for the interface address, with netmask of
/24 (255.255.255.0).

Then, you can start this program with parameter mega65 to
indicate the TAP device name to be used.

lgb@oxygene:~$ gcc -Wall tap.c 
lgb@oxygene:~$ ./a.out mega65

Here, mega65 parameter is the device name we configured above.
I run as user lgb, what I configured the device for (note:
the program can run without it, but then it allocates a new
interface, it won't work a user only as root, but I want to
avoid that, as the TAP handling part needed for the Xemu
and it's really a bad idea to run Xemu as root for obvious
security reasons! Even if it would mean that Xemu would be
able to configure the device for itself, hmmm ...)

This program implements a very crude test, that it answers
ARP requests, and also to ICMP echo requests ("ping").

NOTE: the source is hard-wired to use 10.10.10.10 address.
So you should use a network for the TAP device config which
includes this address, like the example 10.10.10.1/24, thus
the Linux box itself will have 10.10.10.1, and this program
10.10.10.10

The desired IP and MAC address for ourselves are defined in
this program, see later. */

#ifdef HAVE_ETHERTAP

#include "xemu/emutools_basicdefs.h"
#include "xemu/ethertap.h"

#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <unistd.h>
#include <linux/if.h>
#include <linux/if_tun.h>
#include <errno.h>


#define CLONE_TUNTAP_NAME "/dev/net/tun"


static volatile int tuntap_fd = -1;


int xemu_tuntap_close ( void )
{
	if (tuntap_fd >= 0) {
		int fd = tuntap_fd;
		tuntap_fd = -1;
		return close(fd);
	}
	return 0;
}


int xemu_tuntap_read ( void *buffer, int min_size, int max_size )
{
	int got = 0;
	if (tuntap_fd < 0)
		return 0;
	while (got < min_size) {
		int r = read(tuntap_fd, buffer, max_size);
		if (r <= 0) {
			if (errno == EINTR)
				continue;
			if (errno == EAGAIN || errno == EWOULDBLOCK)
				break;
			xemu_tuntap_close();
			return r;
		}
		max_size -= r;
		got += r;
		buffer += r;
	}
	return got;
}


int xemu_tuntap_write ( void *buffer, int size )
{
	int did = 0;
	if (tuntap_fd < 0)
		return 0;
	while (size > 0) {
		int w = write(tuntap_fd, buffer, size);
		if (w <= 0) {
			if (errno == EINTR)
				continue;
			if (errno == EAGAIN || errno == EWOULDBLOCK)
				break;
			xemu_tuntap_close();
			return w;
		}
		size -= w;
		did += w;
		buffer += w;
	}
	return did;
}


/* dev_in: device name to attach too, if it's zero sized string or NULL,
   the kernel will allocate a new name,
   but for that Xemu needs network admin capability (typically being 'root')
   which is not so much a good idea! 
   dev_out: a large enough buffer where name will be written back to. If it's
   NULL pointer, it won't be used.
   dev_out_size: size of dev_out buffer storage
   flags: XEMU_TUNTAP_IS_TAP / XEMU_TUNTAP_IS_TUN, only one, but one is compulsory!
          XEMU_TUNTAP_NO_PI can be OR'ed to the type above though
*/

int xemu_tuntap_alloc ( const char *dev_in, char *dev_out, int dev_out_size, unsigned int flags )
{
	struct ifreq ifr;
	int fd, err;
	if (tuntap_fd >= 0)
		return tuntap_fd;
	memset(&ifr, 0, sizeof(ifr));
	ifr.ifr_flags = 0;
	switch (flags & 0xFF) {
		case XEMU_TUNTAP_IS_TAP:
			ifr.ifr_flags = IFF_TAP;
			break;
		case XEMU_TUNTAP_IS_TUN:
			ifr.ifr_flags = IFF_TUN;
			break;
		default:
			return -1;
	}
	if (flags & XEMU_TUNTAP_NO_PI)
		ifr.ifr_flags |= IFF_NO_PI;
  //char *clonedev = "/dev/net/tun";

  /* Arguments taken by the function:
   *
   * char *dev: the name of an interface (or '\0'). MUST have enough
   *   space to hold the interface name if '\0' is passed
   * int flags: interface flags (eg, IFF_TUN etc.)
   */

	if ((fd = open(CLONE_TUNTAP_NAME, O_RDWR)) < 0)
		return fd;
	if (dev_in && *dev_in) {
		/* if a device name was specified, put it in the structure; otherwise,
		* the kernel will try to allocate the "next" device of the
		* specified type */
		strncpy(ifr.ifr_name, dev_in, IFNAMSIZ);
	} else
		ifr.ifr_name[0] = 0;
	/* try to create the device */
	if ((err = ioctl(fd, TUNSETIFF, (void *) &ifr)) < 0) {
		close(fd);
		return err;
	}
	/* if the operation was successful, write back the name of the
	 * interface to the variable "dev", so the caller can know
	 * it. Note that the caller MUST reserve space in *dev (see calling
	 * code below) */
	if (dev_out) {
		if (strlen(ifr.ifr_name) >= dev_out_size) {
			close(fd);
			return -1;
			
		} else
			strcpy(dev_out, ifr.ifr_name);
	}
	/* this is the special file descriptor that the caller will use to talk
	 * with the virtual interface */
	tuntap_fd = fd;	// file descriptor is available now, good
	return fd;	// also return the FD, but note, that it should not be used too much currently outside of this source
}

#endif
