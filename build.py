"""Build the native mod with Zig 0.15.2, including cross-compilation on Linux.
Usage: python build.py /path/to/zig [--azimuth [--fh3]] [--test] [--output PATH]
"""

import argparse
from concurrent.futures import ThreadPoolExecutor
import hashlib
import json
import os
from pathlib import Path
import subprocess
import re
import struct
import time
SOURCE = Path(__file__).resolve().parent
# Shared sources live at the top level; title-specific ones in fh2/ and fh3/.
INCLUDE_DIRS = ["", "fh2", "fh3"]
import sys
sys.path.insert(0, str(SOURCE / "fh2"))
from generate_data import generate  # noqa: E402
COMMON_SOURCES = [
    "dllmain.c",
    "fh2/runtime.c",
    "fh2/settings.c",
    "support.c",
    "fh2/logging.c",
    "fh2/patch_engine.c",
    "fh2/verification.c",
    "sha256.c",
    "fh2/patches_generated.c",
    "fh2/features/garage.c",
    "fh2/features/road_placement.c",
    "fh2/features/gamerpic.c",
    "fh2/features/treasure_map.c",
    "fh2/features/minimap.c",
    "fh2/features/rims.c",
    "fh2/features/engine_swaps.c",
    "fh2/features/transmission_swaps.c",
    "fh2/features/return_garage.c",
    "fh2/features/stock_wheels.c",
    "fh2/features/wheel_variants.c",
    "fh2/features/world_drivatars.c",
    "fh2/features/local_liveries.c",
    "fh2/features/plugins.c",
    "fh2/features/display_units.c",
    "fh2/features/drift_camera.c",
    "fh2/features/video_settings.c",
    "fh2/features/camera_fov.c",
    "fh2/features/video_options.c",
    "fh2/features/video_options_text.c",
    "fh2/features/video_options_xaml.c",
    "fh2/features/photo_sampling.c",
    "fh2/features/sun_shadows.c",
    "fh2/features/livery.c",
    "fh2/features/media_overrides.c",
    "fh2/features/media_files.c",
    "fh2/features/video_redirect.c",
    "fh2/features/showroom_photo.c",
    "fh2/features/manufacturer_jump.c",
    "fh2/features/presentation_controls.c",
    "fh2/features/driver_options.c",
    "fh2/features/photo_environment.c",
    "fh2/features/audio_volume.c",
    "fh2/features/pause_menu.c",
    "fh2/features/addon_json.c",
    "fh2/features/carbin_patch.c",
    "fh2/features/addon_database.c",
    "fh2/features/addons.c",
    "fh2/features/addon_archives.c",
    "fh2/features/addon_strings.c",
    "fh2/features/addon_wheels.c",
    "fh2/features/vehicle_controls.c",
    "fh2/features/headlight_beams.c",
    "fh2/features/blinkers.c",
    "fh2/features/aero_tuning.c",
    "fh2/features/roof_fade.c",
    "fh2/features/roof_aero.c",
    "fh2/features/speed_zone.c",
    "fh2/features/autodrive.c",
    "fh2/features/game_input.c",
    "fh2/features/bindings.c",
    "fh2/features/offline_events.c",
    "fh2/features/custom_showcases.c",
    "fh2/features/offline_event_division.c",
    "fh2/features/offline_event_setup.c",
    "fh2/features/autodrive_game.c",
    "fh2/features/autodrive_notice.c",
    "fh2/features/autodrive_ui.c",
    "fh2/features/ui_controls.c",
    "fh2/features/png_validation.c",
    "fh2/features/png_decode.c",
]
FM5_SOURCES = ["fh2/features/fm5.c", "fh2/features/fm5_rims.c"]
# The FH3 build is a separate, self-contained title profile: none of the FH2
# runtime, settings, plugins or save compatibility modules are linked.
FH3_SOURCES = ["dllmain.c", "support.c", "sha256.c", "fh3/fh3.c", "fh3/fh3_mods.c",
               "fh3/fh3_text.c"]
AZIMUTH_SOURCES = ["adapters/azimuth.c", "adapters/winmm.c", "adapters/winmm_exports.s",
                   "adapters/azimuth_host.c"]


def parse_arguments():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("zig", type=Path)
    parser.add_argument("--test", action="store_true")
    parser.add_argument("--azimuth", action="store_true", help="Build the Azimuth winmm proxy")
    parser.add_argument("--fm5", action="store_true", help="Opt into the unfinished FM5 title profile (Azimuth only)")
    parser.add_argument("--fh3", action="store_true", help="Build the separate Forza Horizon 3 DLL (Azimuth only)")
    parser.add_argument("--output", type=Path)
    parser.add_argument("--build-dir", type=Path)
    parser.add_argument("--jobs", type=int, default=min(8, os.cpu_count() or 1))
    parser.add_argument("--rebuild", action="store_true", help="Ignore cached build inputs")
    arguments = parser.parse_args()
    if arguments.jobs < 1:
        parser.error("--jobs must be positive")
    if arguments.fm5 and not arguments.azimuth:
        parser.error("--fm5 requires --azimuth")
    if arguments.fh3 and not arguments.azimuth:
        parser.error("--fh3 requires --azimuth")
    if arguments.fh3 and arguments.fm5:
        parser.error("--fh3 cannot be combined with --fm5")
    return arguments


def compiler_arguments(compiler, options):
    arguments = [
        str(compiler),
        "clang",
        "-target",
        "x86_64-w64-windows-gnu",
        "-c",
        "-ffreestanding",
        "-fno-builtin",
        "-fno-stack-protector",
        "-fms-extensions",
        "-funwind-tables",
        "-O2",
        "-Wall",
        "-Wextra",
        "-Werror",
        *[item for folder in INCLUDE_DIRS for item in ("-I", str(SOURCE / folder))],
        "-isystem",
        str(compiler.parent / "lib/include"),
        "-isystem",
        str(compiler.parent / "lib/libc/include/any-windows-any"),
    ]
    if options.test:
        arguments.append("-DFH2_TWEAKS_TEST=1")
    if options.azimuth:
        arguments.append("-DFH2_AZIMUTH=1")
    if options.fm5:
        arguments.append("-DFH2_ENABLE_FM5=1")
    if options.fh3:
        arguments.append("-DFH2_BUILD_FH3=1")
    return arguments


def version_resource(filename, fm5=False, fh3=False):
    version = re.search(r'#define MOD_VERSION "([\d.]+)"',
                        (SOURCE / "extra_options.h").read_text()).group(1)
    manifest_version = json.loads((SOURCE / "fh2" / "patches.json").read_text())["version"]
    if version != manifest_version:
        raise ValueError("Version mismatch between extra_options.h and patches.json")
    numbers = [int(part) for part in version.split(".")]
    numbers += [0] * (4 - len(numbers))

    def padded(data):
        return data + bytes(-len(data) % 4)

    def wide(text):
        return (text + "\0").encode("utf-16le")

    def block(key, value=b"", children=b"", text=False):
        header = padded(struct.pack("<HHH", 0, len(value) // 2 if text else len(value), int(text)) + wide(key))
        data = header + value
        if children:
            data = padded(data) + children
        return struct.pack("<H", len(data)) + data[2:]

    title = ("Forza Horizon 3 Extra Options" if fh3 else
             "Forza Horizon 2 / Motorsport 5 Extra Options" if fm5 else "Forza Horizon 2 Extra Options")
    comments = ("Unofficial in-process game mod. Forza Horizon 3 only: 4CE1DFAB." if fh3 else
                "Unofficial in-process game mod. FH2: 1A96545B. FM5: 7B35D917 (Azimuth only)." if fm5 else
                "Unofficial in-process game mod. Forza Horizon 2 only: 1A96545B.")
    strings = {"FileDescription": title + " - " + ("Azimuth adapter" if filename == "winmm.dll" else "Mezcal adapter"),
               "FileVersion": version, "InternalName": "FH3_ExtraOptions" if fh3 else "FH2_ExtraOptions",
               "OriginalFilename": filename, "ProductName": title,
               "ProductVersion": version, "LegalCopyright": "Arthur (RokkuDayo)",
               "CompanyName": "Arthur (RokkuDayo)",
               "Comments": comments}
    entries = b"".join(padded(block(key, wide(value), text=True)) for key, value in strings.items())
    string_info = block("StringFileInfo", children=block("040904b0", children=entries))
    variable_info = block("VarFileInfo", children=block("Translation", struct.pack("<HH", 0x409, 1200)))
    high, low = numbers[0] << 16 | numbers[1], numbers[2] << 16 | numbers[3]
    fixed = struct.pack("<13I", 0xfeef04bd, 0x10000, high, low, high, low,
                        0x3f, 0, 0x40004, 2, 0, 0, 0)
    data = block("VS_VERSION_INFO", fixed, padded(string_info) + variable_info)
    null_header = struct.pack("<IIHHHHIHHII", 0, 32, 0xffff, 0, 0xffff, 0, 0, 0, 0, 0, 0)
    header = struct.pack("<IIHHHHIHHII", len(data), 32, 0xffff, 16, 0xffff, 1, 0, 0x30, 0x409, 0, 0)
    return null_header + header + padded(data)


def input_signature(command, paths, compiler):
    digest = hashlib.sha256()
    digest.update(json.dumps(command).encode())
    for path in sorted(set(paths)):
        digest.update(str(path).encode())
        digest.update(path.read_bytes())
    tool = compiler.stat()
    digest.update(f"{compiler}:{tool.st_size}:{tool.st_mtime_ns}".encode())
    return digest.hexdigest()


def project_dependencies(path, seen=None):
    seen = set() if seen is None else seen
    if path in seen:
        return seen
    seen.add(path)
    for name in re.findall(r'^\s*#\s*include\s*"([^"]+)"', path.read_text(encoding="utf-8"), re.M):
        candidates = [path.parent / name, *(SOURCE / folder / name for folder in INCLUDE_DIRS)]
        included = next((c for c in candidates if c.is_file()), None)
        if included:
            project_dependencies(included.resolve(), seen)
    return seen


def cached_command(command, inputs, target, compiler, rebuild):
    stamp = target.with_suffix(target.suffix + ".inputs")
    signature = input_signature(command, inputs, compiler)
    if not rebuild and target.is_file() and stamp.is_file() and stamp.read_text() == signature:
        return False
    stamp.unlink(missing_ok=True)
    subprocess.run(command, check=True)
    stamp.write_text(signature)
    return True


def build(compiler, output, directory, options):
    started = time.monotonic()
    directory.mkdir(parents=True, exist_ok=True)
    output.parent.mkdir(parents=True, exist_ok=True)
    objects = []
    if options.fh3:
        sources = [*FH3_SOURCES, *AZIMUTH_SOURCES, "adapters/azimuth_save.c",
                   "adapters/azimuth_markers.c"]
    else:
        sources = [*COMMON_SOURCES]
        if options.fm5:
            sources.extend(FM5_SOURCES)
        sources.extend(
            [*AZIMUTH_SOURCES, "adapters/azimuth_save.c", "adapters/azimuth_markers.c"]
            if options.azimuth
            else ["fh2/adapters/mezcal.c"]
        )
    arguments = compiler_arguments(compiler, options)
    def compile_source(filename):
        object_file = directory / (Path(filename).stem + ".obj")
        command = ([str(compiler), "clang", "-target", "x86_64-w64-windows-gnu", "-c"]
                   if filename.endswith(".s") else arguments)
        command = [*command, str(SOURCE / filename), "-o", str(object_file)]
        compiled = cached_command(command, project_dependencies(SOURCE / filename), object_file,
                                  compiler, options.rebuild)
        return str(object_file), compiled

    with ThreadPoolExecutor(max_workers=options.jobs) as workers:
        results = list(workers.map(compile_source, sources))
    objects = [path for path, _ in results]
    import_library = directory / "kernel32.lib"
    cached_command(
        [
            str(compiler),
            "dlltool",
            "-m",
            "i386:x86-64",
            "-d",
            str(SOURCE / "kernel32.def"),
            "-l",
            str(import_library),
        ],
        [SOURCE / "kernel32.def"], import_library, compiler, options.rebuild,
    )
    exception_library = directory / "ntdll.lib"
    cached_command([str(compiler), "dlltool", "-m", "i386:x86-64", "-d",
                    str(SOURCE / "ntdll.def"), "-l", str(exception_library)],
                   [SOURCE / "ntdll.def"], exception_library, compiler, options.rebuild)
    resource = directory / "version.res"
    resource.write_bytes(version_resource("winmm.dll" if options.azimuth else "xg.dll", options.fm5,
                                          options.fh3))
    link_arguments = [
        str(compiler),
        "lld-link",
        "/dll",
        "/entry:DllMain",
        "/machine:x64",
        "/nodefaultlib",
        "/dynamicbase",
        "/nxcompat",
        "/Brepro",
        "/out:" + str(output),
        "/implib:" + str(directory / "extra_options.lib"),
        *objects,
        str(import_library),
        str(exception_library),
        str(resource),
    ]
    if options.azimuth:
        link_arguments.append("/def:" + str(SOURCE / "adapters/winmm_exports.def"))
    subprocess.run(link_arguments, check=True)
    print(f"Compiled {sum(compiled for _, compiled in results)}/{len(sources)} objects; "
          f"{time.monotonic() - started:.1f}s", flush=True)


def main():
    options = parse_arguments()
    generate()
    compiler = options.zig.resolve()
    filename = (
        "fh2-extra-options-test.dll"
        if options.test
        else "winmm.dll" if options.azimuth else "xg.dll"
    )
    # FH3 also ships as Azimuth's winmm.dll; keep it in its own folder so it
    # never overwrites the FH2 build.
    default_output = SOURCE.parent / "FH3" / filename if options.fh3 else SOURCE.parent / filename
    output = (options.output or default_output).resolve()
    variant = (("azimuth" if options.azimuth else "mezcal") + ("-fh3" if options.fh3 else "")
               + ("-test" if options.test else ""))
    directory = options.build_dir or SOURCE.parent / ".build" / variant
    build(compiler, output, directory.resolve(), options)
    print(output)


if __name__ == "__main__":
    main()
