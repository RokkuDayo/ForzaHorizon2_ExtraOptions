# Camera-relative minimap — 0.2.36

> Current controls (0.2.62): graphics options are in Settings → Video;
> drift/FOV controls, minimap orientation, Autodrive cinematic camera and championship dialogue are
> in Settings → HUD. The moved INI values are accepted only for initial migration
> into the sidecar; the drift master INI switch is retired. The notes below retain
> historical investigation details.

The supported FH2 EXE has `MapViewControllerTrackingMiniMap` at vtable
RVA 0x2d9dc28. Its update at 0xbaaec0 fetches the `default` entity map's
`vehicle` type-2 pose. Position is the first vector; the stock direction is
the second vector minus the first. `0xbadff0` reads its rotation-enabled
property. The controller calls `0xbabb90` with controller, aligned position,
aligned direction, speed and the fifth-argument minimum zoom. This routine
calculates the map matrices and player-relative offset; native icon rendering
uses the individual entities' own poses, independently of the map orientation.

Only that call at 0xbaaff6 is redirected. It calls a six-byte indirect jump
at 0xbabb88 through an aligned pointer at 0xbaae68. Both padding areas are
0xcc bytes outside the enclosing native functions, and no RIP references to
either were found. All three sites are preflighted and use the existing patch
transaction. With the option off, both original code ranges and the unused
pointer slot are restored. The DLL is pinned whenever this callback is used.
No entry-point prologue, vtable, asset or game file is changed by this option.

The primary gameplay view is resolved using the existing path:
player-manager global 0x34aba60 -> +0x48 simulation -> +0x98 view manager ->
+0x2d0 view array -> first view. Supported SplitView/MultiCamView vtables are
0x2c66188 and 0x2e6e890. View +0xc0 is ForzaMultiCam, vtable 0x2c6c840.

The final selected camera's cached view matrix is at ForzaMultiCam +0x260.
Native 0xfe61c0 obtains the active core camera's virtual +0x50 matrix and
copies it there. 0xfe2f00 returns this cache to native callers. The follow
camera's +0x50 getter is 0x1034430; the driver getter is 0x1034360. Both build
matrices with native 0x9a8280. That routine normalizes the look direction,
builds right/up/forward rows, then transposes the result into a view matrix.
Its forward direction is therefore the third column (float indices 2, 6, 10),
not the third row. The private test executes 0x9a8280 for yaw/pitch cases to
verify this convention rather than assuming a graphics API handedness.

The wrapper supplies its horizontal components in an aligned four-float
vector. Native map matrix construction already normalizes the horizontal
direction. No angles, trigonometry, input polling or smoothing are added to
the live callback. Position, speed, zoom, property-driven north-up behavior
and the original entity direction are preserved. Missing/unsupported camera
objects, nonfinite values, invalid matrix lengths and a vertical view fall
back to the original direction. Camera/renderer state is never written.

`HUD.MinimapFollowsCamera` defaults to 1. Both adapters use the same code.
This follows whichever supported gameplay camera is current, including orbit
and look-back; it is not restricted to detecting a right-stick button.

`development/tests/test_minimap.py` maps the owner's original EXE into a private relocated
allocation. Native look-at math executes; the map update and rotation property
service boundaries are mocked. Tests run the installed indirect thunk with
the real Windows ABI, including the fifth stack argument, and verify position,
speed, zoom, aligned heading, unmodified original direction and fallbacks.
It does not render the actual minimap or launch the full game.
