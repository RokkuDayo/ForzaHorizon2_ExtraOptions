# Show All Events

The in-game Show All Events option exposes standalone offline races through the game’s native route activation entities.

When enabled in offline free roam:

- Only routes on the current map are enabled: Mainland routes on the Mainland and Storm Island routes on Storm Island.
- Standalone routes used by the current championship are hidden. The championship’s original event entities and orange markers remain controlled by the game.
- Playground routes (King and Infection) are hidden by default.

To expose playground routes, set the following startup INI option:

```ini
[Frontend]
ShowPlaygroundEvents=1
```

This option does not bypass the map or championship filters. Default: `0`.

When Show All Events is disabled, or during a network session, the standalone gates follow the game’s original feature requests.

## Implementation and checks

Each standalone route receives a unique native FeatureToggle identifier while its activation entity is constructed. Native FeatureEnabled/FeatureDisabled messages update that gate, covering both the marker and its interaction. Championship entities are not renamed or patched.

The filter uses the native CPlayerManager and CPlayer profile getters, then reads the active campaign from CForzaProfile’s typed CareerState save-registry entry. The current map comes from the loaded MediaTrack, using the same source as the native route-activation logic; career progress counters are not used to identify the map. Temporary registry references are released after reading. Route membership comes from the merged runtime database, including addon data. State is checked at most four times per second in free roam; database queries and feature messages are skipped when the state is unchanged. Filters refresh after changing the map, campaign, option, network state, or constructing a route entity.

`source/development/tests/test_event_filters.py` verifies the patched constructor thunk, native FeatureToggle callbacks, all 198 vanilla campaigns on both maps, 28 playground routes, unchanged-state caching, missing-state fallback, addon routes, and recreated entities in a private relocated FH2 image. This does not replace in-game marker validation.
