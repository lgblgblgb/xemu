/* Z8010 MMU emulator
 * Written from the technical manual of the Z8000/Z8010 released by Zilog by me,
 * so it can be very ugly, strange and incorrect code, especially, because
 * Z8000/Z8010 is a new thing me, first try to even get to know it ... */

/* What I know currently:

	MMU contains table for 64 segments

	A process called (by Zilog) "relocation" never touches the lower 8 bytes of generated address.
	(I won't name this way, for me, relocation is a software based thing ...)

	A 24 bit physical addrss is built as output with:
		* So, lower 8 bits are unaltered
			THIS ALSO MEANS THAT IT IS NO NEED FOR THE LOW 8 BITS TO GO INTO THE MMU AND SINCE
			DATA/ADDRESS LINES ARE COMMON ON THE BUS, LOWER 8 BITS ARE NOT USED WHEN PROGRAMMING
			THE MMU EITHER! Warning, big trap for youngplayers :)
		* Higher 16 bits are the output of a 16 bit adder which has two inputs:
			* the higher 8 bits of the CPU offset (the missing upper bits are zero)
			* the base address from the segment table

	Z800X uses "special I/O instructions" to program the MMU. I am not sure why not normal ones, the manual
	says something that special ones provides the bus cycle/state transitions needed by the MMU. OK then ...

*/

#include "xemu/emutools.h"
#include "z8010.h"



static Uint16 segment_bases[64];
static Uint8  segment_limits[64];
static Uint8  segment_attributes[64];
static Uint8  mode,sar,dsc;


void z8010_init ( void )
{
}

void z8010_reset ( void )
{
	for (int a = 0; a < 64; a++) {
		segment_bases[a] = 0;
		segment_limits[a] = 0;
		segment_attributes[a] = 0;
	}
}


Uint8 z8010_read_register ( int port )
{
	return 0xFF;
}


static void write_descriptor ( int dsc_now, Uint8 data )
{
	switch (dsc_now & 3) {
		case 0:
			segment_bases[sar] = (data << 8) | (segment_bases[sar] & 0xFF);
			DEBUGPRINT("Z8010: segment #$%02X base high is set to $%02X, value is now = %04X" NL, sar, data, segment_bases[sar]);
			break;
		case 1:
			segment_bases[sar] = (segment_bases[sar] & 0xFF00) | data;
			DEBUGPRINT("Z8010: segment #$%02X base low is set to $%02X, value is now = %04X" NL, sar, data, segment_bases[sar]);
			break;
		case 2:
			segment_limits[sar] = data;
			DEBUGPRINT("Z8010: segment #$%02X limit is set to $%02X" NL, sar, data);
			break;
		case 3:
			segment_attributes[sar] = data;
			DEBUGPRINT("Z8010: segment #$%02X attribute is set to $%02X" NL, sar, data);
			break;
	}
}


static Uint8 read_descriptor ( int dsc_now )
{
	switch (dsc_now & 3) {
		case 0:	return segment_bases     [sar] >> 8;
		case 1:	return segment_bases     [sar] & 0xFF;
		case 2:	return segment_limits    [sar];
		case 3:	return segment_attributes[sar];
	}
}






// Z8010 has connection for the AD8-AD15 lines only with the CPU. That is,
// even writing MMU registers, only the high byte counts on the data and
// the MMU register selection as well! However, to simplify things, we assume
// here that these fuctions to read/write MMU registers are now called
// with "normalized" values, ie port number and data being a byte entity.

void z8010_write_register ( int port, Uint8 data )
{
	switch (port) {
		case 0x00:
			mode = data;
			break;
		case 0x01:
			sar = data & 63;
			dsc = 0;	// FIXME: do we need this?
			break;
		case 0x08:	// base field of descriptor
			write_descriptor(dsc & 1, data);
			dsc++;
			break;
		case 0x09:	// limit
			write_descriptor(2, data);
			break;
		case 0x0A:	// attribute field
			write_descriptor(3, data);
			break;
		case 0x0B:	// descriptor, ALL fields!!!!
			write_descriptor(dsc, data);
			dsc++;
		case 0x0C:	// base field, incrementing SAR
			write_descriptor(dsc & 1, data);
			sar = (sar + 1) & 63;
			break;
		case 0x0D:	// limit field, incrementing SAR
			write_descriptor(2, data);
			sar = (sar + 1) & 63;
			break;
		case 0x0E:	// attribute field, incrementing SAR
			write_descriptor(3, data);
			sar = (sar + 1) & 63;
			break;
		case 0x0F:
			write_descriptor(dsc, data);
			dsc = (dsc + 1) & 3;
			if (!dsc)
				sar = (sar + 1) & 63;
			break;

		case 0x20:	// descriptor counter
			dsc = data & 3;
			DEBUGPRINT("Z8010: DSC is set to %d" NL, dsc);
			break;

	}
}


// Output: 24 bit linear address, _OR_ negative number, indicating segmenting violation
int z8010_translate ( int is_system_mode, int is_execute, int is_write, Uint8 segment, Uint16 offset )
{
	//if ((!!(segment & 64)) != URS) {
	//}
	segment &= 63;
	return ((segment_bases[segment & 63] << 8) + offset) & 0xFFFFFF;
}
