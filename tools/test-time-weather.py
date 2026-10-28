"""Offline check of [Frontend] WorldMenu and [World] with the --test build.

Usage: py tools/test-time-weather.py TEST_DLL [UI.zip]
Works only in a temporary folder. With the game's media/UI.zip, the Settings
hub and the World page are also built from the shipped XAML and parsed.
"""
import ctypes
import pathlib
import sys
import tempfile
import xml.etree.ElementTree as ElementTree
import zipfile
from ctypes import wintypes

FIELDS = 41  # ModOptions.world_menu, world_time, world_weather (int indices)
WORLD_TIME_CHOICES = 16  # CHOICES_WORLD_TIME in features/video_options.c
dll = ctypes.WinDLL(str(pathlib.Path(sys.argv[1]).resolve()))
dll.Fh2TweaksStatus.restype = ctypes.c_char_p
dll.Fh2TestReadOptions.argtypes = [wintypes.LPCWSTR, ctypes.c_void_p]
dll.Fh2TestSettingChoice.argtypes = [ctypes.c_uint, ctypes.c_uint]
dll.Fh2TestSettingChoice.restype = ctypes.c_wchar_p
dll.Fh2TestWorldMenuSet.argtypes = [ctypes.c_uint, ctypes.c_int]
for name in ('Fh2TestWorldHubXaml', 'Fh2TestWorldXaml', 'Fh2TestVideoXaml'):
    getattr(dll, name).argtypes = [ctypes.c_char_p, ctypes.c_size_t, ctypes.c_char_p, ctypes.c_size_t]
    getattr(dll, name).restype = ctypes.c_size_t
temporary = tempfile.TemporaryDirectory(prefix='time-weather-')
work = pathlib.Path(temporary.name)
ini = work / 'FH2_ExtraOptions.ini'
failed = []


def read(text=None):
    if text is not None:
        ini.write_text(text, encoding='utf-8')
    options = (ctypes.c_int * 256)()
    return dll.Fh2TestReadOptions(str(work), options), tuple(options[FIELDS:FIELDS + 3])


cases = [('default', '[Global]\nEnabled=1\n', (1, (0, 0, 0))),
         ('on', '[Frontend]\nWorldMenu=1\n[World]\nTimeOfDay=sunset\nWeather=storm\n', (1, (1, 3, 4))),
         ('any case', '[World]\nTimeOfDay=Midnight\nWeather=WET\n', (1, (0, 4, 2))),
         ('game', '[World]\nTimeOfDay=game\nWeather=game\n', (1, (0, 0, 0))),
         ('bad time', '[World]\nTimeOfDay=dusk\n', (0, None)),
         ('bad weather', '[World]\nWeather=snow\n', (0, None)),
         ('number', '[World]\nWeather=1\n', (0, None)),
         ('bad key', '[World]\nSeason=summer\n', (0, None)),
         ('bad switch', '[Frontend]\nWorldMenu=2\n', (0, None))]
for name, text, (result, values) in cases:
    got = read(text)
    if got[0] != result or (values is not None and got[1] != values):
        failed.append(name)

# The menu writes its choice to [World]; the strict reader takes it back.
read('﻿[Frontend]\nWorldMenu=1\n')
if (dll.Fh2TestWorldMenuSet(0, 2) != 1 or dll.Fh2TestWorldMenuSet(1, 4) != 1 or
        dll.Fh2TestWorldMenuSet(0, 2) != 0 or dll.Fh2TestWorldMenuSet(1, 5) != 0 or
        dll.Fh2TestWorldMenuSet(2, 0) != 0 or read() != (1, (1, 2, 4))):
    failed.append('write')
if dll.Fh2TestWorldMenuSet(0, 0) != 1 or dll.Fh2TestWorldMenuSet(1, 0) != 1 or read() != (1, (1, 0, 0)):
    failed.append('write game')

# Choice names (no game: the fallbacks of the game's own table entries).
choices = [[dll.Fh2TestSettingChoice(WORLD_TIME_CHOICES + c, i) for i in range(6)] for c in range(2)]
if choices != [['Default', 'Sunrise', 'Midday', 'Sunset', 'Midnight', None],
               ['Default', 'Clear', 'Wet', 'Rain', 'Storm', None]]:
    failed.append('choices')


def build(function, text):
    out = ctypes.create_string_buffer(262144)
    length = function(text, len(text), out, len(out))
    return out.raw[:length] if length else None


def parse(text):
    try:
        return ElementTree.fromstring(text[3:] if text.startswith(b'\xef\xbb\xbf') else text)
    except ElementTree.ParseError:
        return None


checked = 'hub and World page XAML not checked (no UI.zip given)'
if len(sys.argv) > 2:
    with zipfile.ZipFile(sys.argv[2]) as archive:
        hub = archive.read('Scenes/Anthem/A_PauseOptions.xaml')
        video = archive.read('Scenes/Anthem/A_VideoOptions.xaml')
    X = '{http://schemas.microsoft.com/winfx/2006/xaml}'
    result = build(dll.Fh2TestWorldHubXaml, hub)
    root = parse(result) if result else None
    buttons = {} if root is None else {
        element.get(X + 'Name'): element for element in root.iter() if element.tag.endswith('Button') and
        (element.get(X + 'Name') or '').startswith('PART_Button_')}
    world, credits = buttons.get('PART_Button_EOWorld'), buttons.get('PART_Button_TeamCredits')

    def attribute(element, suffix):
        return next((value for key, value in element.attrib.items() if key.endswith(suffix)), None)

    if (world is None or credits is None or len(buttons) != 7 or
            attribute(world, 'GridX') != '4' or attribute(world, 'GridY') != '1' or
            'Tile_1x1' not in credits.get('Style') or 'BottomHalfTile' not in credits.get('Template') or
            b'Heading="WORLD"' not in result or build(dll.Fh2TestWorldHubXaml, result) is not None):
        failed.append('hub XAML')
    result = build(dll.Fh2TestWorldXaml, video)
    root = parse(result) if result else None
    rows = [] if root is None else [element for element in root.iter() if element.tag.endswith('SpinnerControl')
                                    or element.tag.endswith('}Slider')]
    if (root is None or [row.get('SelectedIndex') for row in rows] !=
            ['{Binding WorldTimeOfDay}', '{Binding WorldWeather}'] or
            rows[0].get(X + 'Name') != 'FirstItem' or b'Content="WORLD"' not in result or
            b'IDS_VideoOptions' in result or b'Visibility="Collapsed"' not in result or
            b'x:Key="EO_WorldWeather"' not in result or build(dll.Fh2TestWorldXaml, result) is not None):
        failed.append('World page XAML')
    # The Video page itself is still extended as before, without the World rows.
    result = build(dll.Fh2TestVideoXaml, video)
    if not result or parse(result) is None or b'WorldTimeOfDay' in result:
        failed.append('Video page XAML')
    checked = 'the shipped Settings hub gets the World tile under Team Credits and the Video page ' \
              'rebuilds as the World page (both parse as XML); the Video page is unchanged'
temporary.cleanup()
if failed:
    print('FAIL:', ', '.join(failed))
    sys.exit(1)
print('PASS: [Frontend] WorldMenu is off by default; [World] TimeOfDay/Weather parse, bad values '
      'are refused; menu choices are written to [World] and read back; ' + checked + '.')
