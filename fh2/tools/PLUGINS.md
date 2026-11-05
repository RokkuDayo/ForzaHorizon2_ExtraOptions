# DLL plugins

Place 64-bit Windows DLLs directly in:

- Azimuth: `mods/<mod name>/plugins` beside that mod's media/database files.
- Mezcal: `Mount/mods/<mod name>/plugins`.

No manifest, addon database, import library or hooking framework is required.
The plugin loader is available whenever Extra Options is enabled, independently
of `EnableMediaOverrides`. Restart the game after adding or replacing a plugin.

DLLs load once in mod load order, then case-insensitive filename order. No global
plugins folder is scanned. Prefix names with numbers if order within a mod matters. Only top-level DLLs are plugins; subfolders can hold resources or
dependencies. Windows resolves a DLL's sibling dependencies and normal default
DLL search locations. Plugins remain responsible for their own dependencies;
the loader does not download any runtime.

Loading begins on a worker after the game's first native filesystem lookup,
after host setup. No plugin code runs from Extra Options' `DllMain`, and the
asset resolver does not wait for the worker. This provides an early startup
hook opportunity; it is not a synchronous per-frame or renderer callback.

`FH2_ExtraOptions.log` records each load/initialization result and Windows errors.
A DLL that fails to load does not prevent later DLLs loading. Loaded modules
remain resident until the process exits; there is no hot unloading.

## Optional initialization export

An ordinary DLL without this export loads normally. For initialization outside
your own `DllMain`, export:

```c
__declspec(dllexport) BOOL WINAPI
FH2PluginInitialize(const FH2PluginContext *context);
```

Use `source/fh2_plugin.h`; the example is `examples/plugins/hello_plugin.c`.
For C++, declare the export `extern "C"` to preserve its name.

The version-1 context contains:

- `size` and `api_version` for compatibility checks.
- `adapter`: `FH2_PLUGIN_AZIMUTH` or `FH2_PLUGIN_MEZCAL`.
- `game_image` and `game_image_size`: the actual mapped, relocated FH2 image.
  Under Azimuth, `GetModuleHandleW(NULL)` refers to Azimuth instead.
- `plugin_module`, `plugin_path` and `plugins_directory`.
- `extra_options_version` and a UTF-8 `log_utf8` callback.

Context and strings stay valid until process exit. The initializer runs once
on the worker after `LoadLibraryExW` returns. Keep `DllMain` minimal and install
verified hooks from the initializer. Hook installation and any interaction with
the game's threads remain the plugin's responsibility.

Return `TRUE` for successful initialization. `FALSE` is logged; the DLL remains
loaded because unloading code that may already own hooks would invalidate them.
Initializers are not retried. A plugin crash is a crash in the game's process.

The API requires no link to Extra Options: all information is passed to the
initializer. Existing DLLs using only `DllMain` are supported, though the optional
initializer is preferable for work outside the Windows loader lock.

## Build the example

From an **x64 Native Tools Command Prompt** with Visual Studio Build Tools:

```bat
cl /nologo /LD /O2 /MT /Isource examples\plugins\hello_plugin.c /link /OUT:HelloPlugin.dll
```

`/MT` links the Microsoft C runtime statically. The small C example itself uses
only the supplied context and Windows SDK types. Copy `HelloPlugin.dll` into the
appropriate plugins folder. Its initialization message appears in the mod log.

Optional in EO0.2.212: FH2PluginMediaDirectory returns an existing private asset
directory below the plugin folder after initialization. These assets supplement
the active mod's media tree through native I/O; normal mod media takes priority.
See [PLUGIN_API.md](PLUGIN_API.md) and `fh2_plugin.h` for the ABI contract.
