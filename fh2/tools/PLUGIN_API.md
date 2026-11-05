# Native plugin loading

Enabled mods load top-level DLLs from their own `plugins` folder. The order is
loadorder.txt order, then case-insensitive DLL filename order. No global plugins
directory is scanned. Files are loaded once and remain resident until exit.

`fh2_plugin.h` contains the dependency-free Windows C ABI. The optional
`FH2PluginInitialize` receives a persistent context after DllMain returns on
the loader worker. Returning FALSE retains the module but disables callbacks.
The version1 context layout remains 64 bytes and has not changed.

Starting with 0.2.176 a plugin can optionally export
`void WINAPI FH2PluginVehicleUpdate(const FH2PluginVehicleFrame *frame)`.
The host calls it during the native local player's car update, before the
original update. It never receives AI car updates. The frame and car pointer
are borrowed until the callback returns. Do not retain them or release the car.
The module must remain resident and keep callbacks short; no worker-thread
game-object mutations should be scheduled using a borrowed pointer.

`input_enabled` covers native gameplay input/presentation state and window focus.
It is false in menus/loading, but the callback still runs when the car updates,
so a plugin can cancel a pending gesture. Some stopped simulations do not update
the car; handle car changes and require a released button before rearming.
`buttons` uses Xbox XInput-compatible button bits. `flash()` calls the existing
native player-view flash and returns zero if the camera/view is unavailable or
already busy. All game calls must use the supported executable version.

Callbacks become visible to the game thread only after initialization succeeds.
Plugins without this export retain their existing behavior. Host entry points
and callbacks use WINAPI; compile for Win64.

## In-memory database providers (0.2.211+)

Optional export `BOOL WINAPI FH2PluginDatabaseUpdate(const FH2PluginDatabaseFrame *f)`.
The original64-byte initializer ABI remains unchanged. The new frame contains
size/version, borrowed merged SQLite connection and prepare/step/finalize/
column_int64 function pointers. Do not retain these or use them from workers.
The provider owns transaction/savepoint handling for its changes. Return FALSE
on failure after rollback. No database callback is made for failed initializers.

The host publishes only its committed in-memory merge, never an on-disk base
connection. After activating successful mod media, the native database handoff
starts the deferred loader and allows up to5seconds for initializers. Ready
providers run on the owning game thread before the native factory returns.
Timeout is logged and pending providers run on the next local-car update. Avoid
long initializers. Later-loaded providers use the same pending update dispatch.
A callback runs once per merge generation, independent of vehicle input state.
The connection is invalidated when a new merge begins. DLLs remain resident.

On a later merge, a resident provider is called only if its owning mod is still active. Failed or disabled mods cannot mutate the new merged database.

## Private media directory (0.2.212+)

Optional export `const wchar_t *WINAPI FH2PluginMediaDirectory(void)` is queried
on the worker only after successful initialization. Return a complete immutable
asset tree below this plugin's plugins_directory, or NULL. The host canonicalizes
and copies the directory path, requiring an existing child directory. No callback
runs during individual asset reads. Prepare all files before returning.

Files supplement the owning active mod's media folder and use native sync/async
file objects through EO's existing path bridge. Ordinary mod media takes priority;
other mod priorities remain unchanged. Existing safe-relative-path guards apply.
Resources may be cached by the game; do not rewrite files while they are in use.
A private root does not add new file names to a base ZIP's enumeration. For new
state groups that the engine does not discover, register states in an existing
loaded group using a scoped parser extension rather than relying on enumeration.
