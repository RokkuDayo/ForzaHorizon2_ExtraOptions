# Converting FH2 / FM5 cars for Forza Horizon 3

Extra Options for FH3 loads the car data of a mod; the car files themselves have to be converted once into FH3's
formats. The converter lives in its own repository, FH3-CarConverter (`fh3cars.exe`). This page describes what a
car mod needs and what the converter changes, for someone who wants to build such a mod.

## What you need

- Forza Horizon 3 installed and runnable through Extra Options 0.6.0 or later (the `Mount` folder of the game is
  called `FH3_ROOT` below, for example `D:\Games\FH2\Opus_1.0.125.0_x64__8wekyb3d8bbwe\Mount`).
- The source cars: Forza Horizon 2's `Mount` folder (`FH2_ROOT`) for FH2 cars, or an FH2-format car pack (a mod
  folder with `mod.json`, `database.json`, `strings.json` and `media\cars\*.zip`). Source files are never edited;
  everything is written to a new mod folder.
- ForzaTech Studio installed (default `D:\Games\FH2\ForzaTech.Studio`). The converter uses its
  `ForzaTech Studio.dll`, `libs\ForzaTools.Bundles.dll` and `granny2_x64.dll`. Set `FORZATECH_STUDIO` if it lives
  elsewhere.
- .NET SDK 9 to build the converter.

Rules that FH3 enforces and that the converter follows:

1. The mod folder name must not contain spaces. FH3 cannot see new files under a path with a space.
2. Every car zip must be listed in the game's `media\zipmanifest.xml` with the position and size of its central
   directory. Extra Options generates that list at start-up from every mod whose `mod.json` contains
   `"fh3_cars": true` (the converter writes the flag; unconverted FH2/FM5 zips are skipped because they hang the
   game). `fh3cars manifest` writes the same file into a mod for Extra Options builds that predate the generator.
3. The zip name must equal the car name used inside the zip (`ABC_Car_20.zip` for the car `ABC_Car_20`).

## Using the converter

```
fh3cars pack "<pack folder>" "<new mod folder>"      an FH2-format car pack -> FH3 mod
fh3cars fh2 "<new mod folder>" CAR1 CAR2 ...          Forza Horizon 2 cars -> FH3 mod (or --all-missing)
fh3cars tyres "<mod folder>"                         FH3's old tyre assets converted into the mod
fh3cars manifest "<mod folder>"                      media\zipmanifest.xml for the mod's zips
fh3cars car "<in.zip>" "<out.zip>" [options]         one car zip
```

Common options: `--fh3 "<FH3_ROOT>"`, `--fh2 "<FH2_ROOT>"`, `--studio "<ForzaTech Studio folder>"` (or the environment
variables `FH3_ROOT`, `FH2_ROOT`, `FORZATECH_STUDIO`), `--id`, `--name`, `--author`, and for single cars `--car-id`
(the car's id in the pack's `database.json` when it differs from the carbin's) and the driver options below.

A pack already ships `database.json` and `strings.json`, so `pack` creates the new folder, copies `mod.json`,
`database.json`, `strings.json` and `media\stripped`, converts every zip in `media\cars` (about a minute each),
converts the tyre assets and writes the manifest. FH3 already contains most FH2 cars; `fh2 ... --all-missing`
takes every FH2 car that FH3 lacks, exporting its database rows and names from FH2.

The mod folder then contains `mod.json`, `database.json`, `strings.json` and `media\` (cars, tyre overrides,
`zipmanifest.xml`). Start the game; the cars appear in the Autoshow under their manufacturer.

## Showing a pack on the Car Packs page

Cars added by a mod can be listed like DLC: a download badge in the garage lists, and a pack tile on the Car Packs
page whose page opens only the pack's cars. Add content offers to the mod's `database.json`:

- `ContentOffers`: one pack offer (`Style` `CarPack`, `Visible` 1, a unique `ProductId`, `IsInstalled`,
  `IsPurchased` and `IsFree` 1) and one child offer per car (`Style` `Car`, `ParentOfferId` = the pack's id).
- `ContentOffersMapping`: one row per car with `OfferId` = the car's child offer, `ContentId` = the `Data_Car` id and
  `ContentType` 1. Do not map the cars to the pack offer as well, or each car is listed twice.
- The pack offer must have a real `DisplayName` (and `DisplaySubtitle`, `Description`). The game looks the pack up by
  its `ProductId` and reads that name; with an empty name it falls back to listing every car. The text goes into
  `strings.json` under `Data_Car` (new ids), and the offer refers to it as `_&<(52553 << 16) | id>`; strings added
  under `ContentOffers` are not found by the game.
- `ImagePath` / `ThumbPath` take `game:\Media\Stripped\<folder>\<name>.swatchbin` files shipped in the mod's
  `media\stripped`. `fh3cars --mkswatch <rgba> <w> <h> <out>` writes one from raw RGBA; the game's own pack pictures are
  1383x494 (big panel) and 289x178 (strip tile).

## What the converter changes

Everything was found by trial in FH3; each item was one black or broken car.

- Bundle format 1.0 to 1.1 for every modelbin, materialbin, swatchbin and `ManufacturerColors.bin`. Textures keep the
  Xbox (Durango) tiling that the FH3 package expects.
- All vertex layouts of a model are converted (ForzaTech Studio converts only the main one); shader parameters are
  upgraded to FH3's v3 form with GUIDs learnt from FH3 cars (`param_guids.txt`).
- Vertex data is re-encoded for FH3's compact layout, where the studio's conversion is wrong or incomplete. Positions
  are 16-bit normalised values in a shared buffer with one scale and offset per model, and the x component of the
  vertex normal sits in the position's fourth component (slot 1 holds only y and z); without it one side of the car
  shades like a mirror. Tangents: FH3 wants the tangent and bitangent pair (and rebuilds the normal from it); FM5
  bodies carry one tangent, so the converter builds a complete frame from the source normal. Normals: FH3 keeps
  (y, z) in R16G16_SNORM.
- Mask decals (FM5 slots with an `_opac` mask: sponsor logos, numbers) use FH3's mask layout: `0xe159d67b` on,
  `0x7169ac81` and `0xff73057f` off, the `_opac` texture kept in `0x66e53f62`, a flat colour texture in `0x10350bbc` for
  slots that had none (`converted\fill_<rgb>.swatchbin`) and the slot's colour in `0xc0cb2820`. A colour sheet with a
  mask is baked with its mask into one straight-alpha texture.
- Full-colour decal sheets with no mask take FH3's plain one-parameter texture slot (the material instance of an FH3
  car's plain livery slot, `LiveryNative.cs`), which honours the texture's own alpha. The switch set used earlier
  turns the alpha off and shows the transparent texels as boxes.
- Untextured `carpaint_livery` slots become FH3's paintable `carpaint`, except FM5 solid fills (switch `0x78004a9c`
  with their own colour): those get a 4x4 opaque texture of that colour, because FH3 draws an untextured livery slot
  black.
- Paint zones: `ManufacturerColors.bin` is rewritten for FH3 and every zone that names the generic carpaint, the
  livery shader or a car-specific paint FH3 lacks (and the zip does not ship) gets the FH3 manufacturer preset nearest
  to the zone's FM5 preview colour (black for dark ones). The log prints one `colour zone` line per zone.
- Carbin: decal-panel entries flagged with paint-zone bit 2 get the bit cleared (otherwise the decal is hidden), and
  `--car-id` moves the carbin's ordinal and upgrade ids to the id the pack's database uses; otherwise FH3 finds no
  wing or bumper part rows and the wings disappear.
- Lights: FM5 bulb materials become FH3's `lights_smooth_solidlight` (taillights) or `gls_lightbulb` slots with FH3's
  material instance, and cars without `Lights.bin` / `LightPresets.bin` get FH3's, with the path rewritten to the car's
  taillight model, so the brake and tail lights switch on.
- Mirror glass takes FH3's chrome material (the live rear-view material stays black on converted models).
- Wheels: FM5 `wheel_blur` meshes draw as black cylinders and are hidden, except the large ones that are the real rim
  face; wheel faces whose converted alpha material draws nothing, and car-specific wheel paints FH3 lacks, are replaced
  with FH3 wheel paints the same model already carries. Axle helper meshes (`backfacinggeo`) get FH3's transparent flags.
- Skeleton blobs are set to version 1; FH3 ignores version 0 skeletons (the wheel does not turn, the suspension does
  not move).
- Driver animations: FH3 plays the `male`/`female` sets, which use a different rig than the old `default` set in
  FM5/FH2 cars. The converter borrows an FH3 car's sets, moves the driver so the hands sit on this car's steering wheel,
  and rewrites the steering wheel tracks inside those files (FH3 poses the player's wheel from the animation, not from
  the car). Centred-seat cars use the BAC Mono's pose with tuned defaults (90 % body scale, 3 cm lift, 20 degrees
  recline, hands 4.5 cm lower); other cars take the FH3 car with the closest seat-to-wheel geometry
  (`driver_donors.txt`). Overrides: `--driver-donor`, `--driver-scale`, `--driver-raise`, `--driver-recline`,
  `--hands-lower`, `--hands-spread`.
- FH3's own F1, drag, open-wheel and white-wall tyre assets are still in FH2 format. They are converted and shipped as
  overrides in `media\cars\_library\scene\tires`; without them formula cars show a tiny rim.

## Known limits

- Engine sound comes from an FH3 car. FH3 picks an engine bank through `media\audio\cars\CarEngineEventMapping.xml`;
  a car that is not listed plays `NUL_Car_00`, which is silence, and FH2/FM5 banks (FMOD version 46) cannot be loaded
  by FH3's FMOD (version 78). Name a donor per car in the mod's `mod.json`:

  ```json
  "fh3_engine_audio": { "ABC_Car_20": "ARI_Atom_13", "*": "FOR_FormulaFord_13" }
  ```

  Values are FH3 car MediaNames (anything that appears as `engineName` in the game's mapping or has a
  `PG_<name>.bank`); `"*"` is the default for the pack's other cars and never replaces an engine FH3 already maps.
  Extra Options writes the game's mapping plus one line per add-on car into its own mirror and logs
  `engine audio of <car> borrowed from <donor>`.
- Steering wheel axis: a car whose steering spindle bone has local Z along the column (instead of local X) has its
  wheel pitch face up / face down when you steer. An experimental fix is in the converter (`FTCAR_WHEELAXIS=rot`) and is
  not finished.
- The wheels' spoke blur at speed is lost because the `wheel_blur` meshes are hidden.
- Autoshow thumbnails are not generated.
- Decal panels that the source model mirrors onto the other side keep their mirrored text.
- The cockpit camera and steering wheel sit where the source game placed them, which can look lower than in FH3's own
  cars.
- The converter depends on ForzaTech Studio's libraries and cannot be redistributed with them.
