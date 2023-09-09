#!/usr/bin/env python3

import sys, os


def write24bit(fd, val):
    os.write(fd, bytearray([(val & 0xFF0000) >> 16, (val & 0xFF00) >> 8, val & 0xFF]))


def run(argv):
    if len(argv) != 2:
        return "usage: infile outfile"
    fin = os.open(argv[0], os.O_RDONLY)
    size = os.fstat(fin).st_size
    if size & 511:
        os.close(fin)
        return "Input file size is not multiple of 512 bytes!"
    if size > 0x100000000:
        os.close(fin)
        return "Too large input file, max supported is 2Gbyte in length!"
    size >>= 9
    print("Size is " + str(size) + " blocks.")
    fout = os.open(argv[1], os.O_TRUNC|os.O_WRONLY|os.O_CREAT)
    os.write(fout, b"XemuBlockCompressedImage000\0")
    write24bit(fout, 3) # size of header, currently only one 3 bytes length entity, see the next line ...
    write24bit(fout, size)
    tabpos = os.lseek(fout, 0, os.SEEK_CUR)
    print("Pos-table offset = " + str(tabpos))
    print("Data offset = " + str(size * 3 + tabpos))
    os.lseek(fout, size * 3 + tabpos, os.SEEK_SET)
    datpos = 0
    bmap = [-1] * size
    sbmap = [-1] * 256
    print("Reading all the file ...")
    ncompressed = 0;
    for blocknum in range(size):
        block = os.read(fin, 512)
        if len(block) != 512:
            raise IOError("We must have 512 bytes to be able to read at all steps!")
        fillval = block[0]
        for i in range(1, 512):
            if block[i] != fillval:
                fillval = False
                break
        if fillval is False:
            bmap[blocknum] = datpos
            if os.write(fout, block) != 512:
                raise IOError("We must have 512 bytes to be able to written at once!")
            datpos += 1
        else:
            if sbmap[fillval] > -1:
                bmap[blocknum] = sbmap[fillval]
            else:
                sbmap[fillval] = datpos | 0x800000
                bmap[blocknum] = sbmap[fillval]
                if os.write(fout, block) != 512:
                    raise IOError("We must have 512 bytes to be able to written at once!")
                datpos += 1
            ncompressed += 1
        #print(block)
        #print(fillval)
        #print(len(block))
    os.lseek(fout, tabpos, os.SEEK_SET)
    for blocknum in range(size):
        if bmap[blocknum] < 0:
            raise RuntimeError("block with -1 ...")
        write24bit(fout, bmap[blocknum])
    os.close(fout)
    os.close(fin)
    print("Compression ratio will be: " + str(100 * (size - ncompressed) / ncompressed) + "%")
    return None



if __name__ == "__main__":
    r = run(sys.argv[1:])
    if r:
        sys.stderr.write("ERROR: " + r + "\n")
        sys.exit(1)
    else:
        sys.exit(0)
