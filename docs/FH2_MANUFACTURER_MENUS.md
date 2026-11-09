# FH2 car-selector coverage

Jump to manufacturer uses the existing View / Tab bindings on every native
car-carousel page. Each page retains its original initializer, filters,
eligibility and select/purchase behavior. The picker only changes the highlighted
index in that page's live collection. Its alphabetical names, initial selection,
three-column LB/RB jumps and recommended/current-car handling are shared.

Covered primary page types (verified against the supported executable's RTTI
and virtual methods):

- Buy, Garage and manufacturer-filtered Buy.
- Career Buy/Garage, Destination Buy/Garage and MiniCareer.
- Buy/Garage Horizon Recommends and IEBuy.
- Livery Garage, AllCars and their base page.
- BarnFinds and PhotoCollection.
- Lobby, OnlineFreeRoam, RoadTrip, CarMeet and Rivals.
- MarketplaceCarSelect and the generic CarSelect base page.

These are all 23 primary types whose native hierarchy includes CarSelect.
Their collection and selector are inherited at +428 and +a08, and all retain
the native update function at RVA 0xcae2d0. Only their +488 initializer and
+4c8 update slots are patched. The scripted three-car InWorldCarSelect and
CarSelectInfoLabel are different controls and remain untouched; shared popup
list styles are not modified by this expansion. Existing rim categories retain
their separate Y binding and input state.

Validation: the private native-image manufacturer fixture checks all 23 type
names and original methods, calls every installed initializer/update hook,
and exercises prompts, held-key suppression, keyboard/controller remapping,
focus loss, modal guards and native-call preservation. Existing car filters,
recommended duplicates, pinned cars, rim categories and grid paging tests pass.
UI rendering and event flow still require an in-game check.

The full FH2 patch-transaction suite also passes (168/183 protected pages,
including cross-page patches and failure rollback), as does FM5's manufacturer
suite covering all 12 selectors, scoped popup styles and 118 patch guards.
Azimuth's production winmm.dll was rebuilt with both game implementations.
