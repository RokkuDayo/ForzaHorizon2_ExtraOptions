# Ineligible-car race dialogue — 0.2.35

Settings → HUD → Skip Championship Dialogue skips the blocking warning before
race car selection. It defaults to Off; On skips the original warning.
The menu choice is saved for the next launch because the resource edit is made
before the native parser creates the cached flow. The old INI key seeds the
setting during a one-time migration. Qualification rules and the existing
car-selection screen remain in use.

## Shipped activation flow

The owner's `media/stateflow.zip` contains `race_trigger_activation.xml`.
Inside `offline_race_entry_flow`, the `is_car_eligible` node sends its `no` exit
to `play_car_ineligible_vo`. That node invokes `global.trigger_vo` with
`target="IneligibleCar"` and `blockui="true"`, then exits to
`prepare_for_car_screen`.

In the owner's `global.xml`, `trigger_vo` constructs the `TriggerVO` behavior
and forwards the `blockui` parameter. The native parameter-registration method
at RVA 0x942690 registers that exact property. Race activation also holds game
control through its enclosing flow while the UI voice-over runs, accounting
for the wait before the car-selection screen.

The owner's `media/audio/vo/DialogueScript.xml` maps `IneligibleCar` to
`Ben/HZ2_VO_Ben_FeatInfo_IneligibleCar_1`. Its separate `WrongCar` trigger maps
to two other Ben lines and has a 30-second cooldown.

## Scoped resource edit

The mod replaces only the warning node's opening tag with a node of the same
ID using `global.dummy_state`. This is an empty state in the shipped global
definitions; it adds no voice, UI block, input lock, timer or new behavior.
The existing exit still targets `prepare_for_car_screen`, which performs the
normal streaming/cutscene/UI preparation before the normal car-selection screen.
No eligibility result is changed, and no other warning or voice-over is muted.

Before editing, the planner validates the unique enclosing flow, warning tag,
incoming ineligible exit and existing preparation destination. It rejects
missing, duplicated, changed, truncated, oversized or NUL-containing inputs.
An unsupported resource or allocation failure is logged and passed to the
original parser unchanged.

The existing CXML4Resource parse slot at RVA 0x2ec4fe0, whose original function
is 0x13de7e0, serves both frontend features. The hook is installed when either
the pause-garage option or dialogue option is enabled. Each resource family
checks its own setting and failure state. Failure in one does not disable the
other. Settings routing commits after a successful patch transaction, including
when the hook pointer already matches. The legacy `garage_xml_parse_hook`
descriptor name and test exports remain for compatibility.

The raw resource uses buffer/length fields +0xe0/+0xe8 and the original game
allocator/free methods at RVAs 0x3229c0/0x31c7b0. Ownership remains with the
native parser/cleanup as documented in `GARAGE_NOTES.md`. No archives, original
game files or saves are modified.

## Separate free-roam reminder

`WrongCarForChampionshipComponent` has primary vtable RVA 0x2cd1f30 and an
update method at 0x723c60. The shipped mainland and Storm Island free-roam
entity definitions give its `wrong_car_vo` instance `PlayTime=35`. Its update
checks eligibility through 0x71deb0 and requests `WrongCar` through 0x5117b0.
This is a periodic free-roam reminder, separate from the race activation node
with explicit UI blocking. The new option leaves that component unchanged.

## Validation boundary

The XML regression test reads the owner's archive and checks that only this
opening tag changes. It confirms the instant-exit state and retained destination
against the shipped definitions. Native raw-load, hooked parse, allocation and
cleanup paths run in a private relocated image, with file services and the XML
parser mocked. It checks all four garage/dialogue setting combinations, both
failure directions, disabled pass-through, ownership and failed-preflight routing.
The full stateflow scheduler, actual voice playback and in-game transition are
not executed by these fixtures; an in-game check remains necessary.
