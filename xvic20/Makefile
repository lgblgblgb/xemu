## Test-case for a very simple and inaccurate Commodore VIC-20 emulator using SDL2 library.
## Copyright (C)2016 LGB (Gábor Lénárt) <lgblgblgb@gmail.com>
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA

CC	= gcc
SDLCFG	= sdl2-config
CFLAGS	= -flto $(shell $(SDLCFG) --cflags) -I. -falign-functions=16 -falign-loops=16 -Wall -Ofast -ffast-math -pipe
LDFLAGS	= -flto $(shell $(SDLCFG) --libs)
PRG	= xvic20
SRCS	= main.c cpu65c02.c via65c22.c
FILES	= LICENSE README.md Makefile $(SRCS) *.h rom/README
OBJS	= $(SRCS:.c=.o)
DIST	= xvic20.tgz

all:	$(PRG)

%.o: %.c
	$(CC) -c $(CFLAGS) $< -o $@

$(PRG):	$(OBJS)
	$(CC) -o $(PRG) $(OBJS) $(LDFLAGS)

$(DIST): $(PRG)
	tar cfvz $(DIST) $(FILES)

dist:
	$(MAKE) $(DIST)

clean:
	rm -f $(PRG) $(OBJS) .depend $(DIST)

distclean:
	$(MAKE) clean
	rm -f rom/basic rom/kernal rom/chargen

strip: $(PRG)
	strip $(PRG)

dep:
	$(MAKE) .depend

.depend:
	$(CC) -MM $(CFLAGS) $(SRCS) > .depend

roms:
	wget -O rom/basic http://www.zimmers.net/anonftp/pub/cbm/firmware/computers/vic20/basic.901486-01.bin
	wget -O rom/kernal http://www.zimmers.net/anonftp/pub/cbm/firmware/computers/vic20/kernal.901486-07.bin
	wget -O rom/chargen http://www.zimmers.net/anonftp/pub/cbm/firmware/computers/vic20/characters.901460-03.bin

.PHONY: clean all strip dep dist roms distclean

ifneq ($(wildcard .depend),)
include .depend
endif

