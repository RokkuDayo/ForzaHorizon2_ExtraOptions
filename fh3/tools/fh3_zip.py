"""Write zip archives the way Forza Horizon 3 expects them.

Every central directory entry carries a 4-byte extra field 0x1123 holding
the absolute offset of the file's data, and the game reads data from there.
Local headers have no extra field. Archives from standard zip tools lack the
field, and the game crashes on them.

Usage: python fh3_zip.py repack SOURCE.zip OUTPUT.zip [replacement_dir]
  Files under replacement_dir (same relative paths) replace or extend SOURCE.
"""
import os
import struct
import sys
import zipfile
import zlib


def write_archive(output, entries):
    """entries: list of (name, bytes)."""
    central = []
    with open(output, 'wb') as out:
        for name, data in entries:
            raw_name = name.encode('utf-8')
            compressor = zlib.compressobj(9, zlib.DEFLATED, -15)
            packed = compressor.compress(data) + compressor.flush()
            crc = zlib.crc32(data) & 0xffffffff
            offset = out.tell()
            out.write(struct.pack('<IHHHHHIIIHH', 0x04034b50, 10, 0, 8, 0, 0, crc, len(packed),
                                  len(data), len(raw_name), 0))
            out.write(raw_name)
            out.write(packed)
            central.append((raw_name, crc, len(packed), len(data), offset))
        start = out.tell()
        for raw_name, crc, packed, size, offset in central:
            extra = struct.pack('<HHI', 0x1123, 4, offset + 30 + len(raw_name))
            out.write(struct.pack('<IHHHHHHIIIHHHHHII', 0x02014b50, 10, 10, 0, 8, 0, 0, crc, packed,
                                  size, len(raw_name), len(extra), 0, 0, 0, 0, offset))
            out.write(raw_name)
            out.write(extra)
        length = out.tell() - start
        out.write(struct.pack('<IHHHHIIH', 0x06054b50, 0, 0, len(central), len(central), length,
                              start, 0))


def repack(source, output, replacements=None):
    archive = zipfile.ZipFile(source)
    names = [info.filename for info in archive.infolist()]
    data = {name: archive.read(name) for name in names}
    if replacements:
        for root, _, files in os.walk(replacements):
            for file in files:
                path = os.path.join(root, file)
                name = os.path.relpath(path, replacements).replace(os.sep, '/')
                if name not in data:
                    names.append(name)
                data[name] = open(path, 'rb').read()
    write_archive(output, [(name, data[name]) for name in names])


if __name__ == '__main__':
    if len(sys.argv) >= 4 and sys.argv[1] == 'repack':
        repack(sys.argv[2], sys.argv[3], sys.argv[4] if len(sys.argv) > 4 else None)
    else:
        print(__doc__)
