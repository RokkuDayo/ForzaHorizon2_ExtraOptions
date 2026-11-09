# FM5 gamerpic badge

The first badge in Forza Profile → Change Badge → Player Status uses Azimuth's
configured `data/player.png`. It is immediately selectable and uses the regular
badge-selection save field. Existing badges retain their unlock conditions.

The native virtual texture path `GAME:\Media\UI\Textures\Data_Bound\Achievement_Badges\Player_Status\__eo_gamerpic__.swatchbin` is redirected
to `data/player.png` itself through a dedicated manifest-free native file-system
volume. A Windows file alias alone does not expose new root-level assets. The translation layer accepts PNG contents through
this asset path. There is no conversion, resampling, generated texture or cache.
Old generated gamerpic cache files are ignored. Changing Azimuth's picture is
picked up on the next game launch. Missing or invalid PNG signatures leave the
original badge list alone.

A connection-local SQLite TEMP view appends the entry after the mod merge.
The source database, merged database cache, and existing badge rows are not
edited. Native queries resolve the selected badge's texture wherever the game
displays it. The built-in virtual texture alias also works with Mods and
EnableMediaOverrides disabled. The entry reserves badge ID `64888` (`0xfd78`)
and does not replace an existing row at that ID.

FM5 player-card snapshots store the equipped badge in a 16-bit field. Native
`0x2a5ab0` reads it at `snapshot + 0x44` and passes it to the ordinary texture
resolver, `0x4bc0e0`. The original EO ID, `2147483000`, exceeded that range.
The new ID is its low word, so both existing player-card snapshots and newly
selected badges use the same texture. A hidden lookup-only row for the old
full-width ID also resolves existing profile selections. It has neither a
selectable badge nor a title, so only one gamerpic tile is shown. No profile
migration or save-file edits are needed.

Validation: `source/development/tests/test_fm5_badge.py` uses a private mapped
FM5 executable, its actual SQL formatter, SQLite queries against the original
badge schema, and scratch files. It checks the direct PNG alias, default unlock,
category filtering, original database preservation, selected-texture resolution,
the native player-card refresh and texture resolver with both old and new IDs,
worker-thread use, ignored stale texture caches and missing-picture fallback.
The native resolver test `test_fm5_badge_files.py` additionally exercises FM5's
actual volume constructor and mount lookup, verifying the picture resolves
through that volume and nonexistent neighboring assets do not.

The details panel receives the literal UI string "Your gamerpic." because the
native localized-description reader does not display arbitrary database text.

In-game rendering still requires testing. The running game and personal saves
are never accessed by the test.

Usage: `python test_fm5_badge.py TEST_DLL FM5_EXE NEW_SCRATCH`
