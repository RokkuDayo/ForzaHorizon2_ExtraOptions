#ifndef FH2_ADDON_DATABASE_H
#define FH2_ADDON_DATABASE_H
#include "addon_json.h"

typedef struct {
    int (*open)(const char *, void **);
    int (*close)(void *);
    int (*prepare)(void *, const char *, int, void **, const char **);
    int (*step)(void *);
    int (*finalize)(void *);
    int (*column_count)(void *);
    int64_t (*column_int64)(void *, int);
    const char *(*column_text)(void *, int);
    void *(*backup_init)(void *, const char *, void *, const char *);
    int (*backup_step)(void *, int);
    int (*backup_finish)(void *);
} AddonSqlite;

void addon_database_bind(unsigned char *image);
const AddonSqlite *addon_database_api(void);
void *addon_database_clone(void *source);
int addon_database_apply(void *database, const JsonValue *document, const char *base_hash,
                         const char *addon_id, char error[256]);
void addon_ids_reset(void);
int addon_ids_plan(void *database, JsonValue *documents[128], const char *labels[128],
                   const unsigned char excluded[128], unsigned count);
int addon_ids_blocked(unsigned index);
int addon_ids_changed(int index);
void addon_ids_committed(unsigned index);
unsigned addon_ids_lookup(unsigned index, const char *table, const char *column, unsigned value);
int addon_ids_carbin_json(unsigned index, JsonValue *document, void **temporary);
void addon_ids_free_temporary(void *temporary);
unsigned addon_ids_carbin_value(void *context, unsigned part, unsigned value);
#endif
