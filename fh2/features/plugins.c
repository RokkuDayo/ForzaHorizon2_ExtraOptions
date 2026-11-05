#include "plugins.h"
#include "addons.h"
#include "addon_database.h"
#include "../fh2_plugin.h"
#include "logging.h"
#include "support.h"
#include "autodrive_game.h"

typedef struct Plugin {
    struct Plugin *next;
    FH2PluginContext context;
    wchar_t name[MAX_PATH];
    wchar_t *path;
    wchar_t *folder;
    FH2PluginVehicleUpdateFn volatile vehicle_update;
    FH2PluginDatabaseUpdateFn volatile database_update;
    LONG database_generation;
    wchar_t *volatile media_directory;
} Plugin;

static wchar_t directory[PATH_CAPACITY];
static unsigned char *game;
static int configured;
static volatile LONG state; // 0 pending, 1 worker running, 2 complete
static Plugin *volatile loaded;
static volatile LONG vehicle_callbacks;
static unsigned scanned_revision;
static void *current_database;
static LONG database_generation;

static void log_plugin(const wchar_t *path, const char *result, DWORD error) {
    int size = WideCharToMultiByte(CP_UTF8, 0, path, -1, NULL, 0, NULL, NULL);
    char *text = size ? HeapAlloc(GetProcessHeap(), 0, (size_t)size) : NULL;
    if (text) WideCharToMultiByte(CP_UTF8, 0, path, -1, text, size, NULL, NULL);
    log_append("PLUGINS: ");
    log_append(text ? text : "[path conversion failed]");
    log_append(" | ");
    log_append(result);
    if (error) log_append_value("; Windows error=", (int)error);
    else log_append("\r\n");
    if (text) HeapFree(GetProcessHeap(), 0, text);
}

static void configure_paths(unsigned char *base, const wchar_t *executable,
                            const wchar_t *launcher, int enable) {
    // Configuration never loads foreign code, and may run during proxy attach.
    // Once loaded, plugin callbacks must remain resident until process exit.
    if (InterlockedCompareExchange(&state, 0, 0)) return;
    configured = 0;
    directory[0] = 0;
    game = base;
    if (!enable || !base) return;
    (void)executable; (void)launcher;
    configured = 1; // Enabled mods, rather than a global plugins directory.

}

void plugins_configure(unsigned char *base, const wchar_t *executable, int enable) {
    wchar_t *launcher = HeapAlloc(GetProcessHeap(), 0, PATH_CAPACITY * sizeof(wchar_t));
    if (!launcher) return;
    DWORD n = GetModuleFileNameW(NULL, launcher, PATH_CAPACITY);
    configure_paths(base, executable, n && n < PATH_CAPACITY ? launcher : NULL, enable);
    HeapFree(GetProcessHeap(), 0, launcher);
}

int plugins_configured(void) { return configured; }

static void load_directory(void) {
    wchar_t *pattern = HeapAlloc(GetProcessHeap(), 0, PATH_CAPACITY * sizeof(wchar_t));
    Plugin *pending = NULL;
    if (!pattern || !path_join(pattern, directory, L"*.dll")) goto done;
    WIN32_FIND_DATAW data;
    HANDLE search = FindFirstFileW(pattern, &data);
    if (search == INVALID_HANDLE_VALUE) {
        DWORD error = GetLastError();
        if (error != ERROR_FILE_NOT_FOUND && error != ERROR_PATH_NOT_FOUND)
            log_plugin(directory, "directory scan failed", error);
        goto done;
    }
    do {
        if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        size_t n = (size_t)lstrlenW(data.cFileName);
        if (n < 4 || lstrcmpiW(data.cFileName+n-4, L".dll")) continue;
        Plugin *entry = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*entry));
        if (!entry) {
            log_plugin(directory, "out of memory while scanning", ERROR_NOT_ENOUGH_MEMORY);
            break;
        }
        memory_copy(entry->name, data.cFileName, (n+1)*sizeof(wchar_t));
        Plugin **slot = &pending;
        while (*slot && lstrcmpiW((*slot)->name, entry->name) < 0) slot = &(*slot)->next;
        entry->next = *slot;
        *slot = entry;
    } while (FindNextFileW(search, &data));
    FindClose(search);
    // Alphabetical, case-insensitive, top-level DLLs only. Subfolders may hold
    // dependencies/resources; do not accidentally run those as plugins.
    while (pending) {
        Plugin *entry = pending;
        pending = entry->next;
        int duplicate = 0;
        if (path_join(pattern, directory, entry->name)) {
            for (Plugin *p = loaded; p; p = p->next) duplicate |= !lstrcmpiW(p->path, pattern);
        }
        if (duplicate) { HeapFree(GetProcessHeap(), 0, entry); continue; }
        size_t length = (size_t)lstrlenW(directory) + (size_t)lstrlenW(entry->name) + 2;
        entry->path = HeapAlloc(GetProcessHeap(), 0, length * sizeof(wchar_t));
        if (!entry->path || !path_join(pattern, directory, entry->name)) {
            if (entry->path) HeapFree(GetProcessHeap(), 0, entry->path);
            HeapFree(GetProcessHeap(), 0, entry);
            continue;
        }
        memory_copy(entry->path, pattern, ((size_t)lstrlenW(pattern)+1) * sizeof(wchar_t));
        entry->folder = HeapAlloc(GetProcessHeap(), 0, ((size_t)lstrlenW(directory)+1)*sizeof(wchar_t));
        if (!entry->folder) { HeapFree(GetProcessHeap(),0,entry->path); HeapFree(GetProcessHeap(),0,entry); continue; }
        lstrcpyW(entry->folder, directory);
        HMODULE module = LoadLibraryExW(entry->path, NULL,
            LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
        if (!module) {
            log_plugin(entry->path, "load failed", GetLastError());
            HeapFree(GetProcessHeap(),0,entry->folder);
            HeapFree(GetProcessHeap(), 0, entry->path);
            HeapFree(GetProcessHeap(), 0, entry);
            continue;
        }
        FH2PluginContext *context = &entry->context;
        context->size = sizeof(*context);
        context->api_version = FH2_PLUGIN_API_VERSION;
#ifdef FH2_AZIMUTH
        context->adapter = FH2_PLUGIN_AZIMUTH;
#else
        context->adapter = FH2_PLUGIN_MEZCAL;
#endif
        context->game_image_size = GAME_IMAGE_SIZE;
        context->game_image = game;
        context->plugin_module = module;
        context->plugins_directory = entry->folder;
        context->plugin_path = entry->path;
        context->extra_options_version = MOD_VERSION;
        context->log_utf8 = log_append;
        entry->next = loaded;
        InterlockedExchangePointer((void *volatile *)&loaded, entry);
        // Keep module, context and paths alive, even on init failure.
        FH2PluginInitializeFn initialize = (FH2PluginInitializeFn)(uintptr_t)
            GetProcAddress(module, "FH2PluginInitialize");
        BOOL ready = TRUE;
        if (initialize) {
            ready = initialize(context);
            log_plugin(entry->path, ready ? "initialized" : "initializer returned FALSE; module retained", 0);
        } else {
            log_plugin(entry->path, "loaded (no optional initializer)", 0);
        }
        if (ready) {
            FH2PluginMediaDirectoryFn media=(FH2PluginMediaDirectoryFn)(uintptr_t)
                GetProcAddress(module,"FH2PluginMediaDirectory");
            if(media){
                const wchar_t *requested=media();
                wchar_t *resolved=HeapAlloc(GetProcessHeap(),0,PATH_CAPACITY*sizeof(wchar_t));
                DWORD size=requested && resolved?GetFullPathNameW(requested,PATH_CAPACITY,resolved,NULL):0;
                size_t prefix=(size_t)lstrlenW(entry->folder);
                DWORD attributes=size && size<PATH_CAPACITY?GetFileAttributesW(resolved):INVALID_FILE_ATTRIBUTES;
                if(size>prefix && size<PATH_CAPACITY && attributes!=INVALID_FILE_ATTRIBUTES &&
                   (attributes&FILE_ATTRIBUTE_DIRECTORY) && resolved[prefix]==L'\\' &&
                   CompareStringOrdinal(resolved,(int)prefix,entry->folder,(int)prefix,TRUE)==CSTR_EQUAL){
                    InterlockedExchangePointer((void *volatile *)&entry->media_directory,resolved);
                    log_plugin(entry->path,"private media directory ready",0);
                }else{if(resolved)HeapFree(GetProcessHeap(),0,resolved);log_plugin(entry->path,"private media directory rejected",0);}
            }

            void *database_callback=(void *)(uintptr_t)GetProcAddress(module,"FH2PluginDatabaseUpdate");
            if(database_callback)
                InterlockedExchangePointer((void *volatile *)&entry->database_update,database_callback);
            void *callback = (void *)(uintptr_t)GetProcAddress(module, "FH2PluginVehicleUpdate");
            if (callback) {
                InterlockedExchangePointer((void *volatile *)&entry->vehicle_update, callback);
                InterlockedIncrement(&vehicle_callbacks);
            }
        }
    }
done:
    if (pattern) HeapFree(GetProcessHeap(), 0, pattern);
}

static DWORD WINAPI load_plugins(void *unused) {
    (void)unused;
    unsigned revision = addons_media_revision();
    unsigned count = addons_count();
    for (unsigned i=0; i<count; ++i) {
        const wchar_t *root = addons_directory(i);
        if (root && addons_media_active(i) && path_join(directory, root, L"plugins"))
            load_directory();
    }
    scanned_revision = revision;
    InterlockedExchange(&state, 2);
    return 0;
}

void plugins_start(void) {
    if (!configured) return;
    LONG current = InterlockedCompareExchange(&state, 0, 0);
    if (current == 1) return;
    if (current == 2 && scanned_revision == addons_media_revision()) return;
    if (InterlockedCompareExchange(&state, 1, current) != current) return;
    // Called by native filesystem lookup or the committed database handoff, after host setup.
    // Never wait for a worker under DllMain or recursively load on the asset
    // resolver thread. Windows completes thread DLL initialization first.
    HANDLE worker = CreateThread(NULL, 0, load_plugins, NULL, 0, NULL);
    if (worker) CloseHandle(worker);
    else {
        log_plugin(directory, "could not start loader", GetLastError());
        InterlockedExchange(&state, 2);
    }
}

static int WINAPI plugin_flash(void) {
    return autodrive_game_flash(autodrive_game_camera());
}

static void update_plugin_databases(void);

void plugins_database_ready(void *database) {
    // Called by the native game database handoff, never the loader worker.
    current_database=database;
    ++database_generation;
    if(database && configured){
        // The merge has committed and enabled successful mod media, but the
        // native database factory has not yet returned to its menu consumers.
        // LoadLibrary/initializers stay on the existing deferred worker.
        plugins_start();
        ULONGLONG until=GetTickCount64()+5000;
        while(InterlockedCompareExchange(&state,0,0)==1 && GetTickCount64()<until)Sleep(5);
        if(InterlockedCompareExchange(&state,0,0)==1)
            log_append("PLUGINS: database provider startup timed out; pending callbacks deferred.\r\n");
        update_plugin_databases();
    }
}

static int plugin_mod_active(const Plugin *plugin) {
    unsigned count=addons_count();
    for(unsigned i=0;i<count;++i){
        const wchar_t *root=addons_directory(i);
        if(!root || !addons_media_active(i))continue;
        int length=lstrlenW(root);
        if(lstrlenW(plugin->folder)==length+8 &&
           CompareStringOrdinal(root,length,plugin->folder,length,TRUE)==CSTR_EQUAL &&
           !lstrcmpiW(plugin->folder+length,L"\\plugins"))return 1;
    }
    return 0;
}

wchar_t *plugins_media_file(unsigned addon,const char *relative,size_t length){
    const wchar_t *root=addons_directory(addon);
    if(!configured || !root || !addons_media_active(addon) || !relative || !length || length>32767)return NULL;
    size_t root_length=(size_t)lstrlenW(root);
    for(Plugin *p=InterlockedCompareExchangePointer((void *volatile *)&loaded,NULL,NULL);p;p=p->next){
        const wchar_t *folder=InterlockedCompareExchangePointer((void *volatile *)&p->media_directory,NULL,NULL);
        if(!folder || (size_t)lstrlenW(p->folder)!=root_length+8 ||
           CompareStringOrdinal(root,(int)root_length,p->folder,(int)root_length,TRUE)!=CSTR_EQUAL ||
           lstrcmpiW(p->folder+root_length,L"\\plugins"))continue;
        int n=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,relative,(int)length,NULL,0);
        size_t prefix=(size_t)lstrlenW(folder);
        if(!n || prefix+(unsigned)n+2>=PATH_CAPACITY)continue;
        wchar_t *path=HeapAlloc(GetProcessHeap(),0,(prefix+n+2)*sizeof(wchar_t));if(!path)continue;
        memory_copy(path,folder,prefix*sizeof(wchar_t));path[prefix++]=L'\\';
        MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,relative,(int)length,path+prefix,n);path[prefix+n]=0;
        for(size_t i=prefix;i<prefix+(unsigned)n;++i)if(path[i]==L'/')path[i]=L'\\';
        DWORD a=GetFileAttributesW(path);
        if(a!=INVALID_FILE_ATTRIBUTES && !(a&FILE_ATTRIBUTE_DIRECTORY))return path;
        HeapFree(GetProcessHeap(),0,path);
    }
    return NULL;
}

static void update_plugin_databases(void) {
    if(!current_database)return;
    const AddonSqlite *sql=addon_database_api();
    FH2PluginDatabaseFrame frame={sizeof(frame),1,current_database,sql->prepare,
                                 sql->step,sql->finalize,sql->column_int64};
    for(Plugin *p=InterlockedCompareExchangePointer((void *volatile *)&loaded,NULL,NULL);p;p=p->next){
        FH2PluginDatabaseUpdateFn callback=(FH2PluginDatabaseUpdateFn)(uintptr_t)
            InterlockedCompareExchangePointer((void *volatile *)&p->database_update,NULL,NULL);
        if(callback && p->database_generation!=database_generation){
            p->database_generation=database_generation;
            if(!plugin_mod_active(p))continue;
            log_plugin(p->path,callback(&frame)?"in-memory database callback complete":"database callback failed",0);
        }
    }
}

void plugins_vehicle_update(void *car, float dt) {
    if(!car)return;
    update_plugin_databases();
    if(!InterlockedCompareExchange(&vehicle_callbacks,0,0))return;
    void *controller = NULL;
    unsigned short buttons = 0;
    int enabled = autodrive_game_buttons(&controller, &buttons) == 0 &&
                  autodrive_game_window_focused() && autodrive_game_can_drive(controller);
    FH2PluginVehicleFrame frame = {sizeof(frame), enabled, car, buttons, dt, plugin_flash};
    for (Plugin *p = InterlockedCompareExchangePointer((void *volatile *)&loaded, NULL, NULL);
         p; p = p->next) {
        FH2PluginVehicleUpdateFn callback = (FH2PluginVehicleUpdateFn)(uintptr_t)
            InterlockedCompareExchangePointer((void *volatile *)&p->vehicle_update, NULL, NULL);
        if (callback) callback(&frame);
    }
}

#ifdef FH2_TWEAKS_TEST
__declspec(dllexport) void Fh2TestPluginsVehicleNativeUpdate(void *car, float dt) {
    plugins_vehicle_update(car, dt);
}
__declspec(dllexport) void Fh2TestPluginsVehicleUpdate(void *car, int enabled, unsigned buttons) {
    update_plugin_databases();
    FH2PluginVehicleFrame frame = {sizeof(frame), enabled, car, buttons, 0.016f, NULL};
    for (Plugin *p = loaded; p; p = p->next) if (p->vehicle_update) p->vehicle_update(&frame);
}
__declspec(dllexport) void Fh2TestPluginsConfigure(void *base, const wchar_t *path,
                                                const wchar_t *launcher, int enable) {
    configure_paths(base, path, launcher, enable);
}
__declspec(dllexport) void Fh2TestPluginsDatabaseReady(void *db){plugins_database_ready(db);}
__declspec(dllexport) void Fh2TestPluginsStart(void) { plugins_start(); }
__declspec(dllexport) int Fh2TestPluginsState(void) { return (int)InterlockedCompareExchange(&state, 0, 0); }
__declspec(dllexport) const wchar_t *Fh2TestPluginsDirectory(void) { return directory; }
__declspec(dllexport) void Fh2TestPluginsLogDirectory(const wchar_t *path) { log_set_directory(path); }
#endif
