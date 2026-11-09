# Manufacturer tile names and logos

FM5 lacks the later FZTextBlock.AutoScroll control used by the supplied FH6
reference. Its basic TextBlock exposes TextTrimming, but the inspected measure
and arrange path does not insert ellipsis glyphs: names were simply clipped.
The manufacturer picker now supplies literal trailing `...` for overflowing
names, once when constructing its items. It does not attempt autoscrolling.

Trimming uses advance metrics from FM5's HelveticaNeueLTPro-MdEx.vfont at the
existing font size 22, with a 222 logical-pixel budget including the dots.
The compact metrics table includes Latin accents and punctuation. Unknown
glyphs reserve a conservative full em; surrogate pairs are not split. Full
localized names remain intact for alphabetical sorting, matching and navigation.
Text stays left aligned in the original 222-by-30 viewport. Short labels reuse
the existing native localized string. No font outlines or textures are copied.

The timer, scrolling properties, hidden measuring TextBlock and popup update
hook have been removed. There is no per-frame text work or UI object traversal.
Aftermarket logo cropping and all grid navigation are unchanged.

Validation: private mapped-executable fixture passes native item construction,
localized names, full collection navigation, lifecycle cleanup, resource checks,
trimming width budgets and all 31 rollback/isolation sites. Rendering boundaries
are mocked; visual confirmation still requires an in-game test.
