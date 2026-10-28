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
    "dllmain.c",
    "runtime.c",
    "settings.c",
    "support.c",
    "logging.c",
    "patch_engine.c",
    "verification.c",
    "sha256.c",
    "patches_generated.c",
    "features/garage.c",
    "features/road_placement.c",
    "features/gamerpic.c",
    "features/treasure_map.c",
    "features/minimap.c",
    "features/rims.c",
    "features/engine_swaps.c",
    "features/transmission_swaps.c",
    "features/return_garage.c",
    "features/stock_wheels.c",
    "features/wheel_variants.c",
    "features/world_drivatars.c",
    "features/local_liveries.c",
    "features/plugins.c",
    "features/display_units.c",
    "features/drift_camera.c",
    "features/video_settings.c",
    "features/camera_fov.c",
    "features/video_options.c",
    "features/video_options_text.c",
    "features/video_options_xaml.c",
    "features/photo_sampling.c",
    "features/sun_shadows.c",
    "features/livery.c",
    "features/media_overrides.c",
    "features/media_files.c",
    "features/video_redirect.c",
    "features/showroom_photo.c",
    "features/manufacturer_jump.c",
    "features/presentation_controls.c",
    "features/driver_options.c",
    "features/photo_environment.c",
    "features/audio_volume.c",
    "features/pause_menu.c",
    "features/addon_json.c",
    "features/carbin_patch.c",
    "features/addon_database.c",
    "features/addons.c",
    "features/addon_archives.c",
    "features/addon_strings.c",
    "features/addon_wheels.c",
    "features/vehicle_controls.c",
    "features/blinkers.c",
    "features/aero_tuning.c",
    "features/roof_fade.c",
    "features/roof_aero.c",
    "features/speed_zone.c",
    "features/autodrive.c",
    "features/game_input.c",
    "features/bindings.c",
    "features/kbm_config.c",
    "features/controller_pause.c",
    "features/offline_events.c",
    "features/custom_showcases.c",
    "features/offline_event_division.c",
    "features/offline_event_setup.c",
    "features/autodrive_game.c",
    "features/autodrive_notice.c",
    "features/autodrive_ui.c",
    "features/ui_controls.c",
    "features/png_validation.c",
    "features/png_decode.c",
]


def parse_arguments():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("zig", type=Path)
    parser.add_argument("--test", action="store_true")
    parser.add_argument("--azimuth", action="store_true", help="Build the Azimuth winmm proxy")
    parser.add_argument("--output", type=Path)
    parser.add_argument("--build-dir", type=Path)
    parser.add_argument("--jobs", type=int, default=min(8, os.cpu_count() or 1))
    parser.add_argument("--rebuild", action="store_true", help="Ignore cached build inputs")
    arguments = parser.parse_args()
    if arguments.jobs < 1:
        parser.error("--jobs must be positive")
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
    if options.test:
        arguments.append("-DFH2_TWEAKS_TEST=1")
    if options.azimuth:
        arguments.append("-DFH2_AZIMUTH=1")
    return arguments


def version_resource(filename):
    version = re.search(r'#define MOD_VERSION "([\d.]+)"',
                        (SOURCE / "extra_options.h").read_text()).group(1)
    manifest_version = json.loads((SOURCE / "patches.json").read_text())["version"]
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

    strings = {"FileDescription": "Forza Horizon 2 Extra Options",
               "FileVersion": version, "InternalName": "FH2_ExtraOptions",
               "OriginalFilename": filename, "ProductName": "Forza Horizon 2 Extra Options",
               "ProductVersion": version, "LegalCopyright": "Arthur (RokkuDayo)"}
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
    sources.extend(
        [
            "adapters/azimuth.c",
            "adapters/winmm.c",
            "adapters/winmm_exports.s",
            "adapters/azimuth_host.c",
            "adapters/azimuth_save.c",
            "adapters/azimuth_markers.c",
            "features/kbm_input.c",
            "features/kbm_win32.c",
            "features/kbm_pitch.c",
            "features/kbm_menu.c",
            "features/kbm_rebind.c",
        ]
        if options.azimuth
        else ["adapters/mezcal.c"]
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
    resource.write_bytes(version_resource("winmm.dll" if options.azimuth else "xg.dll"))
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
    output = (options.output or SOURCE.parent / filename).resolve()
    variant = ("azimuth" if options.azimuth else "mezcal") + ("-test" if options.test else "")
    directory = options.build_dir or SOURCE.parent / ".build" / variant
    build(compiler, output, directory.resolve(), options)
    print(output)


if __name__ == "__main__":
    main()
