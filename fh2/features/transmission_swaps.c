#include "transmission_swaps.h"
#include "addon_database.h"
#include "logging.h"
#include "support.h"

static int enabled;
void transmission_swaps_configure(int value) {
    enabled = value != 0;
}
int transmission_swaps_enabled(void) {
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


int transmission_swaps_expand(void *database) {
    if (!enabled || !database) {
        return 0;
    }
    int64_t marked = 0, collisions = 0, added = 0;
    if (!execute(database, "SELECT COUNT(*) FROM sqlite_master WHERE name='FH2_TransmissionSwapReady'", &marked)) { return 0; }
    if (marked) {
        return 1;
    }
    // Separate from the engine namespace. Stable 13-bit destination and donor
    // drivetrain IDs plus a 3-bit original level; never reassign saved IDs.
    // The native transmission reader treats Level>1 as fully ratio-tunable.
    // Preserve every authored row and donor identity, even if another donor has
    // identical ratios. Deduplication by contents would invalidate saved part
    // IDs when a lower-ID mod donor with the same ratios appears or disappears.
    if (!execute(database, "SAVEPOINT fh2_transmission_swaps", NULL)) { return 0; }
    int valid = 1;
    valid = valid && execute(database, "CREATE INDEX IF NOT EXISTS FH2_TransmissionDrivetrainLevel ON "
        "List_UpgradeDrivetrainTransmission(DrivetrainID,IsStock,Level)", NULL);
    valid = valid && execute(database, "CREATE INDEX FH2_TransmissionLevelCollision ON "
        "List_UpgradeDrivetrainTransmission(DrivetrainID,Level)", NULL);
    valid = valid && execute(database,
        "CREATE TABLE FH2_TransmissionSources AS SELECT P.*, P.DrivetrainID*8+P.Level AS SourceKey FROM "
        "List_UpgradeDrivetrainTransmission P WHERE P.DrivetrainID BETWEEN 1 AND 8191 AND P.Level BETWEEN 0 "
        "AND 7 AND P.NumGears BETWEEN 2 AND 9 AND P.FinalDriveRatio>0 AND P.GearRatio1>0", NULL);
    valid = valid && execute(database,
        "CREATE UNIQUE INDEX FH2_TransmissionSourceKey ON FH2_TransmissionSources(SourceKey)", NULL);
    valid = valid && execute(database,
        "CREATE TABLE FH2_TransmissionTargets AS SELECT DISTINCT P.DrivetrainID AS DrivetrainID FROM "
        "List_UpgradeDrivetrainTransmission P JOIN Data_Drivetrain D ON D.DrivetrainID=P.DrivetrainID WHERE "
        "P.IsStock=1 AND P.DrivetrainID BETWEEN 1 AND 8191", NULL);
    valid = valid && execute(database,
        "CREATE TABLE FH2_TransmissionCatalog AS SELECT 536870912+T.DrivetrainID*65536+S.SourceKey AS "
        "Id,T.DrivetrainID AS DrivetrainID,16+S.SourceKey AS Level,0 AS IsStock,S.ManufacturerID AS "
        "ManufacturerID,S.MassDiff AS MassDiff,MAX(1000,S.Price) AS Price,S.MomentInertia AS "
        "MomentInertia,S.GearShiftTime AS GearShiftTime,S.FinalDriveRatio AS FinalDriveRatio,S.NumGears AS "
        "NumGears,S.GearRatio0 AS GearRatio0,S.GearRatio1 AS GearRatio1,S.GearRatio2 AS "
        "GearRatio2,S.GearRatio3 AS GearRatio3,S.GearRatio4 AS GearRatio4,S.GearRatio5 AS "
        "GearRatio5,S.GearRatio6 AS GearRatio6,S.GearRatio7 AS GearRatio7,S.GearRatio8 AS "
        "GearRatio8,S.CVTShiftTime AS CVTShiftTime FROM FH2_TransmissionTargets T CROSS JOIN "
        "FH2_TransmissionSources S WHERE T.DrivetrainID<>S.DrivetrainID", NULL);
    valid = valid && execute(database,
        "CREATE UNIQUE INDEX FH2_TransmissionCatalogId ON FH2_TransmissionCatalog(Id)", NULL);
    valid = valid && execute(database,
        "CREATE INDEX FH2_TransmissionCatalogLevel ON FH2_TransmissionCatalog(Level)", NULL);
    valid = valid && execute(database,
        "CREATE TABLE FH2_TransmissionLabels AS SELECT 536870912+S.SourceKey AS Id,16+S.SourceKey AS "
        "Level,COALESCE((SELECT COALESCE(NULLIF(D.DisplayName,''),NULLIF(D.MediaName,'')) "
        "FROM List_UpgradeDrivetrain U JOIN Data_Car D ON D.Id=U.Ordinal "
        "WHERE U.DrivetrainID=S.DrivetrainID ORDER BY U.IsStock DESC,D.Id LIMIT 1), "
        "'#'||S.DrivetrainID) AS Name,'FD '||S.FinalDriveRatio||' / '||S.GearRatio1||' / "
        "'||S.GearRatio2||' / '||S.GearRatio3||' / '||S.GearRatio4||' / '||S.GearRatio5||' / "
        "'||S.GearRatio6||' / '||S.GearRatio7||' / '||S.GearRatio8 AS Description, (SELECT IconPath FROM "
        "Upgrades WHERE TypeId='15' AND Level=2 AND IsException=0) AS IconPath, (SELECT ImagePath FROM "
        "Upgrades WHERE TypeId='15' AND Level=2 AND IsException=0) AS ImagePath FROM FH2_TransmissionSources "
        "S WHERE EXISTS(SELECT 1 FROM FH2_TransmissionCatalog C WHERE C.Level=16+S.SourceKey)", NULL);
    valid = valid && execute(database,
        "SELECT COUNT(*) FROM FH2_TransmissionCatalog C JOIN List_UpgradeDrivetrainTransmission P ON "
        "P.Id=C.Id", &collisions) && !collisions;
    valid = valid && execute(database, "SELECT COUNT(*) FROM FH2_TransmissionCatalog C JOIN List_UpgradeDrivetrainTransmission P ON "
        "P.DrivetrainID=C.DrivetrainID AND P.Level=C.Level", &collisions) && !collisions;
    valid = valid && execute(database,
        "SELECT COUNT(*) FROM FH2_TransmissionLabels L JOIN Upgrades U ON U.Id=L.Id OR (U.TypeId='15' AND "
        "U.Level=L.Level)", &collisions) && !collisions;
    valid = valid && execute(database, "SELECT COUNT(*) FROM Upgrades WHERE TypeId='15' AND Level=2 AND IsException=0", &marked) && marked == 1;
    valid = valid && execute(database, "DROP INDEX FH2_TransmissionLevelCollision", NULL);
    valid = valid && execute(database, "DROP INDEX FH2_TransmissionDrivetrainLevel", NULL);
    valid = valid && execute(database,
        "INSERT INTO "
        "List_UpgradeDrivetrainTransmission(Id,DrivetrainID,Level,IsStock,ManufacturerID,MassDiff,Price,MomentInertia,GearShiftTime,FinalDriveRatio,NumGears,GearRatio0,GearRatio1,GearRatio2,GearRatio3,GearRatio4,GearRatio5,GearRatio6,GearRatio7,GearRatio8,CVTShiftTime)"
        " SELECT "
        "Id,DrivetrainID,Level,IsStock,ManufacturerID,MassDiff,Price,MomentInertia,GearShiftTime,FinalDriveRatio,NumGears,GearRatio0,GearRatio1,GearRatio2,GearRatio3,GearRatio4,GearRatio5,GearRatio6,GearRatio7,GearRatio8,CVTShiftTime"
        " FROM FH2_TransmissionCatalog ORDER BY Id", NULL);
    valid = valid && execute(database,
        "INSERT INTO Upgrades(Id,TypeId,IsException,Name,Level,Description,IconPath,ImagePath) SELECT "
        "Id,'15',0,Name,Level,Description,IconPath,ImagePath FROM FH2_TransmissionLabels", NULL);
    valid = valid && execute(database,
        "CREATE TABLE FH2_TransmissionSwapReady AS SELECT 1 AS Version,COUNT(*) AS AddedRows FROM "
        "FH2_TransmissionCatalog", NULL);
    valid = valid && execute(database,
        "DROP TABLE FH2_TransmissionCatalog", NULL);
    valid = valid && execute(database,
        "DROP TABLE FH2_TransmissionLabels", NULL);
    valid = valid && execute(database,
        "DROP TABLE FH2_TransmissionSources", NULL);
    valid = valid && execute(database,
        "DROP TABLE FH2_TransmissionTargets", NULL);
    valid = valid && execute(database,
        "CREATE INDEX IF NOT EXISTS FH2_TransmissionDrivetrainLevel ON "
        "List_UpgradeDrivetrainTransmission(DrivetrainID,IsStock,Level)", NULL);
    valid = valid && execute(database,
        "CREATE INDEX FH2_TransmissionMetadata ON Upgrades(TypeId,IsException,Level)", NULL);
    valid = valid && execute(database, "SELECT AddedRows FROM FH2_TransmissionSwapReady", &added);
    if (!valid || !execute(database, "RELEASE fh2_transmission_swaps", NULL)) {
        execute(database, "ROLLBACK TO fh2_transmission_swaps", NULL);
        execute(database, "RELEASE fh2_transmission_swaps", NULL);
        log_append("TRANSMISSIONS: expansion rolled back (invalid data or reserved ID/level collision); authored gearboxes retained.\r\n");
        return 0;
    }
    log_append_value("TRANSMISSIONS: added gearbox variants in memory", (int)added);
    log_append("TRANSMISSIONS: donor/destination drivetrain IDs outside 1..8191 or donor levels outside 0..7 retain authored choices only.\r\n");
    return 1;
}

#ifdef FH2_TWEAKS_TEST
__declspec(dllexport) void Fh2TestTransmissionSwapsConfigure(int value) { transmission_swaps_configure(value); }
__declspec(dllexport) int Fh2TestTransmissionSwaps(void *database) { return transmission_swaps_expand(database); }
#endif
