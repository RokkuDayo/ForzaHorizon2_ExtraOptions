# Mainland festival startup

> Current control (0.2.62): Settings → HUD → Spawn Location replaces the two
> startup INI switches. Default uses neither override, Car Meet Entrance enables
> the car-meet bypass, and Festival Site also adds festival placement. Old keys
> are accepted for one-time migration into the version-5 sidecar. Restart to apply.

`Frontend.SkipCarMeetOnStartup` bypasses mainland car-meet boot.
`Frontend.SpawnAtFestival` optionally changes the resulting placement to the
main festival. Both default to 0 when omitted; the supplied INI enables both.
Festival placement requires the car-meet bypass. It changes mainland startup
placement after the opening experience, while retaining Storm Island's normal
boot path.

## Inspected native path

The supported FH2 image is the build recorded in `fh2/patches.json`. Addresses below
are RVAs relative to its loaded image, not absolute addresses.

`DetermineBootType` calls the predicate at `0x87abe0`. Its
`booting_to_island` property selects separate mainland and island branches.
Both read the `boottocarmeet` startup option at configuration offset `0x2e94`.
The mainland conditional branch at `0x87ac66` is changed from `74 18` to
`eb 18`, returning false for mainland car-meet boot. The island branch at
`0x87ac29` and its progression checks remain untouched. The outer boot logic
still prioritizes the initial experience and special boot modes.

Skipping car-meet boot alone is insufficient: the free-roam startup restores
the saved car position. The shared CXML4Resource parser hook therefore edits
only the placement node inside `free_roam_start.xml`'s `cold_boot` state. It
leaves the preceding `setup_free_roam_offline` and subsequent streaming wait,
world finalization, input setup and camera handling in their original order.

The added nodes use existing states to select the active track and check
`global.is_ie_complete`. Mainland saves with a completed opening experience
use `global.place_player_car_at_route_locator`, resolving
`festival_locator_garage_exit` from `track_route/route`. This is the actual
named locator in `Tracks/Anthem/trackroutes/Route0.nt`, and it is referenced by
the festival garage exit entity. `fast_travel_festival`, used in 0.2.39, does
not exist in that route file; the map entity instead supplies a fallback
position. A route-locator lookup of that name leaves the car unmoved. Island
startup and unfinished opening experiences call the original restore-point state.

No world identifier, save file or fixed world coordinates are overwritten.
Placement runs on the normal loading flow; it adds no thread, timer or hook
descriptor. It shares the existing frontend parser callback.

## Failure and verification

Unexpected or ambiguous cold-boot text is refused. Allocation/parser failures
disable this XML family for the session. The independently requested
car-meet bypass remains enabled.
Garage and race-dialogue changes remain independent. The original parser,
allocation ownership and resource cleanup are retained.

`development/tests/test_xml.py` executes the real boot predicate in a private mapped image,
compares island results before and after the option, executes the native
opening-experience check and follows the resulting XML placement paths. It
also checks the three XML families, the separate bypass dependency,
the actual route locator and its transform, resource failure
isolation and unchanged surrounding startup XML. Runtime and production
Azimuth loader tests cover the INI, master switch and original-byte checks.

These are native fixture tests. Festival placement in the full running game
still requires an in-game test by the user.
