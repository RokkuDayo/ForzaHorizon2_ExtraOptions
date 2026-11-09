# Photo camera restrictions in 0.2.14

PhotoMode.FreeCamera is a startup-only boolean, default 1. It controls three
fixed patches under GROUP_PHOTO_CAMERA and adds no callback, thread or library.

The world photo camera update at RVA 0x397850 first calls its movement update
at 0x103cda0, clamps FOV, and checks the retail debug setting
disablephotomodecollisiondetection (debug object +0x31fe). Its conditional skip
at 0x3978e8 becomes an unconditional jump to the same epilogue at 0x397982.
This omits the collision solve at 0xa92020 and the subsequent position bounds
pass at 0x397d20. Motion integration and FOV handling remain intact. The skipped
bounds pass uses the photo cylinder radius/floor/ceiling settings and the
camera's terrain-derived maximum-height state. The debug setting is not written.

The CUI photo camera restriction method at 0xaa0020 gets the current transform,
optionally solves collision, clamps height and writes the corrected position.
The conditional skip at 0xaa0081 becomes an unconditional jump to 0xaa00fb.
The first height-clamp instruction at 0xaa00fb becomes a jump to 0xaa0153,
where the unchanged transform setter consumes the unmodified current position.
Skipping only collision would still enforce that height clamp. Movement remains
in the preceding 0xaa1b70 call in the CUI update at 0xaa26d0.

All three original and target byte sequences are included in patches.json.
Runtime hashing normalizes those known ranges; loaded-byte validation and
transaction handling are identical to the other fixed patches. Setting 0
restores the three original sequences, and Global.Enabled=0 skips all writes.

tests/test_photo_camera.py executes the world and CUI native methods in a private
relocated image. Movement, collision/terrain queries and transform services are
mocked. It checks large positive/negative coordinates, skipped restriction calls,
unchanged native FOV clamp, actual CUI height-clamp behavior, the unmodified debug
setting and repeated on/off restoration. Full rendering and camera controls still
require an in-game check. No proprietary game bytes are distributed.
# Showroom safety with FreeCamera disabled (issue 122)

The native UI Photo Mode update at `aa26d0` calls the world restriction pass
`aa0020`. With the global FreeCamera bypass disabled, this pass accesses the
collision scene at camera offset `a0`, which preview Photo Mode does not provide.

The call sequence at `aa2708` now retains native movement through `aa1b70` and
omits `aa0020` only during the active showroom Photo Mode camera update. The
subsequent native orbit/FOV update is retained. Other UI updates and gameplay
Photo Mode retain the configured FreeCamera behavior. No global debug flag or
per-frame executable patching is used.

`test_showroom_video.py` runs with FreeCamera disabled and verifies that the
original collision branch remains present, ordinary updates call restrictions,
and the showroom update skips them. `test_photo_camera.py` retains its 59 checks
for movement, FOV, bounds, collision and option restoration. These fixtures run
private native code with scene services mocked; full rendering needs game testing.

