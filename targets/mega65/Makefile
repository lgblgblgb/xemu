## A work-in-progess MEGA65 (Commodore-65 clone origins) emulator
## Part of the Xemu project, please visit: https://github.com/lgblgblgb/xemu
## Copyright (C)2016-2025 LGB (Gábor Lénárt) <lgblgblgb@gmail.com>
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

TARGET		= mega65
PRG_TARGET	= xmega65
EMU_DESCRIPTION	= MEGA65

SRCS_TARGET_xmega65	= configdb.c mega65.c sdcard.c uart_monitor.c hypervisor.c memory_mapper.c io_mapper.c vic4.c vic4_palette.c ethernet65.c input_devices.c memcontent.c ui.c fat32.c sdcontent.c audio65.c inject.c dma65.c rom.c hdos.c matrix_mode.c cart.c
SRCS_COMMON_xmega65	= emutools.c cpu65.c cia6526.c emutools_hid.c sid.c f011_core.c c64_kbd_mapping.c emutools_config.c emutools_files.c emutools_umon.c emutools_socketapi.c ethertap.c d81access.c emutools_gui.c basic_text.c opl3.c lodepng.c compressed_disk_image.c cpu65_disasm.c emutools_osk.c
CFLAGS_TARGET_xmega65	= $(SDL2_CFLAGS) $(MATH_CFLAGS) $(SOCKET_CFLAGS) $(XEMUGUI_CFLAGS)
LDFLAGS_TARGET_xmega65	= $(SDL2_LIBS) $(MATH_LIBS) $(SOCKET_LIBS) $(XEMUGUI_LIBS)
LDFLAGS_TARGET_xmega65_ON_html = -s STACK_SIZE=655360 --preload-file=$$HOME/.local/share/xemu-lgb/mega65/mega65.img.compressed3@/files/mega65.img --preload-file=$$HOME/mega65/megapoly.d81@/files/files/hdos/mega65.d81

MEGA65_CORE	= ../../../mega65-core-0.97-for-xemu
MEGA65_REPO	= https://github.com/MEGA65/mega65-core.git
MEGA65_COMMITID	= aaf4542d02679271b5fb84ad933cc39bbe091c3f
MEGA65_PATCH	= ../../build/mega65-core-0.97-aaf4542d02679271b5fb84ad933cc39bbe091c3f-patch.diff
MEGA65_FILES	= bin/charrom.bin bin/HICKUP.M65 bin/COLOURRAM.BIN sdcard-files/BANNER.M65 sdcard-files/ONBOARD.M65 src/open-roms/bin/mega65.rom
MEGA65_FILES	+= sdcard-files/FREEZER.M65 sdcard-files/MAKEDISK.M65
MEGA65_FILES	+= sdcard-files/AUDIOMIX.M65 sdcard-files/C64THUMB.M65 sdcard-files/C65THUMB.M65 sdcard-files/ROMLOAD.M65 sdcard-files/SPRITED.M65
#MEGA65_FILES	+= sdcard-files/M65THUMB.M65 sdcard-files/MONITOR.M65 sdcard-files/ETHLOAD.M65

MEGA65_FP_FILES	= $(addprefix $(MEGA65_CORE)/,$(MEGA65_FILES))
MEGA65_LOCALDIR	= .sys-files-mega65-core
MATRIX_TAB_VHDL = $(MEGA65_CORE)/src/vhdl/matrix_to_ascii.vhdl

include ../../build/Makefile.common

# mega65-core has very tricky build system, it's very hard to be sure, that everything
# is up-to-date, this is the reason for this mess
recreatememcontent:
	test -f "$(MEGA65_CORE)/xemu_checked_out" || $(MAKE) mega65corefetch
	test -f "$(MEGA65_CORE)/xemu_patched" || $(MAKE) mega65patch
	@echo "*** Compiling needed files from mega65-core at $(MEGA65_CORE) ***"
	cd $(MEGA65_CORE) ; $(MAKE) clean
	cd $(MEGA65_CORE) ; for dir in src/mega65-freezemenu src/mega65-fdisk src/open-roms ; do $(MAKE) -C $$dir clean || exit 1 ; done
	find $(MEGA65_CORE) -name "*.o" | xargs -n 100 rm -f
	find $(MEGA65_CORE) -name "*.M65" | xargs -n 100 rm -f
	mkdir -p $(MEGA65_CORE)/sdcard-files
	rm -f `echo $(MEGA65_FP_FILES) | fgrep -v mega65.rom`
	rm -f $(MEGA65_CORE)/sdcard-files/* $(MEGA65_CORE)/src/mega65-fdisk/*.prg $(MEGA65_CORE)/src/utilities/*.prg
	rm -f $(MEGA65_CORE)/src/hyppo/HICKUP.rep $(MEGA65_CORE)/src/hyppo/HICKUP.sym
	cd $(MEGA65_CORE) ; $(MAKE) cc65/bin/cc65
	cd $(MEGA65_CORE) ; $(MAKE) -C src/mega65-libc
	cd $(MEGA65_CORE) ; $(MAKE) freezer_files
	cd $(MEGA65_CORE) ; $(MAKE) -C src/mega65-fdisk m65fdisk.prg
	cd $(MEGA65_CORE) ; $(MAKE) -C src/mega65-freezemenu
	cd $(MEGA65_CORE) ; $(MAKE) $(MEGA65_FILES)
	mkdir -p $(MEGA65_LOCALDIR)
	rm -f $(MEGA65_LOCALDIR)/*
	cp $(MEGA65_FP_FILES) $(MEGA65_LOCALDIR)/
	cp $(MEGA65_CORE)/src/hyppo/HICKUP.rep $(MEGA65_CORE)/src/hyppo/HICKUP.sym $(MEGA65_LOCALDIR)/
	if [ -d ../../../xemu-gh-pages ]; then mkdir -p ../../../xemu-gh-pages/files/xemu-mega65-sysfiles || exit 1 ; for a in $(MEGA65_LOCALDIR)/* ; do echo "Submitting $$a to gh-pages ..." ; cp $$a ../../../xemu-gh-pages/files/xemu-mega65-sysfiles/`git branch | awk '$$1 == "*" { print $$2 }'`-`basename $$a` || exit 1 ; done ; else echo "*** WARNING cannot submit file to gh-pages @ ../../../xemu-gh-pages/xemu-mega65-sysfiles/ ***" ; fi
	@echo "*** Generating C files from mega65-core binary results ***"
	python3 $(TOPDIR)/build/m65-memcontent-generator.py memcontent.c memcontent.h $(MEGA65_FP_FILES)

mega65corefetch:
	@echo "*** Checking out $(MEGA65_REPO) to $(MEGA65_CORE) at commit-id $(MEGA65_COMMITID) ***"
	rm -fr "$(MEGA65_CORE)"
	git -c init.defaultBranch=whocares init "$(MEGA65_CORE)"
	git -C "$(MEGA65_CORE)" remote add origin "$(MEGA65_REPO)"
	git -C "$(MEGA65_CORE)" fetch --depth 1 origin "$(MEGA65_COMMITID)"
	git -C "$(MEGA65_CORE)" -c advice.detachedHead=false checkout "$(MEGA65_COMMITID)"
	git -C "$(MEGA65_CORE)" submodule update --init --recursive --depth 1
	echo "$(MEGA65_REPO) at commit-id $(MEGA65_COMMITID)" > "$(MEGA65_CORE)/xemu_checked_out.new"
	date >> "$(MEGA65_CORE)/xemu_checked_out.new"
	du -sh "$(MEGA65_CORE)" >> "$(MEGA65_CORE)/xemu_checked_out.new"
	cat "$(MEGA65_CORE)/xemu_checked_out.new" | sed 's/^/| /'
	mv "$(MEGA65_CORE)/xemu_checked_out.new" "$(MEGA65_CORE)/xemu_checked_out"

mega65patch:
	@echo "*** Patching mega65-core for Xemu at $(MEGA65_CORE) ***"
	date > "$(MEGA65_CORE)/xemu_patched"

showmatrix:
	bash $(TOPDIR)/build/m65-import-matrix2ascii.sh < $(MATRIX_TAB_VHDL)
	bash $(TOPDIR)/build/m65-import-matrix2petscii.sh < $(MATRIX_TAB_VHDL)
