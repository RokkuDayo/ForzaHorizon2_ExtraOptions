"""Offline check of [Graphics] SmoothMenus with the --test build.

Usage: py tools/test-smooth-menus.py TEST_DLL
Works only in a temporary folder and in memory; never starts the game.
"""
import ctypes
import pathlib
import struct
import sys
import tempfile
from ctypes import wintypes

FIELD = 41  # ModOptions.smooth_menus (int index)
IMAGE_SIZE = 0x3a18000
VTABLE, NAME, NAME_TEXT, DEFAULT, SERIALIZE = 0x2eb98d0, 0x13073b0, 0x22372a8, 0x130bb50, 0x130ab00
NAME_CODE = bytes.fromhex('488d05f1fef200c3')
DEFAULT_CODE = bytes.fromhex('c741083c000000c3')
SERIALIZE_CODE = bytes.fromhex('40534883ec208b41084c8bca488bd989442430488b024c8d05837cb400'
                               '488d542430498bc9ff90880100008b108953084883c4205bc3')

dll = ctypes.WinDLL(str(pathlib.Path(sys.argv[1]).resolve()))
dll.Fh2TweaksStatus.restype = ctypes.c_char_p
dll.Fh2TestReadOptions.argtypes = [wintypes.LPCWSTR, ctypes.c_void_p]
dll.Fh2TestSmoothMenusInstall.argtypes = [ctypes.c_void_p]
dll.Fh2TestSmoothMenusHook.restype = ctypes.c_void_p
kernel32 = ctypes.WinDLL('kernel32', use_last_error=True)
kernel32.VirtualAlloc.restype = ctypes.c_void_p
kernel32.VirtualAlloc.argtypes = [ctypes.c_void_p, ctypes.c_size_t, wintypes.DWORD, wintypes.DWORD]
kernel32.VirtualFree.argtypes = [ctypes.c_void_p, ctypes.c_size_t, wintypes.DWORD]
temporary = tempfile.TemporaryDirectory(prefix='smooth-menus-')
work = pathlib.Path(temporary.name)
failed = 0


def check(name, ok, detail=''):
    global failed
    failed += not ok
    if not ok:
        print('FAIL:', name, detail)


def read(text):
    (work / 'FH2_ExtraOptions.ini').write_text(text, encoding='utf-8')
    options = (ctypes.c_int * 256)()
    return dll.Fh2TestReadOptions(str(work), options), options[FIELD]


for name, text, expected in [('default', '[Global]\nEnabled=1\n', (1, 0)),
                             ('on', '[Graphics]\nSmoothMenus=1\n', (1, 1)),
                             ('off', '[Graphics]\nSmoothMenus=0\n', (1, 0)),
                             ('bad value', '[Graphics]\nSmoothMenus=60\n', (0, None))]:
    result, value = read(text)
    check(name, result == expected[0] and (expected[1] is None or value == expected[1]),
          f'{result} {value} {dll.Fh2TweaksStatus().decode()}')
temporary.cleanup()

# The rule: below 60 becomes 60, 60 and above stay.
for given, expected in [(0, 60), (30, 60), (59, 60), (60, 60), (120, 120)]:
    check(f'rule {given}', dll.Fh2TestSmoothFrameRate(given) == expected)

# Install on an in-memory image that holds only the guarded bytes; the int
# reader is the game's own code copied there, so the wrapped slot runs for real.
image = kernel32.VirtualAlloc(None, IMAGE_SIZE, 0x3000, 0x40)  # commit+reserve, RWX
if not image:
    sys.exit('VirtualAlloc failed')


def put(rva, data):
    ctypes.memmove(image + rva, data, len(data))


def slot(index):
    return ctypes.c_uint64.from_address(image + VTABLE + 8 * index).value


put(VTABLE, struct.pack('<7Q', 0, 0, image + NAME, 0, image + DEFAULT, 0, image + SERIALIZE))
put(NAME, NAME_CODE)
put(NAME_TEXT, b'FrameRate\0')
put(DEFAULT, DEFAULT_CODE)
put(SERIALIZE, SERIALIZE_CODE)
put(NAME_TEXT + 5, b'X')
check('mismatch refused', dll.Fh2TestSmoothMenusInstall(image) == 0 and slot(6) == image + SERIALIZE)
put(NAME_TEXT + 5, b'R')
check('install', dll.Fh2TestSmoothMenusInstall(image) == 1)
check('slot 6 wrapped', slot(6) == dll.Fh2TestSmoothMenusHook())
check('other slots kept', slot(2) == image + NAME and slot(4) == image + DEFAULT)

# A reader whose slot 0x188 returns a pointer to the value "in the XML".
xml_value = ctypes.c_int32()
ReadInt = ctypes.CFUNCTYPE(ctypes.c_void_p, ctypes.c_void_p, ctypes.c_void_p, ctypes.c_void_p)
read_int = ReadInt(lambda reader, default, name: ctypes.addressof(xml_value))
reader_vtable = (ctypes.c_void_p * 0x40)()
reader_vtable[0x188 // 8] = ctypes.cast(read_int, ctypes.c_void_p).value
reader = ctypes.c_void_p(ctypes.addressof(reader_vtable))
serialize = ctypes.CFUNCTYPE(None, ctypes.c_void_p, ctypes.c_void_p)(slot(6))
for given, expected in [(30, 60), (60, 60), (120, 120)]:
    control = (ctypes.c_uint8 * 16)()
    struct.pack_into('<Q', control, 0, image + VTABLE)
    xml_value.value = given
    serialize(ctypes.addressof(control), ctypes.addressof(reader))
    value = struct.unpack_from('<i', control, 8)[0]
    check(f'FrameRate {given}', value == expected, value)
kernel32.VirtualFree(ctypes.c_void_p(image), 0, 0x8000)

if failed:
    sys.exit(1)
print('PASS: [Graphics] SmoothMenus is off by default, 1 turns it on, other values are refused; '
      'FrameRate below 60 is read as 60, 60 and above unchanged; a mismatched class is left alone.')
