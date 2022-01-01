#!/usr/bin/env python3

# A work-in-progess MEGA65 (Commodore 65 clone origins) emulator
# Part of the Xemu project, please visit: https://github.com/lgblgblgb/xemu
# Copyright (C)2016-2022 LGB (Gábor Lénárt) <lgblgblgb@gmail.com>
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

PROTOTYPE = "const Uint8"

# This must be incremented by ONE every time, when memcontent.c changes, or even
# if sdcontent.c is changed in a way to write new files, new content, or whatever
# to the SD-card as part of the "update system files" process.
MEMCONTENT_VERSION_ID = 1

FILE_DB = {
    # -------------------------------------------------------------------- #
    # mega65-core-base-name | file-id       |SD-image-name(None=not-on-SD) #
    #--------------------------------------------------------------------- #
    "HICKUP.M65":           ("hickup",      None ),
    "COLOURRAM.BIN":        ("cramutils",   None ),
    "BANNER.M65":           ("banner",      "BANNER.M65" ),
    "FREEZER.M65":          ("freezer",     "FREEZER.M65" ),
    "ONBOARD.M65":          ("onboard",     "ONBOARD.M65" ),    # Do we really need this on SD-card?
    "mega65.rom":           ("initrom",     None ),
    #"megaflash-a200t.prg": ("megaflash",   None ),
    "AUDIOMIX.M65":         ("audiomix",    "AUDIOMIX.M65" ),
    "C64THUMB.M65":         ("c64thumb",    "C64THUMB.M65" ),
    "C65THUMB.M65":         ("c65thumb",    "C65THUMB.M65" ),
    "ROMLOAD.M65":          ("romload",     "ROMLOAD.M65" ),    # Do we really need this on SD-card?
    "SPRITED.M65":          ("sprited",     "SPRITED.M65" ),
    "charrom.bin":          ("chrwom",      None ),
}

HEADER = """/* !THIS IS A GENERATED FILE! DO NOT EDIT!
 * Instead, say 'make recreatememcontent' to re-generate this file
 * from binary data from the MEGA65 project. Please note, that MEGA65 is
 * an open source, GNU/GPL project, like Xemu. Thus, it's valid
 * to use binaries from it, as it's from the compiled version of MEGA65
 * which is available in source form at https://github.com/MEGA65/mega65-core
 * always, as per GNU/GPL. */

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
    files_done = set()
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
#define MEMCONTENT_VERSION_ID {}\n\n""".format(MEMCONTENT_VERSION_ID)
    c_data += "#include \"xemu/emutools_basicdefs.h\"\n"
    c_data += "#include \"memcontent.h\"\n\n"
    h_data += "// Special structure array for system files update on the SD-image\n"
    c_data += "// Special structure array for system files update on the SD-image\n"
    on_sd = sorted([k for k, v in FILE_DB.items() if v[1] is not None])
    h_data += "struct meminitdata_sdfiles_st { const Uint8 *p; const char *fn; const int size; };\n"
    h_data += "#define MEMINITDATA_SDFILES_ITEMS {}\n".format(len(on_sd))
    h_data += "extern const struct meminitdata_sdfiles_st meminitdata_sdfiles_db[MEMINITDATA_SDFILES_ITEMS];\n"
    c_data += "const struct meminitdata_sdfiles_st meminitdata_sdfiles_db[MEMINITDATA_SDFILES_ITEMS] = {\n"
    print("Adding files also as SD-content: {}".format(" ".join(on_sd)))
    for a in on_sd:
        c_data += "\t{"
        c_data += " meminitdata_{}, \"{}\", MEMINITDATA_{}_SIZE ".format(FILE_DB[a][0], FILE_DB[a][1].upper(), FILE_DB[a][0].upper())
        c_data += "},\n"
    c_data += "};\n\n"
    for fn in in_files:
        fn_base = fn.split("/")[-1]
        if fn_base not in FILE_DB:
            sys.stderr.write("Unknown file encountered: {}\n".format(fn))
            sys.exit(1)
        fn_id = FILE_DB[fn_base][0]
        if fn_id in files_done:
            sys.stderr.write("ERROR: file {} is used more than once by id \"{}\"!\n".format(fn, fn_id))
            sys.exit(1)
        files_done.add(fn_id)
        with open(fn, "rb") as data:
            data = data.read()
        print("Using file {} (${:X} bytes) as {} ...".format(fn, len(data), fn_id))
        if len(data) < 1:
            sys.stderr.write("ERROR: file {} is zero byte long!\n".format(fn))
            sys.exit(1)
        h_data += "\n// Generated as \"{}\" from file {} (${:X} bytes)\n".format(fn_id, fn, len(data))
        h_data += "#define MEMINITDATA_{}_SIZE 0x{:X}\n".format(fn_id.upper(), len(data))
        h_data += "extern {} meminitdata_{}[MEMINITDATA_{}_SIZE];\n".format(PROTOTYPE, fn_id, fn_id.upper())
        c_data += "\n// Generated as \"{}\" from file {} (${:X} bytes)\n".format(fn_id, fn, len(data))
        c_data += "{} meminitdata_{}[MEMINITDATA_{}_SIZE] = {};\n".format(PROTOTYPE, fn_id, fn_id.upper(), bin_dump(data))
    h_data += "\n#endif\n"
    # OK, now write out the result ...
    with open(c_file, "wt") as f: f.write(c_data)
    with open(h_file, "wt") as f: f.write(h_data)
    for k, v in FILE_DB.items():
        if v[0] not in files_done:
            print("Warning: entity {} was not specified (via filename {})\n".format(v[0], k))
    sys.exit(0)
