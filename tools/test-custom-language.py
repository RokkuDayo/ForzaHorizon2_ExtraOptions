"""Offline check of [Language] Custom (custom language packs) and of the voice
subtitle table reader with the --test build.

Usage: py tools/test-custom-language.py TEST_DLL [VoiceSubtitles.str]
Works only in a temporary folder. The optional VoiceSubtitles.str (for example
the one of a real pack) is read as well and its line count printed.
"""
import ctypes
import io
import pathlib
import struct
import sys
import tempfile
import zipfile
from ctypes import wintypes

dll = ctypes.WinDLL(str(pathlib.Path(sys.argv[1]).resolve()))
dll.Fh2TweaksStatus.restype = ctypes.c_char_p
dll.Fh2TestReadOptions.argtypes = [wintypes.LPCWSTR, ctypes.c_void_p]
dll.Fh2TestCustomLanguage.argtypes = [wintypes.LPCWSTR, wintypes.LPCWSTR, ctypes.c_char_p,
                                      ctypes.c_uint]
dll.Fh2TestVoiceSubtitles.argtypes = [wintypes.LPCWSTR, wintypes.LPCWSTR,
                                      ctypes.POINTER(ctypes.c_ulonglong),
                                      ctypes.POINTER(ctypes.c_uint), ctypes.c_uint,
                                      ctypes.c_wchar_p, ctypes.c_uint]
dll.Fh2TestVoiceSubtitles.restype = ctypes.c_uint
temporary = tempfile.TemporaryDirectory(prefix='custom-language-')
work = pathlib.Path(temporary.name)
settings = work / 'settings'
mount = work / 'Mount'
settings.mkdir()
mount.mkdir()
executable = str(mount / 'forza_durango_release_final.exe')
failed = 0


def check(name, condition, detail=''):
    global failed
    if not condition:
        failed += 1
        print('FAIL:', name, detail)


def write_ini(text):
    (settings / 'FH2_ExtraOptions.ini').write_text(text, encoding='utf-8')


def read_options(text):
    write_ini(text)
    return dll.Fh2TestReadOptions(str(settings), ctypes.create_string_buffer(8192))


def prepare(text):
    write_ini(text)
    output = ctypes.create_string_buffer(1024)
    mounted = dll.Fh2TestCustomLanguage(executable, str(settings), output, len(output))
    return mounted, output.value.decode('utf-8')


def pack(identifier, language_ini=None):
    folder = mount / 'languages' / identifier
    folder.mkdir(parents=True, exist_ok=True)
    if language_ini is not None:
        (folder / 'language.ini').write_text(language_ini, encoding='utf-16')
    return folder


# The strict INI parser: [Language] Custom takes a folder name or nothing.
for name, text, expected in [
        ('no key', '[Language]\n', 1),
        ('pack id', '[Language]\nCustom=uk\n', 1),
        ('pack id and comment', '[Language]\nCustom=uk   ; the pack\n', 1),
        ('empty', '[Language]\nCustom=\n', 1),
        ('id characters', '[Language]\nCustom=pt-BR_test2\n', 1),
        ('32 characters', '[Language]\nCustom=' + 'a' * 32 + '\n', 1),
        ('33 characters', '[Language]\nCustom=' + 'a' * 33 + '\n', 0),
        ('path', '[Language]\nCustom=..\\uk\n', 0),
        ('space', '[Language]\nCustom=u k\n', 0),
        ('other key', '[Language]\nCustomFont=1\n', 0)]:
    result = read_options(text)
    check('INI ' + name, result == expected, (result, dll.Fh2TweaksStatus().decode()))

# Off by default: no key, nothing to mount.
check('off without the key', prepare('[Global]\nEnabled=1\n') == (0, ''))
check('off with an empty value', prepare('[Language]\nCustom=\n') == (0, ''))

# A pack without language.ini is refused.
pack('missing')
mounted, text = prepare('[Language]\nCustom=missing\n')
check('missing language.ini', not mounted and 'language.ini' in text, text)

# A valid pack: mount path, slot and string table archive.
pack('test', '[Language]\nName=Тест\nSlot=ru-RU\n')
mounted, text = prepare('[Language]\nCustom=test\n')
check('valid pack', mounted and text == 'mount=game:\\languages\\test\\;slot=ru-RU;archive=RU;name=Тест',
      text)

pack('comments', '[Language]\nName=Comments ; shown in the log\nSlot=ru-RU   ; Russian\n')
mounted, text = prepare('[Language]\nCustom=comments  ; the pack folder\n')
check('inline comments', mounted and text == 'mount=game:\\languages\\comments\\;slot=ru-RU;archive=RU;'
      'name=Comments', text)

pack('nameless', '[Language]\nSlot=pl-PL\n')
mounted, text = prepare('[Language]\nCustom=nameless\n')
check('name defaults to the id', mounted and text.endswith(';slot=pl-PL;archive=PL;name=nameless'), text)

pack('case', '[Language]\nSlot=PT-br\n')
mounted, text = prepare('[Language]\nCustom=case\n')
check('slot ignores case', mounted and ';archive=br;' in text, text)

for identifier, ini in [('badslot', '[Language]\nName=X\nSlot=uk-UA\n'),
                        ('noslot', '[Language]\nName=X\n'),
                        ('archive', '[Language]\nSlot=RU\n')]:
    pack(identifier, ini)
    mounted, text = prepare('[Language]\nCustom=%s\n' % identifier)
    check('bad slot ' + identifier, not mounted and 'Slot' in text, text)

# A later call with the key removed turns the pack off again.
check('off again', prepare('[Language]\n') == (0, ''))


# Voice subtitles: an LSB2 string table with <id> text rows and <id>~t times.
def fnv1a64(data):
    value = 0xcbf29ce484222325
    for byte in data:
        value = ((value ^ byte) * 0x100000001b3) & 0xffffffffffffffff
    return value


def lsb2(rows):
    texts = b''.join(text.encode('utf-16-le') for _, text in rows)
    records, position = b'', 0
    for index, (_, text) in enumerate(rows):
        records += struct.pack('<HI', index + 1, position)
        position += len(text)
    records += struct.pack('<HI', 0, position)
    text_section = struct.pack('<II', 0, len(rows)) + records + texts
    names = b''
    ids = b''
    for index, (name, _) in enumerate(rows):
        ids += struct.pack('<HHI', index + 1, 0, len(names))
        names += name.encode('ascii') + b'\0'
    id_section = struct.pack('<II', 0, len(rows)) + ids + names
    header = b'LSB2' + bytes(16) + struct.pack('<II', 40, 40 + len(text_section)) + bytes(12)
    return header + text_section + id_section


rows = [('Ben_Intro_01', 'Привіт, це Бен.'), ('Ashley_Race_02', 'Їдьмо!'),
        ('Ben_Intro_01~t', '4.5'), ('Anna_Short_03', 'Так.'), ('Anna_Short_03~t', '0,5')]
table = lsb2(rows)
seconds = {'Ben_Intro_01': 5.1, 'Ashley_Race_02': 3.6, 'Anna_Short_03': 2.0}  # + 0.6 s margin, 2 s minimum


def voice(stringtables, archive='RU'):
    hashes = (ctypes.c_ulonglong * 16)()
    durations = (ctypes.c_uint * 16)()
    first = ctypes.create_unicode_buffer(256)
    count = dll.Fh2TestVoiceSubtitles(str(stringtables), archive, hashes, durations, 16, first, 256)
    return count, list(hashes[:count]), list(durations[:count]), first.value


def expect_lines(name, result):
    count, hashes, durations, first = result
    wanted = [n for n, _ in rows if not n.endswith('~t')]
    check(name + ' count', count == 3, result)
    check(name + ' hashes', hashes == [fnv1a64(n.encode()) for n in wanted], result)
    check(name + ' times', durations == [round(seconds[n] * 1e7) for n in wanted], result)
    check(name + ' text', first == rows[0][1], first)


loose = work / 'loose'
(loose / 'RU').mkdir(parents=True)
(loose / 'RU' / 'VoiceSubtitles.str').write_bytes(table)
expect_lines('loose table', voice(loose))
check('other archive', voice(loose, 'PL')[0] == 0)


def zipped(folder, method):
    folder.mkdir()
    data = io.BytesIO()
    with zipfile.ZipFile(data, 'w', method) as archive:
        archive.writestr('Other.str', b'LSB2' + bytes(60))
        archive.writestr('voicesubtitles.STR', table)
    (folder / 'RU.zip').write_bytes(data.getvalue())
    return folder


expect_lines('stored zip entry', voice(zipped(work / 'stored', zipfile.ZIP_STORED)))
check('compressed zip entry refused', voice(zipped(work / 'deflated', zipfile.ZIP_DEFLATED))[0] == 0)

bad = work / 'bad'
(bad / 'RU').mkdir(parents=True)
(bad / 'RU' / 'VoiceSubtitles.str').write_bytes(b'LSB1' + table[4:])
check('not an LSB2 table', voice(bad)[0] == 0)
(bad / 'RU' / 'VoiceSubtitles.str').write_bytes(table[:60])
check('truncated table', voice(bad)[0] == 0)
check('no table', voice(work / 'none')[0] == 0)

real = ''
if len(sys.argv) > 2:
    source = pathlib.Path(sys.argv[2])
    (work / 'real' / 'RU').mkdir(parents=True)
    (work / 'real' / 'RU' / 'VoiceSubtitles.str').write_bytes(source.read_bytes())
    count = voice(work / 'real')[0]
    check('real table', count > 0, source)
    real = ' %s: %d lines.' % (source.name, count)

temporary.cleanup()
if failed:
    sys.exit(1)
print('PASS: [Language] Custom is off by default and takes only a folder name; a pack needs '
      'language.ini with a game language Slot and resolves to game:\\languages\\<id>\\; '
      'VoiceSubtitles.str is read loose or stored in the zip, with times from <id>~t.' + real)
