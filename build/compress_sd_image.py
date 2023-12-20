#!/usr/bin/env python3

import sys, os



def write_offsets(fp, o):
    for val in o:
        b3, b2, b1, b0 = val >> 24, (val >> 16) & 0xFF, (val >> 8) & 0xFF, val & 0xFF
        if b3 >= 0x100:
            raise RuntimeError("Some offset cannot fit into 32 bits!")
        os.write(fp, bytearray([b0, b1, b2, b3]))



def run(argv):
    if len(argv) != 2:
        return "usage: infile outfile"
    fin = os.open(argv[0], os.O_RDONLY)
    size = os.fstat(fin).st_size
    if size & 0xFFFF:
        os.close(fin)
        return "Input file size is not multiple of 65536 bytes (64K)!"
    if size > 0x100000000:
        os.close(fin)
        return "Too large input file, cannot be larger than 4Gbytes in length!"
    size >>= 16
    if size < 16:
        os.close(fin)
        return "Abnormally short input file!"
    print("Size is " + str(size) + " 64K banks.")
    fout = os.open(argv[1], os.O_TRUNC|os.O_WRONLY|os.O_CREAT)
    os.write(fout, b"XemuBlockCompressedImage001\0")
    rewrite_pos = os.lseek(fout, 0, os.SEEK_CUR)
    write_offsets(fout, [0] * 5) # will be replaced at the end ...
    max_compressed_page_size = 0
    pagedirtab = []
    outblock = bytearray([0] * 0x30000)
    total_compressed_pages = 0
    super_compressed_pages = 0
    data_pos = os.lseek(fout, 0, os.SEEK_CUR)
    for blocknum in range(size):
        sys.stdout.write("Compressing page {}/{}: {}%\r".format(blocknum + 1, size, int(100 * (blocknum + 1)/ size)))
        sys.stdout.flush()
        block = os.read(fin, 0x10000)
        if len(block) != 0x10000:
            raise IOError("We must have 64Kbytes to be able to read at each steps!")
        freq = [0] * 256
        for i in range(0, len(block)):
            freq[block[i]] += 1
        fillfreq = max(freq)
        fillval = freq.index(fillfreq)
        outblock[0], o = fillval, 1
        if fillfreq != len(block): # the whole block is NOT filled with the very same byte!
            i = 0
            while i < len(block):
                j, v = i + 1, block[i]
                while j < len(block) and v == block[j]:
                    j += 1
                if v == fillval and j >= len(block):
                    break
                n = j - i
                if n < 1:
                    raise RuntimeError("'n' cannot be zero!")
                if n >= 0x100:
                    outblock[o + 0] = fillval
                    outblock[o + 1] = v
                    outblock[o + 2] = 0
                    outblock[o + 3] = n & 0xFF
                    outblock[o + 4] = n >> 8
                    o += 5
                elif n > 3 or v == fillval:
                    outblock[o + 0] = fillval
                    outblock[o + 1] = v
                    outblock[o + 2] = n
                    o += 3
                else:
                    for a in range(n):
                        outblock[o] = v
                        o += 1
                i = j
        else:
            total_compressed_pages += 1
        if o > max_compressed_page_size:
            max_compressed_page_size = o
        # -- finally --
        if o == 1 and fillval == 0:
            pagedirtab.append(0)
            super_compressed_pages += 1
        else:
            pagedirtab.append(o)
            if os.write(fout, outblock[:o]) != o:
                raise IOError("Cannot write out a compressed block")
    sys.stdout.write("\n")
    sys.stdout.flush()
    # Now, the pagedirtab ...
    o = 0
    i = 0
    pagedir_base = os.lseek(fout, 0, os.SEEK_CUR)
    while i < len(pagedirtab):
        j, v = i + 1, pagedirtab[i]
        if v < 0x80:
            while j < len(pagedirtab) and pagedirtab[j] == v:
                j += 1
            n = j - i
            if n > 1:
                v = n + ((v | 0x80) << 16)
        outblock[o + 0] = v & 0xFF
        outblock[o + 1] = (v >> 8) & 0xFF
        outblock[o + 2] = v >> 16
        o += 3
        i = j
    pagedir_size = o
    if os.write(fout, outblock[:pagedir_size]) != pagedir_size:
        raise IOError("Cannot write out pagedir")
    # Let's jump back to the header and write the right data now
    os.lseek(fout, rewrite_pos, os.SEEK_SET)
    write_offsets(fout, [
        size,
        pagedir_base,
        max_compressed_page_size,
        pagedir_size,
        data_pos
    ])
    total_size = os.lseek(fout, 0, os.SEEK_CUR)
    print("Result size = {}Mbytes, compression ratio = {}%".format(
        total_size >> 20,
        int(100 * total_size / (size << 16))
    ))
    os.close(fout)
    os.close(fin)
    print("DONE, max_compressed_page_size = {} bytes, total_compressed_pages = {} [super={}] ({}%), pagedir size = {} bytes".format(max_compressed_page_size, total_compressed_pages, super_compressed_pages, int(100 * total_compressed_pages / size), pagedir_size))
    return None



if __name__ == "__main__":
    r = run(sys.argv[1:])
    if r:
        sys.stderr.write("ERROR: " + r + "\n")
        sys.exit(1)
    else:
        sys.exit(0)
