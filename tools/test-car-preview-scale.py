"""Offline check of [Graphics] CarPreviewScale and [Bindings] RebuildCarPreviews with the
--test build.

Usage: py tools/test-car-preview-scale.py TEST_DLL
Works only in a temporary folder.
"""
import ctypes
import pathlib
import sys
import tempfile
from ctypes import wintypes

FIELD = 41  # ModOptions.car_preview_scale (int index)
dll = ctypes.WinDLL(str(pathlib.Path(sys.argv[1]).resolve()))
dll.Fh2TweaksStatus.restype = ctypes.c_char_p
dll.Fh2TestReadOptions.argtypes = [wintypes.LPCWSTR, ctypes.c_void_p]
temporary = tempfile.TemporaryDirectory(prefix='car-previews-')
work = pathlib.Path(temporary.name)


def read(text):
    (work / 'FH2_ExtraOptions.ini').write_text(text, encoding='utf-8')
    options = (ctypes.c_int * 256)()
    return dll.Fh2TestReadOptions(str(work), options), options[FIELD]


cases = [('default', '[Global]\nEnabled=1\n', (1, 1))]
cases += [('scale %d' % n, '[Graphics]\nCarPreviewScale=%d\n' % n, (1, n)) for n in range(1, 5)]
cases += [('scale ' + v, '[Graphics]\nCarPreviewScale=%s\n' % v, (0, None)) for v in ('0', '5', '2x', '')]
cases += [('binding', '[Bindings]\nRebuildCarPreviewsKeyboard=F10\nRebuildCarPreviewsXbox=View+Y\n', (1, 1))]
failed = 0
for name, text, expected in cases:
    result, value = read(text)
    ok = result == expected[0] and (expected[1] is None or value == expected[1])
    failed += not ok
    if not ok:
        print('FAIL:', name, result, value, dll.Fh2TweaksStatus().decode())
temporary.cleanup()
if failed:
    sys.exit(1)
print('PASS: [Graphics] CarPreviewScale defaults to 1 (off), 1-4 parse, other values are refused; '
      'the RebuildCarPreviews binding parses.')
