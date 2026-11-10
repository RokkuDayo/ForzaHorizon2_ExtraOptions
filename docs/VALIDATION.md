# FH2 manufacturer jump: recommended cars

Jump target resolution skips the native per-entry IsRecommended flag (+6c,
confirmed against getter c2b4d0). Regular manufacturer entries remain eligible
even when the same car has a recommended copy at the front. Existing current-car
fallback and alphabetical picker order remain. The target is resolved again
when the modal closes, preserving live filters/reordered collections. No regular
copy means no jump, rather than selecting a recommended entry. The native-image
manufacturer suite passes, including recommended duplicates, list changes,
recommended-only filtering, current-car fallback and all six wheel categories.
Live rendering/input confirmation remains an in-game check.

## Save bypass removal — 3 November 2026

Removed the Azimuth save hash/size rejection patches and their build entry.
FH2 now uses GenerateMissingSaveCommits independently, matching FM5. The retired
INI key is ignored. Private tests passed for all nine new/retired option
combinations, 264 marker-repair cases, title isolation for both games, and eight
native Azimuth v1.0.4 size/hash decisions with the host image unchanged.
The removed bypass test export is absent. This does not establish any change
to antivirus detections; no antivirus clearance is claimed.

# v0.3.12 main vignette — 29 October 2026

Native typed constant-buffer upload test passes through the production vtable
hook and original native uploader into a supplied graphics sink. On preserves
all 9008 bytes; Off changes only the two vignette alpha fields sent to the GPU,
then restores all authored bytes immediately. Runtime re-enable, speed-zone
independence, persistence and v19-to-v20 migration pass. Main vignette defaults
On. Existing native presentation, options/menu/property tests pass; the new HUD
row is directly above Speed Zone Vignette. Localization checks pass for 16
language codes and 64 generated pages. Both production adapters build.
No live rendering test was performed.

# v0.3.11 shared database identities — 29 October 2026

Both production adapters built. Native SQLite/load-order fixtures pass for two
cars sharing one row, all-column winner selection, reversed priority, three
contributors, mixed sharing/nearest/error policies, base IDs, composite/string
keys, exact rollback after a later table fails, and preservation of existing
reference remapping/carbin behavior. The suite also merges real wing rows
against the complete 288-table vanilla database. Shared overrides use UPDATE,
not REPLACE, preserving dependent rows. All 16 diff wizard tests pass, including
selection and export of policy.id_conflicts=last_wins. No live game test performed.

# v0.3.10 player daylight headlights

Both production adapters built. The new isolated upload fixture verifies player
headlight beam/glow selection, untouched brake and unrelated presets, duplicate
preset handling, immediate CPU restoration, cached GPU invalidation on On/Off,
invalid handle rejection and no repeated invalidation on unchanged frames.
Existing native vehicle-control tests pass, including Photo Mode restoration,
car changes, menu guards, roofs and popup headlights. Native upload call sites
and bridge padding pass the supported game-image patch guards.

The native ForwardPlus uploader (10f6c40) maps preset +9c/+a0 to GPU threshold
and transition; LightGlow uses +44/+48. Shader comparison admits activation zero
when both fields are zero. Original CPU data is restored after each upload.
No global environment factor is changed. No live game/GPU rendering test was
performed; daylight beams still require user confirmation.

# v0.3.9 Photo Mode hour row

Both production adapters built. Existing native menu/property tests pass.
Across sixteen languages, semantic template comparisons verify that Locked Hour
retains the native numeric-row and fill templates, including names and focus
triggers. Expected differences are resource keys, hour bindings, quarter-hour
input steps, and extra readout width. Native property calls verify normalized
positions 0, 0.5 and 1 map to valid quarter-hours, update hour and position
bindings, reject invalid values, and retain the save sampling guard.
No live game/GPU rendering test was performed.

# v0.3.8 layout adjustment

Locked Hour now specifies TextAlignment=Right, matching the native effects
values. The existing right-aligned value field and vertical center are retained.
No clock or weather behavior changed. Both production adapters built; existing
native settings/layout checks pass. Live visual confirmation remains external.

# v0.3.7 validation — 27 October 2026

Both production adapters built. Native menu/property fixtures pass, including
all sixteen localized layouts. The hour value occupies a 22-pixel lane aligned
with the slider and is centered within that lane. Weather Control 1.0.3 fixtures
cover unset visibility/opacity values and restore both when closing the modal.
No live game/GPU rendering test was performed. Pause hiding remains unconfirmed.

# v0.3.6 validation — 27 October 2026

Both production adapters built. Native menu/property fixtures pass. All sixteen
localized Photo Mode layouts are checked for hour-style dependency order,
explicit 72-pixel row height, unchanged slider spacing and float binding.
Weather Control 1.0.2 fixtures target a separate named visual and verify the
page controller is untouched, visibility is restored, and references balance.
No live game/GPU rendering test was performed.

# v0.3.5 validation — 27 October 2026

Both production adapters built. Private native fixtures pass for temporary Photo
Mode hour/toggle controls, exact clock restoration, stale managers, weather and
headlights. Native property tests verify float hour binding and accumulation
resets for all five custom effects controls, along with saving suppression.
All sixteen localized labels and custom hour layouts parse. Normal settings,
Azimuth 1.0.3 handoff/title gating and Windows forwarding checks also pass.
No live game/GPU test was performed.

# v0.3.4 validation — 27 October 2026

Built both production adapters. Private native fixtures pass for Azimuth v1.0.3
handoff/title gating, native winmm forwarding, and normal video/HUD/audio settings.
The Weather Control page passes verbatim through the core resource callback;
its standalone marker prevents normal Video settings from being appended.
Plugin fixtures cover modal/event guards, controller and keyboard rebinding,
held input/focus changes, native page URI assignment and ownership, and reset
isolation. All sixteen localized authored pages parse and resolve their static
resources against the game's UI library. Original clock/weather fixtures pass,
including event controls and restoration. No live menu/GPU test was performed.

## Previous validation

# v0.3.3 validation — 27 October 2026

Azimuth v1.0.3 has an independent host/header/full-file fingerprint, startup,
mapper and save-guard profile. Current installation is inspected read-only.
Both production adapters rebuild byte-for-byte in separate output directories.
Windows VERSIONINFO fields were verified through the Windows version API.
Production export lists have no Fh2Test symbols.

Private native tests pass for handoff/title gating/restoration and all sixteen
save-validator decisions on each of Azimuth v1.0.1, v1.0.2 and v1.0.3. The v1.0.3
graphics profiles pass photo and livery tests. Also passed: 64 representative
runtime configurations, 264 filesystem marker cases and winmm forwarding.
No live v1.0.3 game entry point, GPU rendering or FMOD playback is executed.
No FPS improvement, signing or antivirus clearance is claimed.

## Previous validation

# v0.3.2 validation — 27 October 2026

Built production Azimuth `winmm.dll` and Mezcal `xg.dll` with the experimental
startup dialog removed. Both retain the experimental aero warning in the log.
The Azimuth adapter retains its separate initialization-error dialog.
Production exports contain no test API.

Passed private native tests for 64 representative runtime configurations,
Azimuth v1.0.2 loading/title gating and rollback, its 16 save-validator cases,
and all 180 named winmm forwards plus ordinal 2. No engine/audio behavior changed.
No live gameplay or FMOD playback test was performed.

## Previous v0.3.1 validation — 27 October 2026

Target: the installed Azimuth v1.0.2 executable. Azimuth v1.0.1 remains supported
through its separate exact host fingerprint, addresses and save-validation guard.

Passed in private Windows mappings for both host releases:

- One-shot loading handoff, all four native arguments, actual-package FH2 title
  gating, executable checks, inactive other titles, altered/truncated file refusal,
  changed code/padding refusal, restoration and protection-failure rollback.
- Sixteen native save size/hash decision cases per release, idempotence, altered
  guard rejection and rollback. v1.0.2 additionally checks its two-page region.
- Photo sampling and livery override/default behavior using each host's actual
  graphics profile data. Native counts and dimension helpers remain correct.
- Missing-marker repair in 264 private filesystem fixtures.

The new host's mapper, package parser and save validator retain the same
instruction shapes after relocation normalization. Only the host profile and
release metadata changed; FH2 gameplay hooks, settings storage and plugin ABI
remain unchanged. Production forwarding and release export checks are recorded
with the release metadata.

No actual launcher/game entry point, graphics, FMOD playback or personal saves
are executed by these fixtures. The handoff test uses a mapper stand-in; a full
v1.0.2 game launch still needs user confirmation. No FPS change is claimed.
