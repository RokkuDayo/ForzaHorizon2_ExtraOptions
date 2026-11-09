# Treasure Map and offline Extras investigation

Supported executable: SHA-256
`3ec04c6163fb5614e3d351ba0570d39b846041a257715d6340eceb03ccb7f0a3`.
All addresses below are RVAs in that build, whose preferred base is 0x140000000.

## Why the 0.2.16 override was insufficient

PlayerState stores TreasureMapPurchased at byte +0x79 and
IslandTreasureMapPurchased at +0x78. The previous release changed the shared
getters at 0x4028f0 and 0x1657060. The log confirmed these patches were applied,
but the user observed no map reveal.

The shared main getter also serves an unrelated NPC property. The Island
purchase handler at 0xb69550 calls its shared getter to decide whether the
benefit has already been applied; forcing true can skip that apply path.
BarnFindBehaviour reads the raw main flag instead of calling a getter, and
DiscountBoardBehaviour initializes its map state from saved board discovery.
Ownership-query tests alone did not establish the benefit's application.

## Revised implementation

`Content.UnlockTreasureMap` remains independent of UnlockDLC and defaults to 0.
The shared getters are restored even when this option is enabled. Their old
0.2.16 replacements remain accepted so the transaction can safely restore them.
The registered Treasure Map property delegates at 0xa5e9da and 0xa5e9f7 instead
point to an existing boolean-true leaf at 0x84fda0. This limits the override to
those two properties. The two inline WorldMap ownership reads at 0xd72db4 and
0xd72dba still return true when a valid profile is present, preserving region
selection and missing-profile guards.

`fh2/treasure_map.c` wraps the DiscountBoardBehaviour setup callback at
0x50b7c0. After native initialization, state 0 triggers the native discovery
routine at 0x4fe910 with state 1 and the board's position at +0x128. Runtime
state is at +0x100. Discovered state 1 and collected state 2 are left alone.
The native routine updates the profile's board record, dispatches the state
change, writes the reflective State property and sends normal map notifications.
The wrapper neither invokes the board-collection reward nor forces state 2.

The metadata registration instruction at 0x4f5771 loads the wrapper pointer
from an aligned eight-byte slot at 0x4028f8, originally unused INT3 padding.
There is no function-entry trampoline. The slot is validated and installed
through the same transaction as other callbacks, and the DLL is pinned while
the callback is enabled.

BarnFindBehaviour setup at 0x405c40 has a direct PlayerState+0x79 gate at
0x405e86. Bypassing that conditional branch reaches its native
ShowMapElement=true setter. Valid-model and registry checks remain active;
this reveals location markers without awarding the barn car or skipping its
ordinary progression. Storm Island still requires its mounted content.

The option does not write Treasure Map purchase flags. Board discovery does
use the native save path, so recorded locations may remain discovered after
the option is disabled. Disabling is not an operation that erases discoveries.

## Offline Extras

The local game database includes AddOnTable records for TreasureMap and Tokens.
TreasureMap has AddOnId 1, TokenCost 150, localized name/description identifiers
and bundled AddOn/TreasureMap_Big and TreasureMap_Small textures. ContentOffers
also contains offer Id 8, ProductId d013912f-ac6e-4624-ac25-d0854321598c.
The art is present in Media/ui/textures/data_bound/AddOn.zip.

The Extras button handler at 0xdbdd60 and listing query builder at 0xd783b0
call the online/store predicate 0xb00b10. When it returns false, both known
types are excluded and the button reports that there are no Extras.
The Treasure Map option overrides only these calls at 0xdbdd90 and 0xd783da.
The listing keeps the separate Tokens predicate at 0xb00c30, so offline token
availability is not changed. `Frontend.EnableMarketplace` still controls access
to the pause Marketplace tab. Local browsing does not restore purchases.

## Validation

`development/tests/test_treasure_map.py` runs in a private relocated image of the owner's
executable. It covers 960 getter/setter/query checks, 64 registered property
delegate checks, 96 board-discovery checks and 64 barn-visibility checks.
The board test executes the mod callback binding and native discovery routine;
initial setup and external storage/event endpoints are mocked. It verifies
discovery dispatch, idempotence and preservation of collected state.
The barn test executes initialization through ShowMapElement, mocking services
and skipping the subsequent unrelated car-description query.

The same test executes the complete Extras SQL builder with mocked online and
string services. Forty-eight checks verify its results against the read-only
local game database and verify the referenced bundled images. Purchase-query
calls leave profile bytes unchanged; no live game or personal save is opened.

Runtime tests cover every option combination, callback validation, restoration
and failure rollback. The actual Azimuth helper-loader test uses the release
adapter. These isolated tests do not render the map or Extras UI and cannot
establish discovery persistence in a live save; gameplay testing remains needed.
