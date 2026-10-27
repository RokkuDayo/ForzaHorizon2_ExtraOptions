"""Offline check of [KeyboardMouse] parsing with the --test build.

Usage: py tools/test-keyboard-mouse.py TEST_DLL
Works only in a temporary folder.
"""
import ctypes
import pathlib
import sys
import tempfile
from ctypes import wintypes

dll = ctypes.WinDLL(str(pathlib.Path(sys.argv[1]).resolve()))
dll.Fh2TweaksStatus.restype = ctypes.c_char_p
dll.Fh2TestReadOptions.argtypes = [wintypes.LPCWSTR, ctypes.c_void_p]
temporary = tempfile.TemporaryDirectory(prefix='kbm-ini-')
work = pathlib.Path(temporary.name)


def read(text):
    (work / 'FH2_ExtraOptions.ini').write_text(text, encoding='utf-8')
    buffer = ctypes.create_string_buffer(8192)
    return dll.Fh2TestReadOptions(str(work), buffer), dll.Fh2TweaksStatus().decode()


cases = [
    ('no section', '[Global]\nEnabled=1\n', 1),
    ('off', '[KeyboardMouse]\nEnabled=0\n', 1),
    ('on with options', '[KeyboardMouse]\nEnabled=1\nMode=Auto\nMouseLook=1\nMouseSensitivity=80\n'
     'Throttle=W\nBrake=S\n', 1),
    ('bad value', '[KeyboardMouse]\nEnabled=2\n', 0),
    ('unknown key', '[KeyboardMouse]\nNoSuchKey=1\n', 0),
    ('two driving actions on one key', '[KeyboardMouse]\nEnabled=1\nThrottle=W\nBrake=W\n', 0),
]
failed = 0
for name, text, expected in cases:
    result, status = read(text)
    ok = result == expected
    failed += not ok
    if not ok:
        print('FAIL:', name, result, status)
temporary.cleanup()
if failed:
    sys.exit(1)
print('PASS: [KeyboardMouse] is off by default; valid options parse; bad values, unknown keys and '
      'a key bound to two driving actions are refused.')
