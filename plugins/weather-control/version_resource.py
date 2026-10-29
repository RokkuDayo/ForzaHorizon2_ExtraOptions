"""Deterministic VERSIONINFO for the standalone weather control plugin."""
import struct

VERSION = "1.0.4"

def version_resource():
    version = VERSION
    numbers = [int(part) for part in version.split(".")]
    numbers += [0] * (4 - len(numbers))
    def padded(data):
        return data + bytes(-len(data) % 4)

    def wide(text):
        return (text + "\0").encode("utf-16le")

    def block(key, value=b"", children=b"", text=False):
        header = padded(struct.pack("<HHH", 0, len(value) // 2 if text else len(value), int(text)) + wide(key))
        data = header + value
        if children:
            data = padded(data) + children
        return struct.pack("<H", len(data)) + data[2:]

    strings = {"FileDescription": "Extra Options - Weather Control",
               "FileVersion": version, "InternalName": "EO_WeatherControl",
               "OriginalFilename": "EO_WeatherControl.dll",
               "ProductName": "Forza Horizon 2 Weather Control",
               "ProductVersion": version, "LegalCopyright": "Arthur (RokkuDayo)"}
    entries = b"".join(padded(block(key, wide(value), text=True)) for key, value in strings.items())
    string_info = block("StringFileInfo", children=block("040904b0", children=entries))
    variable_info = block("VarFileInfo", children=block("Translation", struct.pack("<HH", 0x409, 1200)))
    high, low = numbers[0] << 16 | numbers[1], numbers[2] << 16 | numbers[3]
    fixed = struct.pack("<13I", 0xfeef04bd, 0x10000, high, low, high, low,
                        0x3f, 0, 0x40004, 2, 0, 0, 0)
    data = block("VS_VERSION_INFO", fixed, padded(string_info) + variable_info)
    null_header = struct.pack("<IIHHHHIHHII", 0, 32, 0xffff, 0, 0xffff, 0, 0, 0, 0, 0, 0)
    header = struct.pack("<IIHHHHIHHII", len(data), 32, 0xffff, 16, 0xffff, 1, 0, 0x30, 0x409, 0, 0)
    return null_header + header + padded(data)
