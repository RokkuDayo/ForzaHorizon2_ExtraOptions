Current release: the Azimuth adapter is a version-checked winmm proxy.
See BUILD.md and AZIMUTH_NOTES.md. Historical TLS-replacement
fixtures live in history/tls-loader and are not part of the active suite.

# Development files

Release builds use the C/header modules, `build.py`, `shared/kernel32.def`, and the
generated patch tables in the parent source directory. `fh2/patches.json` and
`generate_data.py` remain there for maintaining those tables.

Everything else lives here:

- `../docs/`: build instructions, source guide and reverse-engineering notes.
- `tests/`: regression scripts and isolated native fixture harnesses.
- `tools/`: the optional Mezcal save conversion utility.
- `.clang-format`: the formatting configuration.

Run the commands in [BUILD.md](BUILD.md) from the parent source folder.
See [SOURCE_GUIDE.md](SOURCE_GUIDE.md) for the code layout. These development files
are not required to build or install a release DLL.
