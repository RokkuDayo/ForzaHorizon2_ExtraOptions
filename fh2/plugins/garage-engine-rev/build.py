"""Build EO_GarageEngineRev using Python 3.10+ and Zig 0.15.2.

Usage: python build.py /path/to/zig.exe [--test]
"""
import argparse
from pathlib import Path
import subprocess
from version_resource import version_resource

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('zig', type=Path)
    parser.add_argument('--test', action='store_true', help='Add fixture exports; do not distribute this DLL')
    args = parser.parse_args()
    folder = Path(__file__).resolve().parent
    zig = args.zig.resolve()
    build = folder / 'build'
    output = folder / 'dist'
    build.mkdir(exist_ok=True)
    output.mkdir(exist_ok=True)
    resource = build / 'version.res'
    resource.write_bytes(version_resource())
    for library in ('kernel32', 'user32'):
        subprocess.run([str(zig), 'dlltool', '-m', 'i386:x86-64', '-d',
            str(folder / 'sdk' / (library + '.def')), '-l', str(build / (library + '.lib'))], check=True)
    name = 'EO_GarageEngineRev-test' if args.test else 'EO_GarageEngineRev'
    obj = build / (name + '.obj')
    dll = output / (name + '.dll')
    command = [str(zig), 'clang', '-target', 'x86_64-w64-windows-gnu', '-c',
        '-ffreestanding', '-fno-builtin', '-fno-stack-protector', '-fms-extensions',
        '-funwind-tables', '-O2', '-Wall', '-Wextra', '-Werror', '-I', str(folder / 'sdk'),
        '-isystem', str(zig.parent / 'lib/include'), '-isystem',
        str(zig.parent / 'lib/libc/include/any-windows-any')]
    if args.test:
        command.append('-DGARAGE_REV_TEST=1')
    subprocess.run(command + [str(folder / 'EO_GarageEngineRev.c'), '-o', str(obj)], check=True)
    subprocess.run([str(zig), 'lld-link', '/dll', '/entry:DllMain', '/machine:x64',
        '/nodefaultlib', '/dynamicbase', '/nxcompat', '/Brepro', '/out:' + str(dll),
        str(obj), str(resource), str(build / 'kernel32.lib'), str(build / 'user32.lib')], check=True)
    print(dll)

if __name__ == '__main__':
    main()
