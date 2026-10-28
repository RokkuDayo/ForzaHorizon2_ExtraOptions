"""Offline check of the KEYBOARD & MOUSE tile and page with the --test build.

Usage: py tools/test-keyboard-mouse-page.py TEST_DLL UI_ZIP
UI_ZIP is the game's media\\UI.zip (Scenes/Anthem/A_PauseOptions.xaml and
A_VideoOptions.xaml are read from it). Works only in a temporary folder.
"""
import ctypes
import pathlib
import sys
import tempfile
import xml.etree.ElementTree as ElementTree
import zipfile
from ctypes import wintypes

dll = ctypes.WinDLL(str(pathlib.Path(sys.argv[1]).resolve()))
archive = zipfile.ZipFile(sys.argv[2])
dll.Fh2TweaksStatus.restype = ctypes.c_char_p
dll.Fh2TestReadOptions.argtypes = [wintypes.LPCWSTR, ctypes.c_void_p]
dll.Fh2TestKeyboardMenuDirectory.argtypes = [wintypes.LPCWSTR]
dll.Fh2TestKeyboardMenuChoice.restype = wintypes.LPCWSTR
dll.Fh2TestRebindChoice.restype = wintypes.LPCWSTR
for name in ['Fh2TestKeyboardHubXaml', 'Fh2TestKeyboardXaml', 'Fh2TestVideoXaml']:
    getattr(dll, name).restype = ctypes.c_size_t
    getattr(dll, name).argtypes = [ctypes.c_char_p, ctypes.c_size_t, ctypes.c_void_p,
                                   ctypes.c_size_t]
NAMESPACES = {'x': 'http://schemas.microsoft.com/winfx/2006/xaml',
              'local': 'clr-namespace:ForzaUI',
              'w': 'http://schemas.microsoft.com/winfx/2006/xaml/presentation'}
X_NAME = '{%s}Name' % NAMESPACES['x']
X_KEY = '{%s}Key' % NAMESPACES['x']
failed = 0


def check(ok, message):
    global failed
    if not ok:
        failed += 1
        print('FAIL:', message)


def build(function, name):
    text = archive.read('Scenes/Anthem/' + name)
    out = ctypes.create_string_buffer(262144)
    length = function(text, len(text), out, len(out))
    return text, out.raw[:length]


def parse(data):
    return ElementTree.fromstring(data.decode('utf-8-sig'))


# The game's own pages come out unchanged while keyboard & mouse is off.
check(not dll.Fh2TestKeyboardMenuInstalled(), 'the tile is installed without a game')
_, video = build(dll.Fh2TestVideoXaml, 'A_VideoOptions.xaml')
check(video and b'Kbm' not in video, 'the plain Video page carries keyboard & mouse rows')

# Settings hub: Controls cut to 1x1, KEYBOARD & MOUSE next to it.
_, hub = build(dll.Fh2TestKeyboardHubXaml, 'A_PauseOptions.xaml')
check(hub, 'hub layout not matched')
if hub:
    buttons = {b.get(X_NAME): b for b in parse(hub).iter('{%s}Button' % NAMESPACES['w'])}
    grid = '{%s}TileMenuNavigationManager.Grid' % NAMESPACES['local']
    tile, controls = buttons.get('PART_Button_EOKeyboard'), buttons.get('PART_Button_Controls')
    check(tile is not None and (tile.get(grid + 'X'), tile.get(grid + 'Y')) == ('2', '0'),
          'KEYBOARD & MOUSE tile missing or not at column 2 of the top row')
    check(controls is not None and 'Tile_1x1' in controls.get('Style'),
          'Controls tile was not cut to 1x1')
    heading = [e.get('Heading') for e in parse(hub).iter()
               if e.get(X_KEY) == 'EO_MenuDataItem_Keyboard']
    check(heading == ['KEYBOARD & MOUSE'], 'tile caption: %r' % heading)
    out = ctypes.create_string_buffer(262144)
    check(dll.Fh2TestKeyboardHubXaml(hub, len(hub), out, len(out)) == 0,
          'the hub was extended twice')

# The page: own title, only the keyboard & mouse rows.
_, page = build(dll.Fh2TestKeyboardXaml, 'A_VideoOptions.xaml')
check(page, 'Video page layout not matched for the keyboard & mouse page')
if page:
    root = parse(page)
    lists = [e for e in root.iter() if e.get(X_NAME) == 'OptionsList']
    rows = list(lists[0]) if lists else []
    names = [r.get('SelectedIndex') for r in rows]
    check(len(rows) == 9 + 32 + 1, 'page rows: %d' % len(rows))
    check(all(r.tag.endswith('SpinnerControl') for r in rows), 'a non-spinner row is listed')
    check(names[:2] == ['{Binding KbmMode}', '{Binding KbmMouseLook}'] and
          names[-1] == '{Binding KbmKeyReset}', 'row order: %r' % names[:2])
    check(rows and rows[0].get(X_NAME) == 'FirstItem', 'first row is not FirstItem')
    check(lists and lists[0].get('SelectedIndex') ==
          '{Binding KbmKeyListIndex, Mode=OneWayToSource}', 'list selection is not tracked')
    check(b'Gamma' not in page.split(b'OptionsList', 1)[1], 'Video rows left on the page')
    titles = [e.get('Content') for e in root.iter() if e.get('Tag') ==
              '{local:Loc ScreenTitles.IDS_MyProfile}']
    check(titles == ['KEYBOARD & MOUSE'], 'page title: %r' % titles)
    keys = {e.get(X_KEY) for e in root.iter() if e.get(X_KEY)}
    check(all('EO_' + r.get('SelectedIndex')[9:-1] in keys for r in rows),
          'a row has no description resource')

check(dll.Fh2TestKeyboardTextComplete() == 16, 'a translation misses keyboard & mouse text')
check(dll.Fh2TestKeyboardMenuChoice(0, 1) == 'Keyboard & Mouse' and
      dll.Fh2TestKeyboardMenuChoice(1, 1) == 'On' and
      dll.Fh2TestKeyboardMenuChoice(4, 2) == '100%', 'option choice labels')

# FH2_ExtraOptions.ini [KeyboardMouse] round trip.
temporary = tempfile.TemporaryDirectory(prefix='kbm-page-')
work = pathlib.Path(temporary.name)
ini = work / 'FH2_ExtraOptions.ini'
ini.write_text('[KeyboardMouse]\nEnabled=1\nMouseSensitivity=90 ; comment\nThrottle=W\n',
               encoding='utf-8')
dll.Fh2TestKeyboardMenuDirectory(str(work))
dll.Fh2TestRebindReload()


def accepted():
    buffer = ctypes.create_string_buffer(8192)
    return dll.Fh2TestReadOptions(str(work), buffer) == 1


def value(key):
    for line in ini.read_text(encoding='utf-8').splitlines():
        if line.split('=')[0].strip() == key:
            return line.split('=', 1)[1].strip()
    return None


check(dll.Fh2TestKeyboardMenuGet(4) == 2, 'MouseSensitivity=90 does not show the nearest choice')
check(dll.Fh2TestKeyboardMenuGet(1) == 1, 'MouseLook does not default to on')
check(dll.Fh2TestKeyboardMenuSet(4, 5) and value('MouseSensitivity') == '160',
      'mouse sensitivity not written')
check(dll.Fh2TestKeyboardMenuSet(0, 1) and value('Mode') == 'KBM', 'mode not written')
check(not dll.Fh2TestKeyboardMenuSet(0, 1), 'an unchanged value was written again')
check(accepted(), 'the mod refuses the INI the page wrote: %s' % dll.Fh2TweaksStatus())
dll.Fh2TestRebindCapture(0, 0x17)  # Throttle: I
check(value('Throttle') == 'I' and dll.Fh2TestRebindKeys(0) == 0x17, 'Throttle not rebound')
dll.Fh2TestRebindCapture(1, 0x17)  # Brake: I is taken by Throttle
check(value('Brake') is None and dll.Fh2TestRebindKeys(1) == 0x1f, 'a taken key was accepted')
dll.Fh2TestRebindCapture(2, 0x0e)  # Steer left: Backspace clears
check(value('SteerLeft') == 'None' and dll.Fh2TestRebindKeys(2) == 0, 'clearing failed')
dll.Fh2TestRebindCapture(3, 0x01)  # Steer right: Esc cancels
check(value('SteerRight') is None and dll.Fh2TestRebindKeys(3) == 0x20, 'Esc changed a key')
check(accepted(), 'the mod refuses the rebound INI: %s' % dll.Fh2TweaksStatus())
dll.Fh2TestRebindReload()
check(dll.Fh2TestRebindKeys(0) == 0x17, 'the rebound key did not survive a reload')
check(dll.Fh2TestRebindChoice(0, dll.Fh2TestRebindGet(0)) == 'I', 'Throttle row label')
dll.Fh2TestRebindReset()
check(value('Throttle') is None and value('SteerLeft') is None and
      dll.Fh2TestRebindKeys(0) == 0x11, 'Reset did not restore the defaults')
check(accepted(), 'the mod refuses the INI after Reset')
dll.Fh2TestKeyboardMenuDirectory(None)
temporary.cleanup()
if failed:
    sys.exit(1)
print('PASS: KEYBOARD & MOUSE tile next to Controls; its own page with 9 options and 33 key rows; '
      'every translation complete; options and key bindings round-trip through '
      '[KeyboardMouse] and the mod still accepts the INI.')
