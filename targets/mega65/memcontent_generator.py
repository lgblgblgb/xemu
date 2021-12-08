#!/usr/bin/env python3

# A work-in-progess MEGA65 (Commodore 65 clone origins) emulator
# Part of the Xemu project, please visit: https://github.com/lgblgblgb/xemu
# Copyright (C)2016-2021 LGB (Gábor Lénárt) <lgblgblgb@gmail.com>
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

import sys

PROTOTYPE = "Uint8"

# This must be incremented by ONE every time, when memcontent.c changes, or even
# if sdcontent.c is changed in a way to write new files, new content, or whatever
# to the SD-card as part of the "update system files" process.
MEMCONTENT_VERSION_ID = 1

FILE_DB = {
    "HICKUP.M65":           "hickup",
    "COLOURRAM.BIN":        "cramutils",
    "BANNER.M65":           "banner",
    "FREEZER.M65":          "freezer",
    "ONBOARD.M65":          "onboard",
    "mega65.rom":           "openrom",
    "megaflash-a200t.prg":  "megaflash",
    "AUDIOMIX.M65":         "audiomix",
    "C64THUMB.M65":         "c64thumb",
    "C65THUMB.M65":         "c65thumb",
    "ROMLOAD.M65":          "romload",
    "SPRITED.M65":          "sprited",
}
files_done = set()

HEADER = """/* !THIS IS A GENERATED FILE! DO NOT EDIT!
 * Instead, say 'make recreatememcontent' to re-generate this file
 * from binary data from the MEGA65 project. Please note, that MEGA65 is
 * an open source, GNU/GPL project, like Xemu. Thus, it's valid
 * to use binaries from it, as it's from the compiled version of MEGA65
 * which is available in source form at https://github.com/MEGA65/mega65-core
 * always, as per GNU/GPL. */

#include "xemu/emutools.h"

"""


def bin_dump(data):
    out, ct = "{\n\t", 0
    while True:
        out += "0x{:02X}".format(data[ct])
        ct += 1
        if ct == len(data):
            return out + "\n}"
        out += ","
        if ct % 32 == 0:
            out += "\n\t"



if __name__ == "__main__":
    if len(set(FILE_DB.values())) != len(FILE_DB):
        sys.stderr.write("Internal DB problem, redundancy in FILE_DB!\n")
        sys.exit(1)
    if len(sys.argv) < 4:
        sys.stderr.write("Bad usage.\n")
        sys.exit(1)
    c_file = sys.argv[1]
    h_file = sys.argv[2]
    in_files = sys.argv[3:]
    c_data = HEADER
    h_data = HEADER
    h_data += "#ifndef XEMU_MEGA65_{}_INCLUDED\n".format(h_file.upper().replace(".", "_"))
    h_data += "#define XEMU_MEGA65_{}_INCLUDED\n".format(h_file.upper().replace(".", "_"))
    h_data += """\n// This must be incremented by ONE every time, when memcontent.c changes, or even
// if sdcontent.c is changed in a way to write new files, new content, or whatever
// to the SD-card as part of the "update system files" process. Edit this in the python generator though, not in this file!
#define MEMCONTENT_VERSION_ID {}\n""".format(MEMCONTENT_VERSION_ID)
    c_data += "#include \"memcontent.h\"\n\n"
    c_data += PROTOTYPE + " meminitdata_chrwom[MEMINITDATA_CHRWOM_SIZE] = {\n#include \"memcontent_chrwom.cdata\"\n};\n\n"
    h_data += "\n// Extra stuff ...\n"
    h_data += "#define MEMINITDATA_CHRWOM_SIZE 4096\n"
    h_data += "extern " + PROTOTYPE + " meminitdata_chrwom[MEMINITDATA_CHRWOM_SIZE];\n"
    for fn in in_files:
        fn_base = fn.split("/")[-1]
        if fn_base not in FILE_DB:
            sys.stderr.write("Unknown file encountered: {}\n".format(fn))
            sys.exit(1)
        fn_id = FILE_DB[fn_base]
        if fn_id in files_done:
            sys.stderr.write("ERROR: file {} is used more than once by id \"{}\"!\n".format(fn, fn_id))
            sys.exit(1)
        files_done.add(fn_id)
        with open(fn, "rb") as data:
            data = data.read()
        print("Using file {} ({} bytes) as {} ...".format(fn, len(data), fn_id))
        if len(data) < 1:
            sys.stderr.write("ERROR: file {} is zero byte long!\n".format(fn))
            sys.exit(1)
        h_data += "\n// Generated as \"{}\" from file {} ({} bytes)\n".format(fn_id, fn, len(data))
        h_data += "#define MEMINITDATA_{}_SIZE {}\n".format(fn_id.upper(), len(data))
        h_data += "extern {} meminitdata_{}[MEMINITDATA_{}_SIZE];\n".format(PROTOTYPE, fn_id, fn_id.upper())
        c_data += "\n// Generated as \"{}\" from file {} ({} bytes)\n".format(fn_id, fn, len(data))
        c_data += "{} meminitdata_{}[MEMINITDATA_{}_SIZE] = {};\n".format(PROTOTYPE, fn_id, fn_id.upper(), bin_dump(data))
    h_data += "\n#endif\n"
    # OK, now write out the result ...
    with open(c_file, "wt") as f: f.write(c_data)
    with open(h_file, "wt") as f: f.write(h_data)
    for k, v in FILE_DB.items():
        if v not in files_done:
            print("Warning: entity {} was not specified (via filename {})\n".format(v, k))
    sys.exit(0)
