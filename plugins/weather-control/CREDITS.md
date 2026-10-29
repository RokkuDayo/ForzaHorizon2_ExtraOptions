# EO Weather Control credits

Clock and weather controls are adapted from **Set Time & Weather FH2 by Pantsedit**,
provided under GPL-3.0. The original attribution is preserved below.
Extra Options integration adds the separate pause-menu page, bindings, localized
resources, scoped reset handling, and Windows build/metadata support. The adapted
source is distributed under the same GPL-3.0 license; see LICENSE.

This version uses its own authored XAML pages; it does not alter Settings > Video.
It is built with Zig's Clang and lld-link and uses Windows system DLL imports.
The upstream implementation and development-tool acknowledgments below describe
the original project, rather than this version's menu placement/build workflow.

---

# Credits

## Author

- **Set Time & Weather FH2** by **Pantsedit** ([github.com/Pantsedit](https://github.com/Pantsedit)).
- **Claude** (Anthropic): AI assistance.

## Built on

- **Forza Horizon 2 Extra Options** by **Arthur (RokkuDayo)**:
  [github.com/RokkuDayo/ForzaHorizon2_ExtraOptions](https://github.com/RokkuDayo/ForzaHorizon2_ExtraOptions).
  Set Time & Weather is an Extra Options plugin. It relies on Extra Options' mod loader, its plugin
  API (`src/fh2_plugin.h` is Extra Options' plugin API header) and its UI resource hook, which the
  Settings > Video rows chain onto rather than replacing. The way the rows are added as native UI
  properties follows the approach Extra Options uses for its own Video settings rows.
- **Azimuth** by **Almost There Games**:
  [almost-there-games.itch.io/azimuth](https://almost-there-games.itch.io/azimuth).
  The translation layer that runs Forza Horizon 2 on Windows. Nothing from Azimuth is included.
- **Zig** ([ziglang.org](https://ziglang.org/)): `zig cc` 0.15.2 builds the plugin and the
  offline test harness.

## Forza Horizon 2

**Forza Horizon 2** is developed by **Playground Games** with **Turn 10 Studios** and published by
**Microsoft** (Xbox Game Studios). Forza, Forza Horizon and Xbox are trademarks of Microsoft
Corporation; all game content belongs to its owners.

**This project is not affiliated with, endorsed by or supported by Playground Games, Turn 10
Studios, Microsoft, Almost There Games or the Extra Options author.** It contains **no game
files**: no executables, UI files, databases, string tables, textures, audio or other assets from
Forza Horizon 2 or any other game. You need your own copy of the game; the offline tests read it
from your install at a path you give them. Game addresses (RVAs), function, preset and style
names appear in the source and docs only to identify what the mod patches.
