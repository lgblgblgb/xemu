#!/usr/bin/env python
# -*- coding: UTF-8 -*-

# Copyright (C)2015,2016,2018 LGB (Gábor Lénárt) <lgblgblgb@gmail.com>

from sys import argv, stderr, stdout, exit
from textwrap import wrap

if __name__ == "__main__":
    if len(argv) != 3:
        stderr.write("Bad usage.\n")
        exit(1)
    try:
        with open(argv[1], "rb") as bb:
            bb = bytearray(bb.read())
    except IOError as a:
        stderr.write("File open/read error: " + str(a) + "\n")
        exit(1)
    bb = "\n".join(wrap(", ".join(map("0x{:02X}".format, bb)))) + "\n"
    try:
        if argv[2] == "-":
            stdout.write(bb)
        else:
            with open(argv[2], "w") as f:
                f.write(bb)
    except IOError as a:
        stderr.write("File create/write error: " + str(a) + "\n")
        exit(1)
    exit(0)
