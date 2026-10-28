"""Offline check of [Frontend] PauseOnlyOnControllerUnplug with the --test build.

Usage: py tools/test-controller-pause.py TEST_DLL
Works only in a temporary folder.
"""
import ctypes
import pathlib
import sys
import tempfile
from ctypes import wintypes

FIELD = 41  # ModOptions.controller_pause_on_unplug_only (int index)
dll = ctypes.WinDLL(str(pathlib.Path(sys.argv[1]).resolve()))
dll.Fh2TweaksStatus.restype = ctypes.c_char_p
dll.Fh2TestReadOptions.argtypes = [wintypes.LPCWSTR, ctypes.c_void_p]
temporary = tempfile.TemporaryDirectory(prefix='controller-pause-')
work = pathlib.Path(temporary.name)


def read(text):
    (work / 'FH2_ExtraOptions.ini').write_text(text, encoding='utf-8')
    options = (ctypes.c_int * 256)()
    return dll.Fh2TestReadOptions(str(work), options), options[FIELD]


failed = 0
for name, text, expected in [('default', '[Global]\nEnabled=1\n', (1, 0)),
                             ('on', '[Frontend]\nPauseOnlyOnControllerUnplug=1\n', (1, 1)),
                             ('off', '[Frontend]\nPauseOnlyOnControllerUnplug=0\n', (1, 0)),
                             ('bad value', '[Frontend]\nPauseOnlyOnControllerUnplug=2\n', (0, None))]:
    result, value = read(text)
    ok = result == expected[0] and (expected[1] is None or value == expected[1])
    failed += not ok
    if not ok:
        print('FAIL:', name, result, value, dll.Fh2TweaksStatus().decode())
temporary.cleanup()
if failed:
    sys.exit(1)
print('PASS: [Frontend] PauseOnlyOnControllerUnplug is off by default, 1 turns it on, other '
      'values are refused.')
