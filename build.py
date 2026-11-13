"""Build the native mod with Zig 0.15.2, including cross-compilation on Linux.
Usage: python build.py /path/to/zig [--azimuth] [--test] [--output PATH]
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
from generate_data import generate

SOURCE = Path(__file__).resolve().parent
COMMON_SOURCES = [
    "shared/boot_diagnostics.c",
    "fh2/dllmain.c",
    "fh2/runtime.c",
    "fh2/settings.c",
    "shared/support.c",
    "shared/logging.c",
    "fh2/patch_engine.c",
    "fh2/verification.c",
    "shared/sha256.c",
    "fh2/patches_generated.c",
    "fh2/garage.c",
    "fh2/road_placement.c",
    "fh2/treasure_map.c",
    "fh2/minimap.c",
    "fh2/rims.c",
    "fh2/engine_swaps.c",
    "fh2/transmission_swaps.c",
    "fh2/return_garage.c",
    "fh2/stock_wheels.c",
    "fh2/wheel_variants.c",
    "fh2/world_drivatars.c",
    "fh2/local_liveries.c",
    "fh2/designs.c",
    "shared/plugins.c",
    "fh2/display_units.c",
    "fh2/drift_camera.c",
    "fh2/video_settings.c",
    "fh2/camera_fov.c",
    "fh2/video_options.c",
    "shared/video_options_text.c",
    "fh2/video_options_xaml.c",
    "fh2/photo_sampling.c",
    "fh2/sun_shadows.c",
    "fh2/livery.c",
    "fh2/media_overrides.c",
    "fh2/loose_media_index.c",
    "shared/media_files.c",
    "fh2/video_redirect.c",
    "fh2/showroom_photo.c",
    "fh2/manufacturer_jump.c",
    "fh2/presentation_controls.c",
    "fh2/driver_options.c",
    "fh2/photo_environment.c",
    "fh2/audio_volume.c",
    "fh2/pause_menu.c",
    "shared/addon_json.c",
    "shared/carbin_patch.c",
    "shared/addon_database.c",
    "shared/addon_cache.c", "shared/addon_cache_io.c",
    "shared/addons.c",
    "shared/addon_archives.c",
    "shared/addon_strings.c",
    "shared/addon_wheels.c",
    "fh2/vehicle_controls.c",
    "fh2/launch_control.c",
    "fh2/headlight_beams.c",
    "fh2/blinkers.c",
    "fh2/aero_tuning.c",
    "fh2/roof_fade.c",
    "fh2/roof_aero.c",
    "fh2/speed_zone.c",
    "fh2/autodrive.c",
    "fh2/game_input.c",
    "fh2/bindings.c",
    "fh2/offline_events.c",
    "fh2/custom_showcases.c",
    "fh2/offline_event_division.c",
    "fh2/offline_event_setup.c",
    "fh2/autodrive_game.c",
    "fh2/autodrive_notice.c",
    "fh2/autodrive_ui.c",
    "fh2/ui_controls.c",
]
FM5_SOURCES = ["fm5/fm5.c", "fm5/fm5_mods.c", "fm5/fm5_carbin.c", "fm5/fm5_rims.c", "fm5/fm5_camera.c",
               "fm5/fm5_camera_menu.c", "fm5/fm5_manufacturer.c", "fm5/fm5_video.c", "fm5/fm5_vehicle.c", "fm5/fm5_ui.c", "fm5/fm5_settings.c", "fm5/fm5_driver.c", "fm5/fm5_audio.c", "fm5/fm5_badge.c", "fm5/fm5_start.c", "fm5/fm5_designs.c", "fm5/fm5_local_liveries.c"]

FH3_SOURCES = ["fh3/fh3.c", "fh3/fh3_mods.c", "fh3/fh3_text.c"]


def parse_arguments():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("zig", type=Path)
    parser.add_argument("--test", action="store_true")
    parser.add_argument("--boot-diagnostics", action="store_true", help="Instrument boot/mod loading in a separate diagnostic DLL")
    parser.add_argument("--azimuth", action="store_true", help="Build the Azimuth winmm proxy")
    parser.add_argument("--fm5", action="store_true", help="Opt into the unfinished FM5 title profile (Azimuth only)")
    parser.add_argument("--fh3", action="store_true", help="Include the FH3 title profile (Azimuth only)")
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
        "-I",
        str(SOURCE),
        "-isystem",
        str(compiler.parent / "lib/include"),
        "-isystem",
        str(compiler.parent / "lib/libc/include/any-windows-any"),
    ]
    if options.boot_diagnostics:
        arguments.append("-DEO_BOOT_DIAGNOSTICS=1")
    if options.test:
        arguments.append("-DFH2_TWEAKS_TEST=1")
    if options.azimuth:
        arguments.append("-DFH2_AZIMUTH=1")
    if options.fm5:
        arguments.append("-DFH2_ENABLE_FM5=1")
    if options.fh3:
        arguments.append("-DEO_ENABLE_FH3=1")
    return arguments


def version_resource(filename, fm5=False, fh3=False):
    version = re.search(r'#define MOD_VERSION "([\d.]+)"',
                        (SOURCE / "shared/extra_options.h").read_text()).group(1)
    manifest_version = json.loads((SOURCE / "fh2/patches.json").read_text())["version"]
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

    names = ["Forza Horizon 2"]
    titles = ["FH2: 1A96545B"]
    if fm5:
        names.append("Motorsport 5")
        titles.append("FM5: 7B35D917")
    if fh3:
        names.append("Horizon 3")
        titles.append("FH3: 4CE1DFAB")
    title = " / ".join(names) + " Extra Options"
    strings = {"FileDescription": title + " - " + ("Azimuth adapter" if filename == "winmm.dll" else "Mezcal adapter"),
               "FileVersion": version, "InternalName": "FH2_ExtraOptions",
               "OriginalFilename": filename, "ProductName": title,
               "ProductVersion": version, "LegalCopyright": "Arthur (RokkuDayo)",
               "CompanyName": "Arthur (RokkuDayo)",
               "Comments": "Unofficial in-process game mod. " + ". ".join(titles) + "."}
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
        included = path.parent / name
        if not included.is_file():
            included = SOURCE / name
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
    sources = [*COMMON_SOURCES]
    if options.fm5:
        sources.extend(FM5_SOURCES)
    if options.fh3:
        sources.extend(FH3_SOURCES)
    sources.extend(
        [
            "shared/adapters/azimuth.c",
            "shared/adapters/winmm.c",
            "shared/adapters/winmm_exports.s",
            "shared/adapters/azimuth_host.c",
            "shared/adapters/azimuth_markers.c",
        ]
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
            str(SOURCE / "shared/kernel32.def"),
            "-l",
            str(import_library),
        ],
        [SOURCE / "shared/kernel32.def"], import_library, compiler, options.rebuild,
    )
    exception_library = directory / "ntdll.lib"
    cached_command([str(compiler), "dlltool", "-m", "i386:x86-64", "-d",
                    str(SOURCE / "shared/ntdll.def"), "-l", str(exception_library)],
                   [SOURCE / "shared/ntdll.def"], exception_library, compiler, options.rebuild)
    resource = directory / "version.res"
    resource.write_bytes(version_resource("winmm.dll" if options.azimuth else "xg.dll", options.fm5, options.fh3))
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
        link_arguments.append("/def:" + str(SOURCE / "shared/adapters/winmm_exports.def"))
    subprocess.run(link_arguments, check=True)
    print(f"Compiled {sum(compiled for _, compiled in results)}/{len(sources)} objects; "
          f"{time.monotonic() - started:.1f}s", flush=True)


def main():
    options = parse_arguments()
    generate()
    if options.fm5:
        layout = (SOURCE / "fm5/ui/video_options.xaml").read_text(encoding="utf-8")
        header = "/* Generated from ui/video_options.xaml. */\nstatic const char video_layout[]=\n" + "\n".join(
            json.dumps(line + "\n", ensure_ascii=True) for line in layout.splitlines()) + ";\n"
        target = SOURCE / "fm5/fm5_video_layout.h"
        if not target.exists() or target.read_text(encoding="utf-8") != header:
            target.write_text(header, encoding="utf-8")
    if options.fm5:
        layout = (SOURCE / "fm5/ui/e3_start.xaml").read_text(encoding="utf-8")
        header = "/* Generated from ui/e3_start.xaml. */\nstatic const char e3_start_layout[]=\n" + "\n".join(
            json.dumps(line + "\n", ensure_ascii=True) for line in layout.splitlines()) + ";\n"
        target = SOURCE / "fm5/fm5_start_layout.h"
        if not target.exists() or target.read_text(encoding="utf-8") != header:
            target.write_text(header, encoding="utf-8")
    compiler = options.zig.resolve()
    filename = (
        "fh2-extra-options-test.dll"
        if options.test
        else "winmm.dll" if options.azimuth else "xg.dll"
    )
    default_output = SOURCE.parent / (".build" if options.test or not options.azimuth else "azimuth") / filename
    output = (options.output or default_output).resolve()
    variant = ("azimuth" if options.azimuth else "mezcal") + ("-test" if options.test else "")
    directory = options.build_dir or SOURCE.parent / ".build" / variant
    build(compiler, output, directory.resolve(), options)
    print(output)


if __name__ == "__main__":
    main()
