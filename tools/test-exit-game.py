"""Offline check of [Frontend] ExitGame with the --test build.

Usage: py tools/test-exit-game.py TEST_DLL
Works only in a temporary folder.
"""
import ctypes
import pathlib
import sys
import tempfile
from ctypes import wintypes

FIELD = 41  # ModOptions.exit_game (int index)
dll = ctypes.WinDLL(str(pathlib.Path(sys.argv[1]).resolve()))
dll.Fh2TweaksStatus.restype = ctypes.c_char_p
dll.Fh2TestReadOptions.argtypes = [wintypes.LPCWSTR, ctypes.c_void_p]
dll.Fh2TestExitGameRelabel.argtypes = [ctypes.c_char_p, ctypes.c_int, ctypes.c_int]
temporary = tempfile.TemporaryDirectory(prefix='exit-game-')
work = pathlib.Path(temporary.name)
failed = []


def read(text):
    (work / 'FH2_ExtraOptions.ini').write_text(text, encoding='utf-8')
    options = (ctypes.c_int * 256)()
    return dll.Fh2TestReadOptions(str(work), options), options[FIELD]


for name, text, expected in [('default', '[Global]\nEnabled=1\n', (1, 0)),
                             ('on', '[Frontend]\nExitGame=1\n', (1, 1)),
                             ('bad value', '[Frontend]\nExitGame=2\n', (0, None))]:
    result, value = read(text)
    if result != expected[0] or (expected[1] is not None and value != expected[1]):
        failed.append(name)

# The Car Packs data item of the pause page, as the game ships it (abridged).
item = (b'<local:TileMenuDataItem Heading="{local:Loc PauseMenu.IDS_CarPacks}" '
        b'Image="/ForzaUI;component/Textures/CarPacks.png" x:Key="A_MenuDataItem_CarPacks" />')
page = (b'<UserControl><Canvas x:Name="PART_SCOPE_Marketplace">' + item +
        b'<Button x:Name="PART_Button_CarPacks" /></Canvas></UserControl>')
buffer = ctypes.create_string_buffer(page, len(page) + 512)
length = dll.Fh2TestExitGameRelabel(buffer, len(page), len(page) + 512)
text = buffer.raw[:length]
if (b'Heading="EXIT GAME"' not in text or b'PauseMenu/Quit.png' not in text or
        b'IDS_CarPacks' in text or b'x:Key="A_MenuDataItem_CarPacks"' not in text or
        b'<Button x:Name="PART_Button_CarPacks" />' not in text):
    failed.append('relabel')
# Not the pause page: left alone.
other = b'<UserControl x:Key="A_MenuDataItem_CarPacks" />'
buffer = ctypes.create_string_buffer(other, len(other) + 512)
if dll.Fh2TestExitGameRelabel(buffer, len(other), len(other) + 512) != len(other):
    failed.append('other page')
# No room for the longer caption: left alone.
short = (b'<UserControl><Canvas x:Name="PART_SCOPE_Marketplace">'
         b'<local:TileMenuDataItem x:Key="A_MenuDataItem_CarPacks" /></Canvas></UserControl>')
buffer = ctypes.create_string_buffer(short, len(short) + 1)
if dll.Fh2TestExitGameRelabel(buffer, len(short), len(short) + 1) != len(short) or buffer.raw[:len(short)] != short:
    failed.append('no room')
temporary.cleanup()
if failed:
    print('FAIL:', ', '.join(failed))
    sys.exit(1)
print('PASS: [Frontend] ExitGame is off by default; the Car Packs tile data becomes Exit Game with '
      'the Quit icon; other pages and a full buffer are left alone.')
