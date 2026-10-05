#!/usr/bin/env python3
"""Validate final AppImage update information and the matching zsync checksum."""
import hashlib
import pathlib
import struct
import sys

UPDATE_INFORMATION = b"gh-releases-zsync|victorfu|snap-tray|latest|SnapTray-*-x86_64.AppImage.zsync"


def verify(path):
    path = pathlib.Path(path)
    data = path.read_bytes()
    if data[:4] != b"\x7fELF" or data[8:11] != b"AI\x02" or data[4:6] != b"\x02\x01":
        raise ValueError("Expected a little-endian ELF64 type 2 AppImage")
    offset = struct.unpack_from("<Q", data, 40)[0]
    entry_size, count, strings_index = struct.unpack_from("<HHH", data, 58)
    if entry_size < 64 or not 0 < strings_index < count:
        raise ValueError("Invalid ELF section table")
    sections = [struct.unpack_from("<IIQQQQIIQQ", data, offset + i * entry_size) for i in range(count)]
    strings = sections[strings_index]
    names = data[strings[4]:strings[4] + strings[5]]
    updates = [data[s[4]:s[4] + s[5]].rstrip(b"\0") for s in sections
               if names[s[0]:].split(b"\0", 1)[0] == b".upd_info"]
    if updates != [UPDATE_INFORMATION]:
        raise ValueError("Missing or incorrect AppImage update information")
    headers = pathlib.Path(str(path) + ".zsync").read_bytes().split(b"\n\n", 1)[0]
    fields = dict(line.split(b": ", 1) for line in headers.splitlines() if b": " in line)
    if int(fields.get(b"Length", b"-1")) != len(data):
        raise ValueError("zsync length does not match final AppImage")
    if fields.get(b"SHA-1", b"").decode().lower() != hashlib.sha1(data).hexdigest():
        raise ValueError("zsync checksum does not match final AppImage")
    if fields.get(b"Filename", b"").decode() != path.name:
        raise ValueError("zsync filename does not match AppImage")
    url = fields.get(b"URL", b"").decode()
    if url != path.name and not url.endswith("/" + path.name):
        raise ValueError("zsync URL does not identify the AppImage")


if __name__ == "__main__":
    try:
        verify(sys.argv[1])
    except (ValueError, OSError, IndexError, struct.error) as error:
        sys.exit(f"Invalid update artifacts: {error}")
