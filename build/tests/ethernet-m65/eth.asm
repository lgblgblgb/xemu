; ** A work-in-progess Mega-65 (Commodore-65 clone origins) emulator
; ** Part of the Xemu project, please visit: https://github.com/lgblgblgb/xemu
; ** Copyright (C)2018 LGB (Gábor Lénárt) <lgblgblgb@gmail.com>
;
; This program is free software; you can redistribute it and/or modify
; it under the terms of the GNU General Public License as published by
; the Free Software Foundation; either version 2 of the License, or
; (at your option) any later version.
;
; This program is distributed in the hope that it will be useful,
; but WITHOUT ANY WARRANTY; without even the implied warranty of
; MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
; GNU General Public License for more details.
;
; You should have received a copy of the GNU General Public License
; along with this program; if not, write to the Free Software
; Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA


; Quick and (VERY) dirty test program for M65/Ethernet (also Xemu/M65)
; Compile with cl65 -t c64

; This program only checks the last digit of IPv4 address being 65, and thinks it is its own :)
; Only Ethernet-II frames are supported so far, LANs using length field @ ethertype will fail ...
; Beware, in frames, most data in the opposite byte order, we're familiar with. However,
; eg, RX buffer's first two bytes, are M65 specific, they are NOT.

.SETCPU		"4510"
.SEGMENT	"STARTUP"

; RX and TX buffer are the same "entity", ie the same address the CPU can only write the TX buffer,
; but only read the RX buffer. However, on RX, the first two bytes are the received length of the
; frame. So it can be confusing to take this account when copy things from request to reply for
; example. That's why I use these symbols instead of addresses.
; Why these "CPU addresses"? Because MAP opcode maps the required memory range to there ...
; (Actually, the real ethernet RX/TX buffer cannot be accessed in the usual I/O area, we must
; map it, using an M65-specific MAP workflow, see the main program)

ETH_MAPPED_TO	= $6800

RX_LEN		= ETH_MAPPED_TO
RX_BUFFER	= ETH_MAPPED_TO+2
TX_BUFFER	= ETH_MAPPED_TO

RX_ETH_MAC_DST	= RX_BUFFER
RX_ETH_MAC_SRC	= RX_BUFFER+6
RX_ETHERTYPE	= RX_BUFFER+12
RX_ETH_PAYLOAD	= RX_BUFFER+14
TX_ETH_MAC_DST	= TX_BUFFER
TX_ETH_MAC_SRC	= TX_BUFFER+6
TX_ETHERTYPE	= TX_BUFFER+12
TX_ETH_PAYLOAD	= TX_BUFFER+14


; M65 can be queried (and also set, AFAIK) about its own MAC address, at the I/O area (series of 6 registers @ here)
; However (maybe I'm using an older bitstream?) currently it seems it doesn't work on M65, so I'll use a constant,
; see at the end of the program.

;OUR_MAC		= $D6E9
OUR_MAC			= OUR_MAC_GIVEN


OUR_IP_LAST_DIGIT	= 65



; This is really "overkill" sometimes to be used in this program, but makes it a bit more simple at least to overview currently ...
.MACRO	MEMCPY	target, source, bytes
	LDX	#bytes
	.LOCAL	@copy
@copy:
	LDA	source-1,X
	STA	target-1,X
	DEX
	BNE	@copy
.ENDMACRO



; START of our program, do not put code before this!!

	JSR	$E544		; clear screen
	SEI			; disable interrupts

	; M65 I/O mode
	LDA	#$47
	STA	$D02F
	LDA	#$53
	STA	$D02F


;	LDA	#21
;	STA	$D018
;lda #$0d
;sta $d020
;lda #$05
;sta $d021


	; "Nice" header ....
	LDX	#screen_header_length-1
@screen_header:
	LDA	screen_header,X
	STA	$400,X
	DEX
	BPL	@screen_header

	; Show MAC addr three times:
	; 1. reading D6E9-E registers of M65
	; 2. showing the value to re-program those registers with
	; 3. reading back the result to be checked
	LDY	#0
	LDX	#40
@show_mac:
	LDA	$D6E9,Y
	JSR	show_hex_byte
	TXA
	CLC
	ADC	#13-2
	TAX
	LDA	OUR_MAC_GIVEN,Y
	STA	$D6E9,Y
	JSR	show_hex_byte
	TXA
	ADC	#13-2
	TAX
	LDA	$D6E9,Y
	JSR	show_hex_byte
	CPY	#5
	BEQ	@end_show_mac
	TXA
	SEC
	SBC	#26
	TAX
	INY
	BRA	@show_mac
@end_show_mac:

	; Set mapping
	lda #$ff		; set Mbyte for low
        ldx #$0f	; M65 specific stuff
        ldy #$00
        ldz #$00
	MAP
	EOM

	LDA	#$80
	LDX	#$8D
	LDY	#0
	LDZ	#0
	MAP
	EOM

	JSR	eth_ack

	; **** START OF OUR MAIN LOOP ****

main_loop:
	; Waiting for packet to be received, actually do the polling here
	INC	$400+38	; just to signal we're alive
	LDA	#$20
@wait_rx:
	; this part can be safely ignored, and deleted (this routine should keep reg A though)
	JSR	update_status
	BIT	$D6E1
	BEQ	@wait_rx
	INC	$400+37	; just to signal we're alive
	; ACK RX, move RX buffer to CPU mapped, also allow eth ctrl to receive a new one "meanwhile"
	JSR	eth_ack
	; Some counter, just as some indication for the user how many frames has been received already (any frame, even skipped ones ...)
	LDX	#5
	JSR	ugly_counter
	; Ugly stuff: since this is a quick test program, we accept only frames shorter than 256 bytes
	; thus we check the size of received frame, M65 eth stores that in the first two bytes in "6502 byte order"
	; so we have to check only the high byte.
	; Also, in case of CRC error, etc, M65 eth ctrl sets the MSB of high byte, so again it's not zero, good for us!
	; Surely, for proper a proper networing, we should check frame sizes, ie ARP req is large enough for a real
	; valid ARP req, the same for other kind of packets. Now this is ugly again, we simply assume, that it's ok.
	LDA	RX_LEN + 1
	BNE	main_loop
	; Ugly stuff: we do not check if the MAC target is us at all.
	; It's because:
	; * this is a very primitive test program (surely, it's not an ideal solution, just for demo!)
	; * if it's an ARP request we check if the IPv4 address is us
	; * if it's an IPv4 packet, again, we check the IPv4 target addres
	; * sometimes eg ARP request can target no the bcast MAC address, so some may should not expect that bcast MAC target can be only ARP request asking us
	;   (yeah, still can be checked for OUR mac addr _OR_ bcast MAC addr, but anyway ...)
	
	; If first byte of ethertype is not 8, then it's not Ethernet-II frame, or anyway an Ethernet-II frame with payload what we don't support
	LDA	RX_ETHERTYPE
	CMP	#$08
	BNE	main_loop

	LDA	RX_ETHERTYPE+1
	BEQ	maybe_ipv4	; 0? then it should be an IPv4 stuff, with some payload (UDP/ICMP/TCP)
	CMP	#$06
	BNE	main_loop	; not ARP (and neither IPv4), ignore

maybe_arp:

	; Check size of wannabe ARP request, we need at least (or exactly? ... anyway) 28+6+6+2 = 42 bytes
	LDA	RX_LEN
	CMP	#42
	BCC	main_loop

	; Check if destination MAC is us, or "MAC bcast" FF:FF:FF:FF:FF:FF
	; Note: in theory, this part can be skipped with M65's filters (?)
	JSR	check_mac_target_us
	BEQ	@arp_for_us
	JSR	check_mac_target_bcast
	BNE	main_loop

@arp_for_us:

	; First, let's assume, that it's an Ethernet-II framed IPv4 related ARP request, blah-blah
	; Since, we're lazy, and *every* IP addresses ends in a given byte on the LAN is treated as being ours,
	; to save some work, check if it's true. If it seems to be "our" IP, we will still check, if
	; it's an ARP request at all :-O The reason: it's cheaper to check a single byte, and skip at
	; this point ...

	LDA	RX_ETH_PAYLOAD+27
	CMP	#OUR_IP_LAST_DIGIT		; pre-check for last IPv4 digit of ARP req, assuming, it's really an IPv4 ARP req as we want
	BNE	main_loop

	; Ok, now as we assumed it is an ARP request at all, let's check it.
	; If it turns out to be, we have already know then, we should reply.
	; We don't use the ARP sample's two bytes, it was already checked

	LDX	#8-1
@check_if_arp_req:
	LDA	RX_ETH_PAYLOAD,X
	CMP	arp_req_sample,X
	BNE	main_loop	; it doesn't seems to be an ARP request (or not even a request ...) which we're interested in
	DEX
	BPL	@check_if_arp_req

	 ; Cool enough, everything seems to be so perfect (valid ARP request for us), we want to send an ARP reply now

	LDX	#6
	JSR	make_reply_frame_eth_header

	; Copy the ARP request sample, we have to pach then a single byte, being the reply not the request actually
	MEMCPY  TX_ETH_PAYLOAD, arp_req_sample, 8
	LDA	#2
	STA	TX_ETH_PAYLOAD+7

	; Copy request's source MAC+IP (4+6=10 bytes) as the reply's target
	MEMCPY	TX_ETH_PAYLOAD+18, RX_ETH_PAYLOAD+8,10

	; Copy our MAC adress
	MEMCPY	TX_ETH_PAYLOAD+8, OUR_MAC, 6

	; Copy requested IP
	MEMCPY	TX_ETH_PAYLOAD+14, RX_ETH_PAYLOAD+24, 4
	
	; Ready to TX, setup TX size and trigger TX
	LDA	#42
	JSR	eth_do_tx
	; Now, back to the business
	JMP	main_loop

	; ---- IPv4 related ----

maybe_ipv4:

	; For IPv4 we only accept packets framed for our MAC address
	JSR	check_mac_target_us
	LBNE	main_loop

	LDA	RX_ETH_PAYLOAD
	CMP	#$45		; IPv4, and std IP header size? (Ugly: we ignore packets with options included in the header! ie, IHL field should be '5')
	LBNE	main_loop
	LDA	RX_ETH_PAYLOAD+6	; flags and a piece of fragment offset field
	AND	#$FF-$40		; ... only bits other than DF (don't fragment) field, the result must be zero to be supported by us!
	LBNE	main_loop
	LDA	RX_ETH_PAYLOAD+7	; fragment offset, other half
	LBNE	main_loop		; if non-zero, it's maybe a fragmented IP datagram, what we're not so much interested in yet.

	; Just to be sure, as we want to handle only small frames, check IP header as well, that high byte of total length is zero
	; Maybe it's just too much to check in a program like this, but anyway ...
	LDA	RX_ETH_PAYLOAD+2
	LBNE	main_loop

	; OK, as a quick workaround to avoid answering bcast ping/etc in an odd way (we copy our IP from the dest addr of the request ...)
	; we check, that at least, last digit of the target IPv4 is our "marker" (by default: 65)
	LDA	RX_ETH_PAYLOAD+19
	CMP	#OUR_IP_LAST_DIGIT
	LBNE	main_loop

	LDA	RX_ETH_PAYLOAD+9
	CMP	#1		; ICMP?
	LBNE	main_loop	; currently, we support only ICMP
	LDA	RX_ETH_PAYLOAD+20
	CMP	#8		; ICMP echo request?
	LBNE	main_loop	; from ICMP protocol we supports only ICMP echo requests
	LDA	RX_ETH_PAYLOAD+21
	LBNE	main_loop	; ICMP code within a given type, should be zero within '8' (echo request)

	; We want to answer ... :D :D

	LDX	#0
	JSR	make_reply_frame_eth_header	; create the head of answer in TX buffer type 0 ($0800 it means actually, $08 is always used, X=0)
	INC	$400+36	; ICMP heartbeat ...

	; Now the hard work, we have to build an IPv4 header, then ICMP, checksums, etc, crazy :-O

	; Clear our buffer, so we don't need to write zeroes
	; Maybe a template for IP header would be better than clear+setting manually, but anyway.
	LDA	#0
	TAX
@clear_packet_temp:
	STA	packet_temp,X
	INX
	BNE	@clear_packet_temp

	; Make the IPv4 header (zero bytes aren't written)
	; We don't set DF (Don't fragemnt) bit, since how the f*ck is want to fragment something shorter than 256 bytes even with all the Eth level header etc? ;-P

	LDA	#$45
	STA	packet_temp	; Version+IHL field
	LDA	RX_ETH_PAYLOAD+3
	STA	packet_temp+3	; full length, low byte is just copied from the request (we assume that request had IHL=5, which makes sense as we checked that ...)
	LDA	#$65		; TTL ... $65 seems to be a cool enough value ;-P
	STA	packet_temp+8
	LDA	#1
	STA	packet_temp+9	; protocol (ICMP)
	; Ok, now copy request's target as our source (that's us)
	MEMCPY	packet_temp+12, RX_ETH_PAYLOAD+16, 4
	; Ok, now copy request's source as our target
	MEMCPY	packet_temp+16, RX_ETH_PAYLOAD+12, 4

	; Calculate checksum of IPv4 header ...

	LDA	#.LOBYTE(packet_temp)
	STA	Z:checksum_data_p
	LDA	#.HIBYTE(packet_temp)
	STA	Z:checksum_data_p+1
	LDX	#20			; 20 bytes of header needs to be checksummed! (remember, IHL=5 in 32 bit words, 5*4=20)
	JSR	inet_checksum
	; Write calculated checksum into their fields
	LDA	Z:checksum
	STA	packet_temp+10
	LDA	Z:checksum+1
	STA	packet_temp+11

	; Do the ICMP part itself ...

	; Copy "rest of the ICMP header" + ICMP "data" from the request. Without too much thinking, just copy "some" size :-P
	MEMCPY	packet_temp+24, RX_ETH_PAYLOAD+24, $FF

	; That was easy ;-P But now the problem, we need to calculate ICMP checksum. That is, checksum of header+data :-@
	LDA	#.LOBYTE(packet_temp+20)
	STA	Z:checksum_data_p
	LDA	#.HIBYTE(packet_temp+20)
	STA	Z:checksum_data_p+1
	LDA	RX_ETH_PAYLOAD+3		; IP header: total length
	SEC
	SBC	#20				; minus 20 bytes of IP header
	TAX
	JSR	inet_checksum
	LDA	Z:checksum
	STA	packet_temp+22
	LDA	Z:checksum+1
	STA	packet_temp+23

	; Copy temp buffer to TX buffer
	MEMCPY	TX_ETH_PAYLOAD, packet_temp, $FF
	

	; TRANSMIT PARTY TIIIIIIME ;-P
	LDA	RX_ETH_PAYLOAD+3	; full length of packet, see comments above when we built IPv4 header!
	CLC
	ADC	#14			; still, add 14 bytes for the eth header
	JSR	eth_do_tx
	JMP	main_loop



; X=Ethertype, low byte only (hi is always 8)
make_reply_frame_eth_header:
	LDA	#8
	STA	TX_ETHERTYPE
	STX	TX_ETHERTYPE+1
	LDX	#5
@loop:
	LDA	OUR_MAC,X
	STA	TX_ETH_MAC_SRC,X
	LDA	RX_ETH_MAC_SRC,X
	STA	TX_ETH_MAC_DST,X
	DEX
	BPL	@loop
	RTS

; I have not so much idea, if it's really OK for all cases
; I just used RFC1071 to try to implement the algorithm,
; however this 1-complement addition stuff makes my head exploding ...
; Input:
;	X = number of bytes to be checked
;	ZP (checksum_data_p) pointer to the memory to be checksummed
;	The memory area being checksummed must have the checksum field ZEROED!
; Output:
;	ZP (checksum) = 16 bit word of checksum
; NOTE: unfortunately, it's not possible to use TX buffer, to fill the checksum, as it's write-only (read would reads the RX buffer instead)
inet_checksum:
	LDY	#0
	STY	Z:checksum
	STY	Z:checksum+1
	; I try to use carry around with two ADC using each other's carry result. So we need a single CLC outside of the loop
	; Also, be careful not to mess carry up inside the loop!!!! It must be preserved always, even after the second ADC,
	; as it will be go back to the first!
	CLC
	LDZ	#0
@loop:
	LDA	(checksum_data_p),Y
	ADC	Z:checksum
	STA	Z:checksum
	DEX
	BEQ	@length_was_odd
	INY
	LDA	(checksum_data_p),Y
	ADC	Z:checksum+1
	STA	Z:checksum+1
	DEX
	BEQ	@length_was_even
	INY
	BRA	@loop
@length_was_even:
	BCC	@ret
@inw:
	INW	checksum	; do not forget to do the last correction if needed, now just in a single step, using INW
@ret:
	RTS
@length_was_odd:
	BCC	@ret
	INC	Z:checksum+1
	BEQ	@inw
	RTS



eth_ack:
	; * ACK' previous packet (what LSR does, last used buffer by ctrl is moved to the mapped bit), also at the FIRST iteration, it just do some kind of "init" for us
	;   and enable RX by triggering the eth ctrl's buffer swap requirement (see: LSR)
	; * do not hold ctrl in reset (bit 0 set with ORA)
	; * clear TX/RX IRQ signals (but IRQs are NOT enabled here, we're using just polling), any write to $D6E1 do, so it's OK
	LDA	$D6E1
	LSR
	AND	#2
	ORA	#1
	STA	$D6E1
	RTS


; A = packet size to TX
eth_do_tx:
	; Set size
	STA	$D6E2	; TX size, low byte
	LDA	#0
	STA	$D6E3	; TX size, high byte
	; Some counter, just as some indication for the user how many frames has been sent already by us
	LDX	#12
	JSR	ugly_counter
	; Trigger TX
	LDA	#1
	STA	$D6E4	; D6E4 <= 01, triggers TX'ing
	RTS



; Check, if RX'ed frame targeting us
check_mac_target_us:
	LDX	#6
@loop:
	LDA	RX_ETH_MAC_DST-1,X
	CMP	OUR_MAC-1,X
	BNE	@ret
	DEX
	BNE	@loop
@ret:
	RTS

; or the "bast" (FF:FF:FF:FF:FF:FF, not so much an std notion for this ...)
check_mac_target_bcast:
	LDX	#6
@loop:
	LDA	RX_ETH_MAC_DST-1,X
	INA
	BNE	@ret	; zero flag '0' if this caused to exit
	DEX
	BNE	@loop
@ret:
	RTS


; Update a three digit decimal counter ON the screen itself!!!
; must be intiailized to '000'.
; X must points to the LAST character relative to $400 in scr mem
ugly_counter:
	JSR	@proc
	BCC	@ret
	JSR	@proc
	BCC	@ret
@proc:
	LDA	$400,X
	DEX
	CMP	#'9'
	BEQ	@nine
	INA
	STA	$401,X
	CLC
	RTS
@nine:
	LDA	#'0'
	STA	$401,X
	SEC
@ret:
	RTS


; A = hex byte to print
; X = offset from $400 in scr.mem, will be incremeneted
show_hex_byte:
	PHA
	LSR
	LSR
	LSR
	LSR
	JSR	show_hex_digit
	PLA
show_hex_digit:
	AND	#$0F
	ORA	#$30
	CMP	#$3A
	BCC	@below_a
	SBC	#$39
@below_a:
	STA	$400,X
	INX
	RTS


update_status:
	PHA	; this routine should not mess A up ...
	INC	$400+39 ; just to signal we're alive
	LDX	#120
	LDY	#7
	; Reset lower bits of reg '6, to dump some MIIM regs ...
;	LDA	$D6E6
;	AND	#$E0
;	STA	$D6E6
		; According to Paul, the PHY ID should be set to '1' for the Nexys4 board. I do this here now.
		LDA	#$20
		STA	$D6E6
	; now we can safely (?) increment it, as the reg num is zero
@showregs:
	; show reg val!
	LDA	$D6E8
	JSR	show_hex_byte
	LDA	$D6E7
	JSR	show_hex_byte
	; increment reg index:
	INC	$D6E6
	INX		; space
	DEY
	BPL	@showregs
	PLA
	RTS



; Using .DBYT is ideal, since it defines a swapped (HI-LO byte order, instead of usual 6502 LO-HI) word size data! What we need in many network-related stuff ...
; 8 bytes
arp_req_sample:
	.DBYT	1	; HTYPE, 1 = ethernet
	.DBYT	$800	; PTYPE, $800 = IPV4, actually about similar as ethertype field (it mustn't be $806, as it refers to the protocol what is ARP assigned to)
	.BYTE	6	; HLEN, size of hardware addresses, 6 for ethernet (number of octets in MAC addresses)
	.BYTE	4	; PLEN, size of protocol addresses, 4 for IPv4, of course
	.DBYT	1	; OPER, 1 = request, 2 = reply



OUR_MAC_GIVEN:
;;	.BYTE	$00,$80,$10,$64,$65,$66
	.BYTE	$02,$47,$53,$65,$65,$65


; Messages


;.REPEAT 26,i
;.CHARMAP 65+i, 
;.ENDREP

screen_header:
	.BYTE $12,$18	; "RX"
	.BYTE ":000 "
	.BYTE $14,$18	; "TX"
	.BYTE ":000"
screen_header_length = .LOBYTE(* - screen_header)


.ZEROPAGE

checksum:		.RES 2
checksum_data_p:	.RES 2

.SEGMENT "INIT"
.SEGMENT "ONCE"
.BSS

packet_temp:		.RES 255

