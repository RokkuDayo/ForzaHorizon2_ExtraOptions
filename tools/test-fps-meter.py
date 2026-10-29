"""Offline check of [Monitor] ShowFps / PerfLog parsing with the --test build.

Usage: py tools/test-fps-meter.py TEST_DLL
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
temporary = tempfile.TemporaryDirectory(prefix='fps-meter-')
work = pathlib.Path(temporary.name)


def read(text):
    (work / 'FH2_ExtraOptions.ini').write_text(text, encoding='utf-8')
    return dll.Fh2TestReadOptions(str(work), ctypes.create_string_buffer(8192))


cases = [('no section', '[Global]\nEnabled=1\n', 1)]
cases += [('ShowFps=%d' % n, '[Monitor]\nShowFps=%d\n' % n, 1) for n in range(3)]
cases += [('PerfLog=%d' % n, '[Monitor]\nPerfLog=%d\n' % n, 1) for n in range(2)]
cases += [('ShowFps=3', '[Monitor]\nShowFps=3\n', 0), ('PerfLog=2', '[Monitor]\nPerfLog=2\n', 0),
          ('unknown key', '[Monitor]\nResolution=1\n', 0)]
failed = 0
for name, text, expected in cases:
    result = read(text)
    if result != expected:
        failed += 1
        print('FAIL:', name, result, dll.Fh2TweaksStatus().decode())
temporary.cleanup()
if failed:
    sys.exit(1)
print('PASS: [Monitor] is off by default; ShowFps 0-2 and PerfLog 0/1 parse; other values and '
      'unknown keys are refused.')
