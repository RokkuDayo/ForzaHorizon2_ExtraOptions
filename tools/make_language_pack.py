"""Build a custom language pack (features/custom_language.c) from a translation mod.

usage: py -I tools/make_language_pack.py <original media> <mod media> <output folder>
           --slot ru-RU --name "Українська" [--whole-zips]

<original media> is the game's untouched Mount\\media, <mod media> the mod's
replacement files laid out the same way (zips and loose files). Every zip entry
and loose file whose data differs from the original goes into <output folder>
in the mediaoverride layout (media\\X.zip\\a\\b -> X\\a\\b), so the pack ships only
what the mod changes. --whole-zips copies each changed zip whole instead.
zipmanifest.xml is never copied: the original archives stay as they are.

The output gets language.ini; install it as <game>\\Mount\\languages\\<id>\\ and
set FH2_ExtraOptions.ini [Language] Custom=<id> (the output folder's name).
"""

import argparse
import hashlib
import shutil
import sys
import zipfile
from pathlib import Path, PurePosixPath


def digest(data):
    return hashlib.sha1(data).digest()


def file_digest(path):
    h = hashlib.sha1()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 22), b""):
            h.update(chunk)
    return h.digest()


def safe_parts(name):
    parts = PurePosixPath(name.replace("\\", "/")).parts
    if not parts or any(p in ("", ".", "..") or ":" in p for p in parts):
        sys.exit(f"refusing unsafe archive path {name!r}")
    return parts


def changed_entries(original, modded):
    """[(entry name, data)] of modded entries that are new or differ from the original."""
    with zipfile.ZipFile(original) as zo:
        known = {i.filename.lower(): (i.file_size, i.CRC, i.filename) for i in zo.infolist() if not i.is_dir()}
        out = []
        with zipfile.ZipFile(modded) as zm:
            for info in zm.infolist():
                if info.is_dir():
                    continue
                data = zm.read(info)
                old = known.get(info.filename.lower())
                if old and old[:2] == (info.file_size, info.CRC) and digest(zo.read(old[2])) == digest(data):
                    continue
                out.append((info.filename, data))
    return out


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("original", type=Path)
    parser.add_argument("modded", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--slot", required=True, help="game language slot the pack takes over, e.g. ru-RU")
    parser.add_argument("--name", required=True, help="language name shown in the log")
    parser.add_argument("--whole-zips", action="store_true")
    options = parser.parse_args()
    if not options.original.is_dir() or not options.modded.is_dir():
        sys.exit("both media folders must exist")
    if options.output.exists() and any(options.output.iterdir()):
        sys.exit(f"{options.output} is not empty")
    options.output.mkdir(parents=True, exist_ok=True)
    entries = files = size = 0
    for source in sorted(p for p in options.modded.rglob("*") if p.is_file()):
        relative = source.relative_to(options.modded)
        if relative.as_posix().lower() == "zipmanifest.xml":
            continue
        original = options.original / relative
        if source.suffix.lower() == ".zip" and original.is_file() and not options.whole_zips:
            for name, data in changed_entries(original, source):
                target = options.output / relative.with_suffix("") / Path(*safe_parts(name))
                target.parent.mkdir(parents=True, exist_ok=True)
                target.write_bytes(data)
                entries += 1
                size += len(data)
            continue
        if original.is_file() and file_digest(original) == file_digest(source):
            continue
        target = options.output / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source, target)
        files += 1
        size += source.stat().st_size
    (options.output / "language.ini").write_text(
        f"; Custom language pack for Forza Horizon 2 Extra Options (features/custom_language.c).\n"
        f"; Install as <game>\\Mount\\languages\\{options.output.name}\\ and set\n"
        f"; FH2_ExtraOptions.ini [Language] Custom={options.output.name}; choose {options.slot} in the launcher.\n"
        f"[Language]\nName={options.name}\nSlot={options.slot}\n",
        encoding="utf-16")
    print(f"{entries} changed zip entries, {files} whole files, {size / 1e6:.2f} MB -> {options.output}")


if __name__ == "__main__":
    main()
