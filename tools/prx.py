"""THUG2 PRE v3 archives (.prx): read, and rebuild with entries replaced or added.
Usage: prx.py add <src.prx> <dst.prx> <entry name>=<file> ... [--qdir <line>]"""
import struct
import sys
import zlib

RING, MAXM = 4096, 18


def crc(text):  # Neversoft checksum: CRC-32 of the lower-case text, no final xor
    return (zlib.crc32(text.lower().encode('latin1')) ^ 0xFFFFFFFF) & 0xFFFFFFFF


def decode(data, size_out):  # LZSS, as Pre::Decode (thug2-decomp src/game/file/pre.cpp)
    ring = bytearray(b' ' * RING); r = RING - MAXM; out = bytearray(); i = 0; flags = 0
    while i < len(data):
        flags >>= 1
        if not flags & 0x100:
            flags = data[i] | 0xFF00; i += 1
            if i >= len(data): break
        if flags & 1:
            c = data[i]; i += 1; out.append(c); ring[r] = c; r = (r + 1) & (RING - 1)
        else:
            if i + 1 >= len(data): break
            pos = data[i] | ((data[i + 1] & 0xF0) << 4); last = (data[i + 1] & 0x0F) + 2; i += 2
            for k in range(last + 1):
                c = ring[(pos + k) & (RING - 1)]; out.append(c); ring[r] = c; r = (r + 1) & (RING - 1)
    return bytes(out[:size_out])


def raw_entries(b):
    _, _, magic, n = struct.unpack_from('<IHHI', b, 0)
    assert magic == 0xABCD
    off = 12
    for _ in range(n):
        size, csize, nlen, ncrc = struct.unpack_from('<IIII', b, off)
        namefield = b[off + 16:off + 16 + nlen]
        dlen = csize or size
        start = off + 16 + nlen
        off = start + ((dlen + 3) & ~3)
        yield dict(size=size, csize=csize, crc=ncrc, namefield=namefield, raw=b[start:start + dlen],
                   pad=b[start + dlen:off], name=namefield.split(b'\0')[0].decode('latin1'))


def read(b, name):
    for e in raw_entries(b):
        if e['name'].lower() == name.lower():
            return decode(e['raw'], e['size']) if e['csize'] else e['raw']
    raise KeyError(name)


def build(src, replace=None, add=None):
    """replace: {name: bytes} for existing entries; add: [(name, bytes)] appended. Stored uncompressed."""
    replace = {k.lower(): v for k, v in (replace or {}).items()}
    _, ver, magic, _ = struct.unpack_from('<IHHI', src, 0)
    body = bytearray(); count = 0; done = set()

    def put(size, csize, ncrc, namefield, raw, pad):
        nonlocal count
        body.extend(struct.pack('<IIII', size, csize, len(namefield), ncrc) + namefield + raw + pad); count += 1

    for e in raw_entries(src):
        key = e['name'].lower()
        if key in replace:
            data = replace[key]; done.add(key)
            put(len(data), 0, e['crc'], e['namefield'], data, b'\0' * (((len(data) + 3) & ~3) - len(data)))
        else:
            put(e['size'], e['csize'], e['crc'], e['namefield'], e['raw'], e['pad'])
    assert done == set(replace), set(replace) - done
    for name, data in add or []:
        put(len(data), 0, crc(name), name.encode('latin1') + b'\0', data, b'\0' * (((len(data) + 3) & ~3) - len(data)))
    out = struct.pack('<IHHI', 12 + len(body), ver, magic, count) + body
    return bytes(out)


if __name__ == '__main__':
    args = sys.argv[1:]
    assert args[0] == 'add'
    src = open(args[1], 'rb').read(); dst = args[2]; adds = []; qdir_lines = []; i = 3
    while i < len(args):
        if args[i] == '--qdir':
            qdir_lines.append(args[i + 1]); i += 2; continue
        name, path = args[i].split('=', 1); adds.append((name, open(path, 'rb').read())); i += 1
    replace = {}
    if qdir_lines:
        q = read(src, 'scripts\\qdir.txt')
        replace['scripts\\qdir.txt'] = q + b''.join(l.encode('latin1') + b'\r\n' for l in qdir_lines)
    assert build(src) == src, 'no-op rebuild differs from the source'
    open(dst, 'wb').write(build(src, replace, adds))
    print('wrote', dst, 'with', [n for n, _ in adds], 'qdir +', len(qdir_lines))
