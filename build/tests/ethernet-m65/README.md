A really simple program to test Ethernet emulation of Xemu/Mega65. Which
requires Linux, with TAP device support!

*Surely, the test program itself should work on a real M65 as well!*

Please review the Makefile before you try anything!

## Test with Xemu

    make xemu

That should build it and also configure TAP device for you. It also starts
Xemu/Mega65 emulator with disk image attached. Answer 'External D81' if Xemu
ask it at all, then go to C64 mode (`GO64`) then `LOAD"ETH"`.

At this point, in theory, you can ping it: `pind 10.10.10.65`, or you
can see it in your ARP table: `arp -an`.

## Testing with Mega65

    make board

Similar as above, your Nexys4 board should be switched on, etc etc.

Makefile assumes, that M65 repository is put at the same level as `xemu`
with the name of `mega65-core`. That should be a version/branch,
supporting bug-free Ethernet implementation, even for TX what had some
issues in the past.

## NOTES:

* The software answers for each IPv4 address ends in '65'.
  This is just a comfortable way to test on eg both a real a LAN, and
  with the Xemu/Mega65 TAP device, where you need to use different IP
  ranges for example, but we don't want to test for two separated IPs.
  Still, you must be careful not to collide with something, using the
  IPv4 address already with the 4th octet being '65'.
* The Makefile configures TAP device for 10.10.10.0/24. If it's used
  by you somewhere on the Linux box which runs Xemu, you should modify
  it.
* The `arp -d` statements in the Makefile are just for convience. Eg,
  you want to delete arp cache on your Linux box, to see, if the
  program on M65 (emulated or real) really answer an ARP request, and
  it's not only a cached value. Use `arp -an` to see your ARP table.
* This program won't work on an Ethernet LAN not using Ethernet-II
  type frames. Also it probably won't run, if you use some extra
  features, like VLANs etc, which may tags ethernet frames before
  the real data payload.
* The program uses a hard-wired MAC address. This should be the same
  what Xemu and M65 (at least according to its VHDL) uses by default.
  There should be no mismatch, as MAC filter option would rule frames
  out otherwise. In theory, M65 provides a way to get/set MAC address
  through the VIC-IV I/O mode I/O area, but for me it didn't worked
  for some reason (maybe too old bitstream?).
* The three long numbers are actually MAC addresses. The first is the
  M65 specific MAC registers read. The second is the valus being
  programmed, the third is the read back value. Actually since
  currently not so much used, and the same as M65's default MAC,
  they should be all the same.
* The 4 digit hex values then are content of MIIM registers.


