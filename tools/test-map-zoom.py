"""Offline check of the [Frontend] map zoom keys with the --test build.

Usage: py tools/test-map-zoom.py TEST_DLL
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
temporary = tempfile.TemporaryDirectory(prefix='map-zoom-')
work = pathlib.Path(temporary.name)


def read(text):
    (work / 'FH2_ExtraOptions.ini').write_text(text, encoding='utf-8')
    return dll.Fh2TestReadOptions(str(work), ctypes.create_string_buffer(8192))


cases = [('no keys', '[Frontend]\nSkipIntro=1\n', 1),
         ('all keys', '[Frontend]\nMapStartZoom=150\nMapMaxZoomOut=225\nMapIslandStartZoom=100\n'
                      'MapIslandMaxZoomOut=175\n', 1),
         ('limits', '[Frontend]\nMapStartZoom=25\nMapMaxZoomOut=400\n', 1),
         ('start too small', '[Frontend]\nMapStartZoom=24\n', 0),
         ('max below the game', '[Frontend]\nMapMaxZoomOut=99\n', 0),
         ('too large', '[Frontend]\nMapIslandMaxZoomOut=401\n', 0),
         ('not a number', '[Frontend]\nMapStartZoom=far\n', 0)]
failed = 0
for name, text, expected in cases:
    result = read(text)
    if result != expected:
        failed += 1
        print('FAIL:', name, result, dll.Fh2TweaksStatus().decode())
temporary.cleanup()
if failed:
    sys.exit(1)
print('PASS: [Frontend] map zoom keys parse within 25-400 (max zoom-out 100-400); other values '
      'are refused; other [Frontend] keys are unaffected.')
