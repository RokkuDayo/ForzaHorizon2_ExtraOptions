# Storm Island Autoshow requirements

The native game explicitly checks two car ordinals:

| Ordinal | Database media name | Car | Base credit cost |
|---|---|---|---|
| 2145 / 0x861 | FOR_RangerT6_14 | 2014 Ford Ranger T6 Rally Raid | 875,000 |
| 2143 / 0x85F | CHE_7_SST_13 | 2013 Chevrolet #7 Stadium Super Truck | 100,000 |

The IslandIE save-state lookup at RVA 0xA65E70 obtains `islandiestate` from the
player save-state collection. The Ranger uses byte +0xC; the SST uses byte +0xD.
These offsets are confirmed by the two native consumer branches. The exact
events that set the flags have not been traced in this change.

## Availability

The predicate at RVA 0xB02BA0 first evaluates normal car eligibility and rejects
barn-find/traffic entries through its existing SQL. Its final BL result reaches
0xB02F65. If false, it returns false without evaluating Island progress. Otherwise
it calls the save-state lookup at 0xB02F69 and forces false for either named car
whose corresponding Island flag is zero. All other cars retain BL.

The patch changes `E8 02 2F F6 FF` at RVA 0xB02F69 (file offset 0xB02569) to
`EB 2A 90 90 90`. It jumps to 0xB02F95, which returns the original BL eligibility
value. The earlier eligibility, barn-find and traffic filtering remain intact.

## Purchase dispatch

The complete handler at RVA 0xCACEA0 obtains the chosen record's ordinal through
its native virtual getter at +0x38. Its lookup call at 0xCACEC9 checks the same
two Island flags. A locked result calls the Island-unavailable popup helper at
0xCB3110, whose localized keys are IDS_IslandCarUnavailable_Title/Message, then
returns without opening the purchase screen.

The patch changes `E8 A2 8F DB FF` at RVA 0xCACEC9 (file offset 0xCAC4C9) to
`EB 33 90 90 90`, reaching the normal path at 0xCACEFE. It passes the selected
ordinal through the original controller and scene 0x59 transition. There is no
forced gift, price change, ownership grant or saved event/unlock modification.

Both patches are controlled by Content.UnlockStormIslandCars, default 1. Setting
it to 0 restores both original sites in memory at startup. Global.Enabled=0
continues to skip all memory changes. The engine verifies both sites along with
every other patch before writes, supports relocation, and leaves disk files alone.

## Native regression

tests/test_autoshow.py uses the owner's game executable and read-only database. It
checks the above ordinals/prices, maps the game privately at a different base,
executes the original gate with its exact stack frame/epilogue, and executes the
entire original purchase handler with service recorders. Both Island flags,
earlier eligibility on/off, toggle on/off and every one of the 354 database cars
are exercised: 5,664 gate cases and 2,832 complete handler cases. The saved state
bytes remain unchanged. With the toggle enabled, both trucks also work without
a save-state lookup result. Disabling restores the exact original instructions.
The former pause-tab slots remain unchanged throughout. Source EXE/DB hashes
are unchanged. The full game, renderer and real credit transaction are not run.
