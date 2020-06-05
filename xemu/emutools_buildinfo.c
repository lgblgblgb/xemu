/* Xemu - Somewhat lame emulation (running on Linux/Unix/Windows/OSX, utilizing
   SDL2) of some 8 bit machines, including the Commodore LCD and Commodore 65
   and MEGA65 as well.
   Copyright (C)2016-2020 LGB (Gábor Lénárt) <lgblgblgb@gmail.com>

   The goal of emutools.c is to provide a relative simple solution
   for relative simple emulators using SDL2.

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

#include "xemu/emutools_basicdefs.h"

const char *XEMU_BUILDINFO_CC  = CC_TYPE " " __VERSION__ " " ARCH_BITS_AS_TEXT ENDIAN_NAME;

const char emulators_disclaimer[] =
	"LICENSE: Copyright (C)" COPYRIGHT_YEARS " Gábor Lénárt (aka LGB) lgb@lgb.hu http://lgb.hu/" NL
	"LICENSE: This software is a GNU/GPL version 2 (or later) software." NL
	"LICENSE: <http://gnu.org/licenses/gpl.html>" NL
	"LICENSE: This is free software; you are free to change and redistribute it." NL
	"LICENSE: There is NO WARRANTY, to the extent permitted by law." NL
;

void xemu_dump_version ( FILE *fp, const char *slogan )
{
	if (!fp)
		return;
	if (slogan)
		fprintf(fp, "**** %s ****" NL, slogan);
	fprintf(fp, "This software is part of the Xemu project: https://github.com/lgblgblgb/xemu" NL);
	fprintf(fp, "CREATED: %s at %s" NL "CREATED: %s for %s" NL, XEMU_BUILDINFO_ON, XEMU_BUILDINFO_AT, XEMU_BUILDINFO_CC, XEMU_ARCH_NAME);
	fprintf(fp, "VERSION: %s %s" NL, XEMU_BUILDINFO_GIT, XEMU_BUILDINFO_CDATE);
	fprintf(fp, "EMULATE: %s (%s): %s" NL, TARGET_DESC, TARGET_NAME, XEMU_BUILDINFO_TARGET);
	fprintf(fp, "%s" NL, emulators_disclaimer);
}
