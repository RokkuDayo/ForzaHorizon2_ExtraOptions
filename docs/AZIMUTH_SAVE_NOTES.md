# Azimuth save commit generation

`Compatibility.GenerateMissingSaveCommits=1` generates missing v2 save commit
markers before the game reads its saves. It is available independently in
FH2_ExtraOptions.ini and FM5_ExtraOptions.ini and defaults to On.

The generator scans only the active supported title's save directory beside
Azimuth: `data/titles/1A96545B/saves` for FH2, or
`data/titles/7B35D917/saves` for FM5 in builds with FM5 support. Each new commit
contains the actual blob names, lengths and SHA-256 hashes. Existing commits
and blob contents are never overwritten. Incomplete, malformed or unreadable
revisions are left untouched. Publication uses a temporary file and refuses
to replace a concurrently created commit.

The previous `DisableAzimuthSaveChecksums` feature has been removed, including
its host patch code. Old INI entries are ignored. Native checksum, file-size,
marker-format and revision-selection validation remain enabled in both games.
External changes to blobs with an existing commit may therefore fail validation;
missing-marker generation does not repair stale existing commits.

Private fixtures exercise exact generated counts/sizes/hashes, repeat runs,
existing-marker preservation and invalid/locked revisions. Separate native-host
fixtures confirm mismatched sizes and hashes are rejected. Personal saves are
not accessed by these tests. Mezcal has no Azimuth commit-generation step.
