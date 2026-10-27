"""Offline check of "Saves in Documents" (adapters/azimuth_saves.c) and "Photos as
JPG" (adapters/azimuth_photos.c) with the --test build.

Usage: py tools/test-documents-saves.py TEST_DLL
Works only in a temporary folder: never reads or writes a real saves folder,
or the real Documents folder (the Documents known folder is only looked up,
not opened).
"""
import ctypes
import ctypes.wintypes as wt
import datetime
import hashlib
import os
import pathlib
import sys
import tempfile

dll = ctypes.WinDLL(str(pathlib.Path(sys.argv[1]).resolve()))
k32 = ctypes.WinDLL('kernel32', use_last_error=True)
dll.Fh2TestSavesConfigure.argtypes = [ctypes.c_wchar_p, ctypes.c_wchar_p, ctypes.c_wchar_p,
                                      ctypes.c_int]
dll.Fh2TestSavesHook.argtypes = [ctypes.c_char_p]
dll.Fh2TestSavesHook.restype = ctypes.c_void_p
dll.Fh2TestSavesMap.argtypes = [ctypes.c_wchar_p, ctypes.c_wchar_p, ctypes.c_uint, ctypes.c_int]
dll.Fh2TestSavesRedirectPath.argtypes = [ctypes.c_wchar_p]
dll.Fh2TestSavesRoot.argtypes = [ctypes.c_wchar_p, ctypes.c_uint]
dll.Fh2TestSavesOption.argtypes = [ctypes.c_wchar_p]
dll.Fh2TestDocumentsFolder.argtypes = [ctypes.c_wchar_p, ctypes.c_uint]
dll.Fh2TestReadOptions.argtypes = [ctypes.c_wchar_p, ctypes.c_void_p]
dll.Fh2TestPhotosConfigure.argtypes = [ctypes.c_wchar_p, ctypes.c_wchar_p, ctypes.c_wchar_p]
dll.Fh2TestPhotosOption.argtypes = [ctypes.c_wchar_p]


def hook(name, restype, *argtypes):
    return ctypes.WINFUNCTYPE(restype, *argtypes)(dll.Fh2TestSavesHook(name.encode()))


CreateFileW = hook('CreateFileW', wt.HANDLE, wt.LPCWSTR, wt.DWORD, wt.DWORD, ctypes.c_void_p,
                   wt.DWORD, wt.DWORD, wt.HANDLE)
GetFileAttributesW = hook('GetFileAttributesW', wt.DWORD, wt.LPCWSTR)
CreateDirectoryW = hook('CreateDirectoryW', wt.BOOL, wt.LPCWSTR, ctypes.c_void_p)
RemoveDirectoryW = hook('RemoveDirectoryW', wt.BOOL, wt.LPCWSTR)
DeleteFileW = hook('DeleteFileW', wt.BOOL, wt.LPCWSTR)
MoveFileExW = hook('MoveFileExW', wt.BOOL, wt.LPCWSTR, wt.LPCWSTR, wt.DWORD)
CopyFileW = hook('CopyFileW', wt.BOOL, wt.LPCWSTR, wt.LPCWSTR, wt.BOOL)
FindFirstFileExW = hook('FindFirstFileExW', wt.HANDLE, wt.LPCWSTR, ctypes.c_int, ctypes.c_void_p,
                        ctypes.c_int, ctypes.c_void_p, wt.DWORD)
GetFinalPath = hook('GetFinalPathNameByHandleW', wt.DWORD, wt.HANDLE, wt.LPWSTR, wt.DWORD, wt.DWORD)
SetInfo = hook('SetFileInformationByHandle', wt.BOOL, wt.HANDLE, ctypes.c_int, ctypes.c_void_p, wt.DWORD)
GetInfo = hook('GetFileInformationByHandleEx', wt.BOOL, wt.HANDLE, ctypes.c_int, ctypes.c_void_p, wt.DWORD)
k32.CloseHandle.argtypes = [wt.HANDLE]
k32.FindClose.argtypes = [wt.HANDLE]
k32.CreateFileW.argtypes = [wt.LPCWSTR, wt.DWORD, wt.DWORD, ctypes.c_void_p, wt.DWORD, wt.DWORD,
                            wt.HANDLE]
k32.CreateFileW.restype = wt.HANDLE
INVALID = ctypes.c_void_p(-1).value
start = os.getcwd()


def mapped(path, reverse=0):
    out = ctypes.create_unicode_buffer(4096)
    return out.value if dll.Fh2TestSavesMap(path, out, 4096, reverse) else None


def tree(root):
    return sorted((str(p.relative_to(root)), p.stat().st_size) for p in root.rglob('*') if p.is_file())


def contents(root):
    return sorted((str(p.relative_to(root)), p.read_bytes()) for p in root.rglob('*') if p.is_file())


def make_saves(root, sizes, stamp=None):
    revision = root / 'player-1' / 'User_9000000000000' / 'revisions' / '0000000000000a29'
    (revision / 'data').mkdir(parents=True)
    for file, size in sizes:
        (revision / file).write_bytes(os.urandom(size))
        if stamp:
            os.utime(revision / file, (stamp, stamp))
    return revision


def known_documents():
    folder_id = (ctypes.c_byte * 16).from_buffer_copy(
        bytes.fromhex('d09ad3fd8f23af46adb46c85480369c7'))
    path = ctypes.c_wchar_p()
    shell = ctypes.WinDLL('shell32')
    if shell.SHGetKnownFolderPath(ctypes.byref(folder_id), 0, None, ctypes.byref(path)) != 0:
        return None
    value = path.value
    ctypes.WinDLL('ole32').CoTaskMemFree(path)
    return value


FILES = [('commit', 463), ('data/SaveState', 118284), ('data/PlayerDatabase', 3 << 20),
         ('data/VersionFlags', 4)]

with tempfile.TemporaryDirectory(prefix='docs-saves-') as name:
    base = pathlib.Path(name)
    game = base / 'FH2' / 'data' / 'titles' / '1A96545B' / 'saves'
    docs = base / 'Documents' / 'My Games' / 'Forza Horizon 2' / 'Saves' / '1A96545B'
    log = base / 'FH2_ExtraOptions.log'
    revision = make_saves(game, FILES)
    original = tree(game)
    docs.mkdir(parents=True)    # empty Documents folder (left by an earlier try) is replaced

    # 1. Migration from the game folder: copy, verify, rename into place; the source stays.
    assert dll.Fh2TestSavesConfigure(str(game), str(docs), str(log), 1) == 1
    assert tree(docs) == original and tree(game) == original
    for a, b in zip(sorted(game.rglob('*')), sorted(docs.rglob('*'))):
        if a.is_file():
            assert a.read_bytes() == b.read_bytes() and int(a.stat().st_mtime) == int(b.stat().st_mtime)
    assert not list(docs.parent.glob('*.migrating-*'))
    text = log.read_text(encoding='utf-8')
    assert 'SAVES: migration source: the game-folder saves' in text, text
    assert 'SAVES: migrated 4 files' in text and 'from the game folder ' in text, text
    assert 'count and sizes verified' in text, text

    # 2. Path mapping: plain, \\?\, \??\, slashes, case, relative, the root itself, look-alikes.
    g, a = str(game), str(docs)
    assert mapped(g + r'\player-1\x') == a + r'\player-1\x'
    assert mapped(g) == a and mapped(g + '\\') == a + '\\'
    assert mapped(g.upper().replace('\\', '/') + '/p/q') == a + r'\p\q'
    assert mapped('\\\\?\\' + g + r'\p') == '\\\\?\\' + a + r'\p'
    assert mapped('\\??\\' + g + r'\p') == '\\??\\' + a + r'\p'
    assert mapped(g + 'X\\p') is None and mapped(g[:-1]) is None and mapped(str(base / 'saves')) is None
    os.chdir(game.parent)
    assert mapped(r'saves\player-1') == a + r'\player-1'
    assert mapped(r'..\1A96545B\SAVES') == a
    os.chdir(start)
    long_tail = '\\' + '\\'.join(['d' * 50] * 5)
    assert mapped(g + long_tail).startswith('\\\\?\\' + a)
    # Back to the game path for Azimuth: DOS, \\?\ DOS, volume-relative, volume GUID, \Device.
    assert mapped('\\\\?\\' + a + r'\p', 1) == '\\\\?\\' + g + r'\p'
    assert mapped(a + r'\p', 1) == g + r'\p'
    assert mapped(a[2:] + r'\p', 1) == g[2:] + r'\p'
    assert mapped('\\\\?\\Volume{0000-11}' + a[2:], 1) == '\\\\?\\Volume{0000-11}' + g[2:]
    assert mapped('\\Device\\HarddiskVolume3' + a[2:] + r'\p', 1) == '\\Device\\HarddiskVolume3' + g[2:] + r'\p'
    assert mapped(a + 'X', 1) is None
    # The mod's own reader ([Achievements] Saves).
    reader = ctypes.create_unicode_buffer(g + r'\player-1', 32768)
    assert dll.Fh2TestSavesRedirectPath(reader) == 1 and reader.value == a + r'\player-1'

    # 3. The hooks as Azimuth would call them.
    folder = g + r'\player-1\new'
    assert CreateDirectoryW(folder, None) and (docs / 'player-1' / 'new').is_dir()
    assert not (game / 'player-1' / 'new').exists()
    handle = CreateFileW(folder + r'\file.bin', 0x40000000 | 0x80000000 | 0x00010000, 7, None, 2,
                         0x80, None)
    assert handle != INVALID
    assert (docs / 'player-1' / 'new' / 'file.bin').is_file()
    final = ctypes.create_unicode_buffer(1024)
    length = GetFinalPath(handle, final, 1024, 0)
    assert final.value.lower() == ('\\\\?\\' + folder + r'\file.bin').lower(), final.value
    assert length == len(final.value) and GetFinalPath(handle, final, 5, 0) == length + 1
    info = ctypes.create_string_buffer(4 + 2048)
    assert GetInfo(handle, 2, info, len(info))
    size = int.from_bytes(info.raw[:4], 'little')
    assert info.raw[4:4 + size].decode('utf-16le').lower() == (g[2:] + r'\player-1\new\file.bin').lower()
    # Rename by handle to an NT path in the game folder, as SetFileInformationByHandle takes it.
    target = '\\??\\' + folder + r'\renamed.bin'
    rename = ctypes.create_string_buffer(20 + 2 * len(target) + 2)
    ctypes.memmove(ctypes.byref(rename, 16), (len(target) * 2).to_bytes(4, 'little'), 4)
    ctypes.memmove(ctypes.byref(rename, 20), target.encode('utf-16le'), 2 * len(target))
    assert SetInfo(handle, 3, rename, len(rename)), ctypes.get_last_error()
    k32.CloseHandle(handle)
    assert (docs / 'player-1' / 'new' / 'renamed.bin').is_file()
    assert GetFileAttributesW(folder + r'\renamed.bin') != 0xFFFFFFFF
    data = ctypes.create_string_buffer(1024)
    find = FindFirstFileExW(folder + r'\*', 0, data, 0, None, 0)
    assert find != INVALID
    k32.FindClose(find)
    assert MoveFileExW(folder + r'\renamed.bin', folder + r'\moved.bin', 1)
    assert CopyFileW(folder + r'\moved.bin', folder + r'\copy.bin', 1)
    assert DeleteFileW(folder + r'\moved.bin') and DeleteFileW(folder + r'\copy.bin')
    assert RemoveDirectoryW(folder) and not (docs / 'player-1' / 'new').exists()
    assert tree(game) == original and tree(docs) == original

    # 4. Documents and the game folder hold saves: Documents wins, the newer copy is
    #    reported, nothing is copied.
    (revision / 'commit').write_bytes(b'newer')
    os.utime(revision / 'commit', (2_000_000_000, 2_000_000_000))
    log.unlink()
    docs_before = contents(docs)
    assert dll.Fh2TestSavesConfigure(str(game), str(docs), str(log), 1) == 1
    text = log.read_text(encoding='utf-8')
    assert 'using the Documents saves (4 files' in text and 'is ignored and left untouched' in text, text
    assert 'WARNING: the game-folder saves were written after the Documents ones' in text, text
    assert contents(docs) == docs_before

    # 5. No saves anywhere yet: the Documents folder is made and used.
    fresh_game = base / 'G2' / 'saves'
    fresh_docs = base / 'D2' / 'Forza Horizon 2' / 'Saves' / '1A96545B'
    fresh_docs.parent.mkdir(parents=True)
    assert dll.Fh2TestSavesConfigure(str(fresh_game), str(fresh_docs), str(log), 1) == 1
    assert fresh_docs.is_dir() and not fresh_game.exists()
    text = log.read_text(encoding='utf-8')
    assert 'no saves in the game folder or Documents yet' in text, text

    # 6. Off (not configured): nothing is mapped.
    assert dll.Fh2TestSavesConfigure(None, None, None, 0) == 0
    assert mapped(g + r'\p') is None and mapped(a + r'\p', 1) is None

    # 7. FH2_ExtraOptions.ini [Saves] InDocuments and [Photos] ExportJpg (both off by default).
    options = ctypes.create_string_buffer(1 << 16)
    ini = base / 'FH2_ExtraOptions.ini'
    for body, expected in [(b'[Saves]\nInDocuments=1\n', 1), (b'[Saves]\nInDocuments=0\n', 1),
                           (b'[Saves]\nInDocuments=2\n', 0), (b'[Saves]\nOther=1\n', 0),
                           (b'[Photos]\nExportJpg=0\n', 1), (b'[Photos]\nExportJpg=1\n', 1),
                           (b'[Photos]\nExportJpg=2\n', 0), (b'[Photos]\nOther=1\n', 0)]:
        ini.write_bytes(body)
        assert dll.Fh2TestReadOptions(str(base), options) == expected, body
    for body, expected in [(b'', 0), (b'[Saves]\nInDocuments=1\n', 1), (b'[Saves]\nInDocuments=0\n', 0)]:
        ini.write_bytes(body)
        assert dll.Fh2TestSavesOption(str(ini)) == expected, body
    for body, expected in [(b'', 0), (b'[Photos]\nExportJpg=0\n', 0), (b'[Photos]\nExportJpg=1\n', 1)]:
        ini.write_bytes(body)
        assert dll.Fh2TestPhotosOption(str(ini)) == expected, body

    # 8. The Documents known folder (looked up only).
    found = ctypes.create_unicode_buffer(4096)
    assert dll.Fh2TestDocumentsFolder(found, 4096) == 1
    assert found.value == known_documents(), found.value

    # 9. Photos as JPG.
    saves = base / 'PS' / 'saves'
    photos = base / 'PD' / 'My Games' / 'Forza Horizon 2' / 'Photos'
    photo_log = base / 'photos.log'

    def jpeg(size):
        return b'\xff\xd8\xff\xe0\x00\x10JFIF\x00\x01' + os.urandom(size) + b'\xff\xd9'

    def photo(player, container, rev, image, commit=True, commit_image=None):
        folder = saves / player / container / 'revisions' / rev
        (folder / 'data').mkdir(parents=True)
        blobs = {'header': os.urandom(106), 'image': image, 'metadata': os.urandom(60),
                 'thumb': jpeg(500)}
        for blob, value in blobs.items():
            (folder / 'data' / blob).write_bytes(value)
        os.utime(folder / 'data' / 'image', (1_760_000_000, 1_760_000_000))
        if commit:
            listed = dict(blobs, image=commit_image if commit_image is not None else image)
            lines = ''.join(f'{hashlib.sha256(v).hexdigest()} {len(v)} {k}\n' for k, v in listed.items())
            (folder / 'commit').write_bytes(
                f'Azimuth complete save revision v2\nblobs {len(listed)}\n{lines}'.encode())
        return folder

    def local_name(stamp, suffix=''):
        when = datetime.datetime.strptime(stamp, '%Y%m%d%H%M%S').replace(tzinfo=datetime.timezone.utc)
        return 'FH2 ' + when.astimezone().strftime('%Y-%m-%d %H-%M-%S') + suffix + '.jpg'

    first = jpeg(200_000)
    photo('player-1', 'Photo_0000_20261015203217', '0000000000000001', first)
    same_second = jpeg(150_000)
    photo('player-1', 'Photo_0008_20261015203217', '0000000000000001', same_second)
    pending = jpeg(90_000)
    pending_folder = photo('player-1', 'Photo_0001_20261016101500', '0000000000000001', pending,
                           commit=False)
    old_rev, new_rev = jpeg(80_000), jpeg(81_000)
    photo('player-1', 'Photo_0002_20261017120000', '0000000000000001', old_rev)
    photo('player-1', 'Photo_0002_20261017120000', '0000000000000002', new_rev)
    photo('player-1', 'Photo_0002_20261017120000', '0000000000000003', jpeg(82_000), commit=False)
    photo('player-1', 'Photo_0003_20261018000000', '0000000000000001', b'\x89PNG' + os.urandom(5000))
    photo('player-1', 'Photo_0004_20261018000001', '0000000000000001', jpeg(5000),
          commit_image=jpeg(5000))  # SHA-256 differs from the commit
    second_player = jpeg(70_000)
    photo('player-2', 'Photo_0000_20260101000000', '0000000000000001', second_player)
    (saves / 'player-1' / 'User_9000000000000').mkdir()
    photos.mkdir(parents=True)
    (photos / 'FH2 old.jpg.fh2-export.tmp').write_bytes(b'cut short')
    saves_before = contents(saves)

    assert dll.Fh2TestPhotosConfigure(str(saves), str(photos), str(photo_log)) == 1
    assert not (photos / 'FH2 old.jpg.fh2-export.tmp').exists()
    assert dll.Fh2TestPhotosScan() == 4
    dll.Fh2TestPhotosSummary()
    exported = {p.name: p.read_bytes() for p in photos.iterdir()}
    expected = {local_name('20261015203217'): first,
                local_name('20261015203217', ' (0008)'): same_second,
                local_name('20261017120000'): new_rev,
                local_name('20260101000000'): second_player}
    assert exported == expected, sorted(exported)
    assert int((photos / local_name('20261015203217')).stat().st_mtime) == 1_760_000_000
    assert dll.Fh2TestPhotosScan() == 0  # remembered
    # The pending photo is committed later: the next pass exports it.
    blobs = {b: (pending_folder / 'data' / b).read_bytes() for b in ('header', 'image', 'metadata', 'thumb')}
    lines = ''.join(f'{hashlib.sha256(v).hexdigest()} {len(v)} {k}\n' for k, v in blobs.items())
    (pending_folder / 'commit').write_bytes(f'Azimuth complete save revision v2\nblobs 4\n{lines}'.encode())
    saves_before = contents(saves)
    assert dll.Fh2TestPhotosScan() == 1
    assert (photos / local_name('20261016101500')).read_bytes() == pending
    for _ in range(5):
        assert dll.Fh2TestPhotosScan() == 0
    text = photo_log.read_text(encoding='utf-8')
    assert text.count('PHOTOS: exported ') == 5, text
    assert 'PHOTOS: first scan: 4 photos, 4 exported now, 0 already in the Photos folder.' in text, text
    assert 'Photo_0003_20261018000000 not exported: data\\image is not a JPEG' in text, text
    assert 'Photo_0004_20261018000001 not exported: data\\image does not match the SHA-256' in text, text
    assert text.count('PHOTOS: ERROR:') == 2, text
    # A restart: nothing is written again (same-size files are left alone).
    stamps = {p.name: p.stat().st_mtime_ns for p in photos.iterdir()}
    assert dll.Fh2TestPhotosConfigure(str(saves), str(photos), str(photo_log)) == 1
    assert dll.Fh2TestPhotosScan() == 0
    dll.Fh2TestPhotosSummary()
    assert {p.name: p.stat().st_mtime_ns for p in photos.iterdir()} == stamps
    assert 'PHOTOS: first scan: 5 photos, 0 exported now, 5 already' in photo_log.read_text(encoding='utf-8')
    # A different file already holds a name: it is never overwritten.
    (photos / local_name('20260101000000')).write_bytes(b'mine')
    assert dll.Fh2TestPhotosConfigure(str(saves), str(photos), str(photo_log)) == 1
    assert dll.Fh2TestPhotosScan() == 1
    assert (photos / local_name('20260101000000')).read_bytes() == b'mine'
    assert (photos / local_name('20260101000000', ' (0000)')).read_bytes() == second_player
    # The saves were only read; deleting a photo in game keeps its JPG.
    assert contents(saves) == saves_before
    import shutil
    shutil.rmtree(saves / 'player-1' / 'Photo_0000_20261015203217')
    assert dll.Fh2TestPhotosScan() == 0 and (photos / local_name('20261015203217')).read_bytes() == first
    assert not list(photos.glob('*.tmp'))

print('PASS: Documents saves: migration from the game folder copies and verifies (source kept), plain/'
      '\\\\?\\/\\??\\/relative paths map, final-path and FileNameInfo map back, CreateFile/'
      'CreateDirectory/rename/move/copy/delete/remove/find hooks redirect, Documents wins when '
      'both hold saves, [Saves] InDocuments and [Photos] ExportJpg parse (off by default), Documents '
      'known folder resolves. Photos: committed JPEGs exported once (newest committed revision, '
      'local-time names, collision suffix, never overwritten, bad JPEG/SHA-256 refused, saves untouched).')
