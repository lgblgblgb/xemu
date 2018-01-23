TAP_DEVICE	= mega65

#M65_BITSTREAM	= /home/lgb/prog_here/mega65-core/bin/nexys4ddr.bit.NEWEST-BEFORE
M65_BITSTREAM	= ../../../../mega65-core/bin/nexys4ddr.bit.NEWEST-BEFORE
#M65_MON_LOADER	= /usr/local/bin/mega65-monitor_load
M65_MON_LOADER	= ../../../../mega65-core/src/tools/monitor_load

CBMCONVERT	= cbmconvert
CL65		= cl65
XEMU		= ../../bin/xmega65.native
SUDO		= sudo

PRG		= eth
DISKIMAGE	= eth.d81
TARGETS		= $(PRG) $(DISKIMAGE)

all:	$(TARGETS)

$(DISKIMAGE): $(PRG)
	$(CBMCONVERT) -v2 -D8o $(DISKIMAGE) $(PRG)

$(PRG): eth.asm
	$(CL65) -o $(PRG) -t c64 -l list $<

tapcfg:
	test ! "`whoami`" = "root"
	$(SUDO) ip tuntap add mode tap user `whoami` $(TAP_DEVICE)
	$(SUDO) ip addr add 10.10.10.1/24 dev $(TAP_DEVICE)
	$(SUDO) ip link set $(TAP_DEVICE) up
	ip link list $(TAP_DEVICE)

board:	$(PRG)
	$(SUDO) arp -d 192.168.0.65 || true
	$(SUDO) arp -d 10.10.10.65 || true
	$(M65_MON_LOADER) -r -4 -b $(M65_BITSTREAM) $(PRG)

xemu:	$(DISKIMAGE)
	test ! "`whoami`" = "root"
	ip link list $(TAP_DEVICE) 2>/dev/null || $(MAKE) tapcfg
	$(SUDO) arp -d 192.168.0.65 || true
	$(SUDO) arp -d 10.10.10.65 || true
	$(XEMU) -ethertap $(TAP_DEVICE),debug -8 $(DISKIMAGE)

clean:
	rm -f $(TARGETS) *.o list uart.sock dump.mem

.PHONY: clean board xemu tapcfg
