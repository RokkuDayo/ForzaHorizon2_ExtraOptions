# TRD parts logo lookup

Verified against the vanilla `media/stripped/gamedbRC.slt` and English string table.

`List_PartManufacturer` contains:

| Id | PartManufacturer | LogoPath |
|---|---|---|
| 256 | `_&1568994440` | `decal388` |

The localized name resolves through `List_PartManufacturer.str`, string ID 62600,
to **TRD**. Most upgrade tables reference this row using `ManufacturerID` /
`ManufacturerId`; wheels use `PartManufacturerID`.

The UI constructs the path:

`GAME:\Media\UI\Textures\Data_Bound\Aftermarket_Logos\decal388.swatchbin`

`Aftermarket_Logos.zip` does not contain that entry. `Livery.zip` separately has:

- `Decals/decal388.modelbin`
- `Decals/Textures/decal388.swatchbin`

The database already specifies the TRD logo basename. Supplying a UI-compatible
texture at the expected UI path is the missing asset step, rather than adding
another brand or pointing `LogoPath` at the livery directory. The decal modelbin
is geometry, not a UI logo. No database or game files were changed during lookup.

A media mod would supply the texture under:

`<mod>/media/ui/textures/data_bound/Aftermarket_Logos/decal388.swatchbin`

The existing decal texture is a candidate source. Its UI rendering has not been
tested here; the modelbin is not needed for the upgrade-menu logo.
