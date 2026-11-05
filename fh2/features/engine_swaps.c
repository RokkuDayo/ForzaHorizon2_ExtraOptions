#include "engine_swaps.h"
#include "addon_database.h"
#include "logging.h"
#include "support.h"

static int enabled;

void engine_swaps_configure(int value) {
    enabled = value != 0;
}

int engine_swaps_enabled(void) {
    return enabled;
}

static int execute(void *database, const char *text, int64_t *value) {
    const AddonSqlite *sql = addon_database_api();
    void *statement = NULL;
    int result = sql->prepare(database, text, -1, &statement, NULL);
    if (result || !statement) {
        if (statement) {
            sql->finalize(statement);
        }
        return 0;
    }
    result = sql->step(statement);
    if (value && result == 100) {
        *value = sql->column_int64(statement, 0);
    }
    while (result == 100) {
        result = sql->step(statement);
    }
    int finished = sql->finalize(statement);
    return result == 101 && !finished;
}

static int index_engine_parts(void *database) {
    static const char *const tables[] = {"Camshaft",
                                         "CSC",
                                         "Displacement",
                                         "DSC",
                                         "Exhaust",
                                         "Flywheel",
                                         "FuelSystem",
                                         "Ignition",
                                         "Intake",
                                         "Intercooler",
                                         "Manifold",
                                         "OilCooling",
                                         "PistonsCompression",
                                         "RestrictorPlate",
                                         "TurboQuad",
                                         "TurboSingle",
                                         "TurboTwin",
                                         "Valves"};
    for (unsigned i = 0; i < ARRAY_COUNT(tables); ++i) {
        char text[256];
        const char *parts[] = {"CREATE INDEX IF NOT EXISTS FH2_Engine_", tables[i],
                               " ON List_UpgradeEngine", tables[i], "(EngineID,IsStock,Level)"};
        size_t length = 0;
        for (unsigned j = 0; j < ARRAY_COUNT(parts); ++j) {
            size_t count = text_length(parts[j]);
            memory_copy(text + length, parts[j], count);
            length += count;
        }
        text[length] = 0;
        // Minimal/custom databases may omit optional upgrade types.
        int64_t present = 0;
        char name[128] = "SELECT COUNT(*) FROM sqlite_master WHERE name='List_UpgradeEngine";
        size_t n = text_length(name), suffix = text_length(tables[i]);
        memory_copy(name + n, tables[i], suffix);
        memory_copy(name + n + suffix, "'", 2);
        if (!execute(database, name, &present) || (present && !execute(database, text, NULL))) {
            return 0;
        }
    }
    return 1;
}

int engine_swaps_expand(void *database) {
    if (!enabled || !database) {
        return 0;
    }
    int64_t marked = 0;
    if (!execute(database, "SELECT COUNT(*) FROM sqlite_master WHERE name='FH2_EngineSwapReady'",
                 &marked)) {
        return 0;
    }
    if (marked) {
        return 1;
    }

    // Stable positive signed part IDs: namespace bit, 15-bit car, 15-bit engine.
    // Existing authored swaps win. Neither part IDs nor levels depend on load order.
    // This runs on the private database before any car/profile caches are built.
    static const char sources[] =
        "CREATE TABLE FH2_EngineSwapSources AS SELECT E.EngineID,E.[EngineMass-kg], "
        "COALESCE((SELECT MIN(Price) FROM List_UpgradeEngine P WHERE P.EngineID=E.EngineID "
        "AND P.IsStock=0 AND P.Price>0), MAX(5000,MIN(75000,ROUND(COALESCE((SELECT MIN(D.BaseCost) "
        "FROM List_UpgradeEngine P JOIN Data_Car D ON D.Id=P.Ordinal WHERE P.EngineID=E.EngineID "
        "AND P.IsStock=1 AND D.BaseCost>0),25000)*0.2/1000)*1000))) AS Price "
        "FROM Data_Engine E WHERE E.EngineID BETWEEN 1 AND 32767 AND E.[EngineMass-kg]>0 "
        "AND COALESCE(E.MediaName,'')<>'' "
        "AND EXISTS(SELECT 1 FROM List_UpgradeEngineCamshaft P WHERE P.EngineID=E.EngineID AND "
        "P.IsStock=1) "
        "AND EXISTS(SELECT 1 FROM List_UpgradeEngineDisplacement P WHERE P.EngineID=E.EngineID AND "
        "P.IsStock=1)";
    static const char create[] =
        "CREATE TABLE FH2_EngineSwapCatalog AS SELECT "
        "1073741824 + S.Ordinal*32768 + E.EngineID AS Id, S.Ordinal AS Ordinal, "
        "E.EngineID+16 AS Level, E.EngineID, 0 AS IsStock, S.ManufacturerID, E.Price, "
        "E.[EngineMass-kg]-B.[EngineMass-kg] AS MassDiff, "
        "(E.[EngineMass-kg]-B.[EngineMass-kg])*(CASE WHEN C.EnginePlacementID=1 THEN 0.3 ELSE -0.3 "
        "END)"
        "/(C.CurbWeight*100.0) AS WeightDistDiff, 1.0 AS DragScale, 1.0 AS WindInstabilityScale "
        "FROM List_UpgradeEngine S JOIN Data_Car C ON C.Id=S.Ordinal "
        "JOIN Data_Engine B ON B.EngineID=S.EngineID CROSS JOIN FH2_EngineSwapSources E "
        "WHERE S.IsStock=1 AND S.Ordinal BETWEEN 1 AND 32767 "
        "AND C.CurbWeight>0 AND B.[EngineMass-kg]>0 "
        "AND NOT EXISTS(SELECT 1 FROM List_UpgradeEngine P WHERE P.Ordinal=S.Ordinal AND "
        "P.EngineID=E.EngineID)";
    if (!execute(database, "SAVEPOINT fh2_engine_swaps", NULL)) {
        return 0;
    }
    int valid =
        execute(
            database,
            "CREATE INDEX IF NOT EXISTS FH2_EngineOrdinal ON List_UpgradeEngine(Ordinal,EngineID)",
            NULL) &&
        execute(
            database,
            "CREATE INDEX IF NOT EXISTS FH2_EngineDonor ON List_UpgradeEngine(EngineID,IsStock)",
            NULL) &&
        // Keep authored names byte-for-byte. Missing names inherit a real stock
        // donor's localized car name before synthetic swap rows are introduced.
        execute(database,
            "UPDATE Data_Engine SET EngineName=(SELECT COALESCE(NULLIF(D.DisplayName,''), "
            "NULLIF(D.MediaName,'')) FROM List_UpgradeEngine P JOIN Data_Car D ON D.Id=P.Ordinal "
            "WHERE P.EngineID=Data_Engine.EngineID AND P.IsStock=1 "
            "AND COALESCE(NULLIF(D.DisplayName,''),NULLIF(D.MediaName,'')) IS NOT NULL "
            "ORDER BY D.Id LIMIT 1) WHERE TRIM(COALESCE(EngineName,''), ' \t\n\r')='' "
            "AND EXISTS(SELECT 1 FROM List_UpgradeEngine P JOIN Data_Car D ON D.Id=P.Ordinal "
            "WHERE P.EngineID=Data_Engine.EngineID AND P.IsStock=1 "
            "AND COALESCE(NULLIF(D.DisplayName,''),NULLIF(D.MediaName,'')) IS NOT NULL)", NULL) &&
        index_engine_parts(database) && execute(database, sources, NULL) &&
        execute(database, create, NULL);
    int64_t collisions = 0, added = 0, unsupported = 0;
    valid =
        valid &&
        execute(
            database,
            "SELECT COUNT(*) FROM FH2_EngineSwapCatalog N JOIN List_UpgradeEngine P ON P.Id=N.Id",
            &collisions) &&
        !collisions &&
        execute(database,
                "SELECT COUNT(*) FROM FH2_EngineSwapCatalog N JOIN List_UpgradeEngine P ON "
                "P.Ordinal=N.Ordinal AND P.Level=N.Level",
                &collisions) &&
        !collisions &&
        execute(database, "CREATE UNIQUE INDEX FH2_EngineCatalogId ON FH2_EngineSwapCatalog(Id)",
                NULL) &&
        execute(database, "DROP INDEX FH2_EngineOrdinal", NULL) &&
        execute(database, "DROP INDEX FH2_EngineDonor", NULL) &&
        execute(database,
                "INSERT INTO "
                "List_UpgradeEngine(Id,Ordinal,Level,EngineID,IsStock,ManufacturerID,Price,"
                "MassDiff,WeightDistDiff,DragScale,WindInstabilityScale) "
                "SELECT "
                "Id,Ordinal,Level,EngineID,IsStock,ManufacturerID,Price,MassDiff,WeightDistDiff,"
                "DragScale,WindInstabilityScale FROM FH2_EngineSwapCatalog ORDER BY Id",
                NULL) &&
        execute(database, "SELECT COUNT(*) FROM FH2_EngineSwapCatalog", &added) &&
        execute(
            database,
            "CREATE INDEX FH2_EngineOrdinal ON List_UpgradeEngine(Ordinal,IsStock,Level,EngineID)",
            NULL) &&
        execute(database,
                "CREATE INDEX FH2_EngineOrdinalLevel ON List_UpgradeEngine(Ordinal,Level)", NULL) &&
        execute(database, "CREATE INDEX FH2_EngineDonor ON List_UpgradeEngine(EngineID,IsStock)",
                NULL) &&
        execute(database, "CREATE INDEX FH2_EngineStock ON List_UpgradeEngine(IsStock,EngineID)",
                NULL) &&
        execute(database,
                "CREATE TABLE FH2_EngineSwapReady AS SELECT 1 AS Version,COUNT(*) AS AddedRows "
                "FROM FH2_EngineSwapCatalog",
                NULL) &&
        execute(database, "DROP TABLE FH2_EngineSwapCatalog", NULL) &&
        execute(database, "DROP TABLE FH2_EngineSwapSources", NULL);
    if (!valid) {
        execute(database, "ROLLBACK TO fh2_engine_swaps", NULL);
        execute(database, "RELEASE fh2_engine_swaps", NULL);
        log_append("ENGINES: expansion rolled back (invalid data or reserved part/level "
                   "collision); authored swaps retained.\r\n");
        return 0;
    }
    if (!execute(database, "RELEASE fh2_engine_swaps", NULL)) {
        execute(database, "ROLLBACK TO fh2_engine_swaps", NULL);
        execute(database, "RELEASE fh2_engine_swaps", NULL);
        return 0;
    }
    execute(database,
            "SELECT (SELECT COUNT(*) FROM Data_Engine WHERE EngineID NOT BETWEEN 1 AND 32767) + "
            "(SELECT COUNT(*) FROM List_UpgradeEngine WHERE IsStock=1 AND Ordinal NOT BETWEEN 1 "
            "AND 32767)",
            &unsupported);
    log_append_value("ENGINES: added engine swaps in memory", (int)added);
    if (unsupported) {
        log_append("ENGINES: IDs outside 1..32767 retain authored swaps only.\r\n");
    }
    return 1;
}

// The engine-description getter constructs a literal text object, unlike
// transmission metadata's localized binding. Resolve the donor reference here
// at display time, so startup ordering and language changes are respected.
static unsigned char *game;
typedef void **(*LiteralText)(void **, const char *, int);
static void **engine_name_text(void **out, const char *text, int length) {
    unsigned key = 0;
    if (enabled && text && text[0]=='_' && text[1]=='&') {
        unsigned i=2;
        for (; i<12 && (length<0 ? text[i]!=0 : i<(unsigned)length); ++i) {
            unsigned digit=(unsigned char)text[i]-'0';
            if (digit>9 || key>(0xffffffffu-digit)/10) { break; }
            key=key*10+digit;
        }
        if (i>2 && (length<0 ? text[i]==0 : i==(unsigned)length)) {
            return ((void **(*)(void **, unsigned))(game+0x123a4d0))(out,key);
        }
    }
    return ((LiteralText)(game+0x323310))(out,text,length);
}

static unsigned char thunk[12], call[5];
static const unsigned char unused[12]={0xcc,0xcc,0xcc,0xcc,0xcc,0xcc,0xcc,0xcc,0xcc,0xcc,0xcc,0xcc};
static const unsigned char original_call[5]={0xe8,0xd6,0xd2,0x46,0xff};
static const unsigned char *const accepted_thunk[]={unused,thunk};
static const unsigned char *const accepted_call[]={original_call,call};
PatchDefinition engine_name_hook_patches[2]={
    {0xeb6154,0,12,GROUP_ENGINE_SWAPS,"engine_donor_name_thunk",unused,thunk,thunk,accepted_thunk,2},
    {0xeb6035,0,5,GROUP_ENGINE_SWAPS,"engine_donor_name_text",original_call,call,call,accepted_call,2},
};
void engine_swaps_prepare_hooks(unsigned char *image) {
    game=image;
    const unsigned char jump[]={0x48,0xb8,0,0,0,0,0,0,0,0,0xff,0xe0};
    uintptr_t callback=(uintptr_t)engine_name_text;
    memory_copy(thunk,jump,12);memory_copy(thunk+2,&callback,8);
    int32_t displacement=0xeb6154-0xeb6035-5;
    call[0]=0xe8;memory_copy(call+1,&displacement,4);
}

#ifdef FH2_TWEAKS_TEST
__declspec(dllexport) void *Fh2TestEngineNameText(void) { return engine_name_text; }
__declspec(dllexport) void Fh2TestEngineSwapsConfigure(int value) {
    engine_swaps_configure(value);
}

__declspec(dllexport) int Fh2TestEngineSwaps(void *database) {
    return engine_swaps_expand(database);
}
#endif
