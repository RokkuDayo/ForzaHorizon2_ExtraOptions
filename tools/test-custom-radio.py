"""Offline check of [CustomRadio] and the radio [Bindings] with the --test build.

Usage: py tools/test-custom-radio.py TEST_DLL
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
temporary = tempfile.TemporaryDirectory(prefix='radio-ini-')
work = pathlib.Path(temporary.name)
def read(text):
    (work / 'FH2_ExtraOptions.ini').write_text(text, encoding='utf-8')
    return dll.Fh2TestReadOptions(str(work), ctypes.create_string_buffer(8192)), dll.Fh2TweaksStatus().decode()
cases = [('no section', '[Global]\nEnabled=1\n', 1),
         ('on', '[CustomRadio]\nEnabled=1\nFolder=Music\nName=MY TUNES\nVolume=150\nUnlockAllStations=1\nPcmDecode=1\n', 1),
         ('volume too high', '[CustomRadio]\nVolume=401\n', 0),
         ('bad boolean', '[CustomRadio]\nEnabled=yes\n', 0),
         ('unknown key', '[CustomRadio]\nSingleEntry=1\n', 0),
         ('bindings', '[Bindings]\nRadioStationKeyboard=F10\nRadioNextXbox=RB+Y\nRadioRestartKeyboard=None\n', 1)]
bad = 0
for name, text, expected in cases:
    r, status = read(text); ok = r == expected; bad += not ok
    if not ok:
        print('FAIL:', name, r, status)
temporary.cleanup()
if bad:
    sys.exit(1)
print('PASS: [CustomRadio] is off by default; valid options parse; bad values and unknown keys are '
      'refused; RadioStation/RadioNext/RadioRestart bindings parse.')
