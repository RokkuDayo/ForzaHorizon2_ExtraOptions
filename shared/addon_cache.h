#pragma once
#include "shared/addons.h"
#include "shared/addon_database.h"
// Cache hits always return a private in-memory connection, never a writable
// persistent game database. Keys include the actual incoming SQLite snapshot.
int addon_cache_key(void *source,const wchar_t *directory,unsigned settings,unsigned char key[32]);
void *addon_cache_load(const wchar_t *directory,const unsigned char key[32],unsigned count,
                      unsigned char active[128],unsigned char excluded[128]);
void addon_cache_store(const wchar_t *directory,const unsigned char key[32],void *merged,unsigned count,
                       const unsigned char active[128],const unsigned char excluded[128]);
