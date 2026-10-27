"""Build fh2-monitor.dll (FPS counter, fh2-perf.csv). Never installs files.

Usage: python monitor/build-monitor.py /path/to/zig.exe [--output DIR]
Default output: out/monitor/fh2-monitor.dll. Install it as <game>/monitor/fh2-monitor.dll.
"""
import argparse
import pathlib
import subprocess

HERE = pathlib.Path(__file__).resolve().parent
SOURCE = HERE.parent

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('zig', type=pathlib.Path)
parser.add_argument('--output', type=pathlib.Path, default=SOURCE / 'out/monitor')
args = parser.parse_args()
output = args.output.resolve()
output.mkdir(parents=True, exist_ok=True)
subprocess.run([str(args.zig.resolve()), 'c++', '-target', 'x86_64-windows-gnu', '-std=c++17', '-O2',
                '-Wall', '-Wextra', '-Werror', '-Wno-missing-field-initializers',
                '-D_WINDOWS', '-DWIN32_LEAN_AND_MEAN', '-DNOMINMAX', '-shared',
                str(HERE / 'monitor.cpp'),
                '-ld3d12', '-ldxgi', '-ldxguid', '-ld3dcompiler_47',
                '-o', str(output / 'fh2-monitor.dll')], check=True)
for leftover in ('fh2-monitor.lib', 'monitor.lib', 'fh2-monitor.pdb'):
    (output / leftover).unlink(missing_ok=True)
print(output / 'fh2-monitor.dll')
