# Workspace layout

The maintained source is `source/`, with game-specific implementations in
`fh2/`, `fm5/` and `fh3/`, common code in `shared/`, and settings templates in `config/`.
All core documentation is in `source/docs/`. Tests, private compatibility
fixtures and authoring tools are in `source/development/`; examples are in
`source/examples/`.

The latest Azimuth development build and game INIs are in `azimuth/`.
It includes all three title profiles. Build from the workspace root:

```text
python source/build.py toolchain/zig-x86_64-windows-0.15.2/zig.exe --azimuth --fm5 --fh3
```

Use `--test --output .build/extra-options-test.dll` for the private regression
DLL. Build objects, test output and validation reports live in `.build/`.
An FH2-only release omits `--fm5` and `--fh3`. Mezcal is built only when requested.

Standalone plugin projects are in `plugins/`. The garage rev plugin retains
its self-contained source/SDK and latest DLL/INI under `plugins/garage-engine-rev`.
`tools/` contains the standalone texture conversion utility. The combined
database/carbin/string diff tool is `source/development/tools/Mod Diff.cmd`.
`toolchain/` holds the local compiler, not another copy of project source.

## Mod caches

New loader-owned files are grouped beneath `mods/Cache` beside Extra Options:

| Folder | Contents |
|---|---|
| `fh2-db` | FH2 merged database cache |
| `fm5-db` | FM5 merged database cache |
| `fh2-carbins` | FH2 generated carbin files, deleted when native file handles close |
| `fm5-carbins` | FM5 generated carbin files |

Disabling `DatabaseCache` disables persistent database reuse; generated carbin
files still use their game's folder. Existing installed cache locations are
neither migrated nor removed. The game's own livery cache is independent.

FM5's canonical folder override setting is `[Files] EnableMediaOverrides`,
matching FH2. The former `MediaOverrides` key is accepted only as a fallback.

## Manufacturer tile update

Names stay left aligned in a 222-pixel text area. Long names are trimmed once
at popup construction using FM5 font advance metrics and literal trailing `...`.
There is no autoscrolling timer or popup update hook. Full localized names remain
available for sorting and navigation. Aftermarket logos use a 120-pixel image
clipped by a 104-pixel viewport. Stock-rim and car-brand styles are unchanged.
See FM5_MANUFACTURER_TEXT.md for details.
