"""Offline check of [Frontend] ExitGame with the --test build.

Usage: py tools/test-exit-game.py TEST_DLL [UI.zip]
Works only in a temporary folder. With the game's Mount/media/UI.zip the
pause page edit also runs on the shipped A_PauseMenu.xaml.
"""
import ctypes
import pathlib
import re
import sys
import tempfile
import xml.etree.ElementTree as ET
import zipfile
from ctypes import wintypes

FIELD = 41  # ModOptions.exit_game (int index)
dll = ctypes.WinDLL(str(pathlib.Path(sys.argv[1]).resolve()))
dll.Fh2TweaksStatus.restype = ctypes.c_char_p
dll.Fh2TestReadOptions.argtypes = [wintypes.LPCWSTR, ctypes.c_void_p]
dll.Fh2TestExitGameXaml.argtypes = [ctypes.c_char_p, ctypes.c_size_t, ctypes.c_size_t]
dll.Fh2TestExitGameXaml.restype = ctypes.c_size_t
temporary = tempfile.TemporaryDirectory(prefix='exit-game-')
work = pathlib.Path(temporary.name)
failed = []


def read(text):
    (work / 'FH2_ExtraOptions.ini').write_text(text, encoding='utf-8')
    options = (ctypes.c_int * 256)()
    return dll.Fh2TestReadOptions(str(work), options), options[FIELD]


def extend(page, room=16384):
    buffer = ctypes.create_string_buffer(page, len(page) + room)
    length = dll.Fh2TestExitGameXaml(buffer, len(page), len(page) + room)
    return buffer.raw[:length]


for name, text, expected in [('default', '[Global]\nEnabled=1\n', (1, 0)),
                             ('on', '[Frontend]\nExitGame=1\n', (1, 1)),
                             ('bad value', '[Frontend]\nExitGame=2\n', (0, None))]:
    result, value = read(text)
    if result != expected[0] or (expected[1] is not None and value != expected[1]):
        failed.append(name)

WIDE = (b'Style="{StaticResource A_Style_Button_Tile_2x1}" '
        b'Template="{StaticResource A_ControlTemplate_HorizontalImageTile}"')


def check(page, label):
    text = extend(page)
    home = text.find(b'x:Name="PART_SCOPE_Home"')
    tile = text.find(b'x:Name="PART_Button_EOExitGame"')
    after_home = re.search(rb'x:Name="PART_SCOPE_(?!Home)', text[home + 1:]) if home >= 0 else None
    inside = home >= 0 and tile > home and (not after_home or tile < home + 1 + after_home.start())
    squares = text.count(b'Template="{StaticResource EO_ControlTemplate_SquareImageTile}"')
    if not (inside and squares == 2 and b'x:Key="EO_ControlTemplate_SquareImageTile"' in text and
            b'x:Key="EO_MenuDataItem_ExitGame" Heading="EXIT GAME"' in text and
            b'PauseMenu/Quit.png' in text):
        failed.append(label)
    if extend(text) != text:  # a second pass leaves it alone
        failed.append(label + ' twice')
    return text


sample = (b'<Page><Page.Resources><ResourceDictionary></ResourceDictionary></Page.Resources>'
          b'<Canvas x:Name="PART_SCOPE_Home">'
          b'<Button ' + WIDE + b' x:Name="PART_Button_HorizonOnline" />'
          b'<Button ' + WIDE + b' x:Name="PART_Button_HorizonSolo" /></Canvas>'
          b'<Canvas x:Name="PART_SCOPE_Marketplace"></Canvas></Page>')
check(sample, 'sample')
if extend(b'<Page x:Name="PART_SCOPE_Home"></Page>') != b'<Page x:Name="PART_SCOPE_Home"></Page>':
    failed.append('other page')
buffer = ctypes.create_string_buffer(sample, len(sample) + 1)
if dll.Fh2TestExitGameXaml(buffer, len(sample), len(sample) + 1) != len(sample):
    failed.append('no room')

if len(sys.argv) > 2:
    shipped = zipfile.ZipFile(sys.argv[2]).read('Scenes/Anthem/A_PauseMenu.xaml')
    text = check(shipped, 'shipped page')
    try:
        ET.fromstring(text.decode('utf-8-sig'))
    except ET.ParseError as error:
        failed.append('shipped page XML: %s' % error)
temporary.cleanup()
if failed:
    print('FAIL:', ', '.join(failed))
    sys.exit(1)
print('PASS: [Frontend] ExitGame is off by default; the Exit Game tile is added on the Home tab '
      'with Online Road Trip and Horizon Solo made square%s; other pages and a full buffer are '
      'left alone.' % (', on the shipped pause page too' if len(sys.argv) > 2 else ''))
