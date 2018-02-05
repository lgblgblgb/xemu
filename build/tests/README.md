# Test programs, examples, etc

This directory contains various tests for Xemu.

## cbmhostfs

`CBMhostFS` is intended to be a way to interact files on your host OS file
system, which means the file system of the OS, which runs the emulator itself.
It's an Xemu specific solution. Currently, only C65 emulator implements it
(not the M65).

## ethernet-m65

This sub-project has been moved to here: https://github.com/lgblgblgb/mega65-utils/tree/master/eth-tool

Testing ethernet emulation of Xemu/Mega65. This piece of code should run on
a real M65 as well. It does ARP reply, ICMP echo request reply (ping), custom
UDP based "monitor port", TFTP server and DHCP client. The status of the project
can be checked on the link above, though.
