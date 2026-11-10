#pragma once
#include "fh2/fh2_plugin.h"

// FM5 plugins use the existing version-1 ABI with title-specific export names.
// Export BOOL FM5PluginInitialize(const FM5PluginContext *context), optionally
// void FM5PluginDatabaseUpdate(const FM5PluginDatabaseFrame *frame).
// The FM5 context reports Title ID 7B35D917's image size (0x279d000).
// FH2PluginVehicleUpdate/flash callbacks are not available on the FM5 profile.
typedef FH2PluginContext FM5PluginContext;
typedef FH2PluginDatabaseFrame FM5PluginDatabaseFrame;
typedef FH2PluginInitializeFn FM5PluginInitializeFn;
typedef FH2PluginDatabaseUpdateFn FM5PluginDatabaseUpdateFn;
