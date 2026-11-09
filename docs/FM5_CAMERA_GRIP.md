# FM5 chase camera grip

The chase drift heading and spring blend use the native lateral-slip getter at physics interface slot 0x1A0 (7FDA00), reading each wheel's +0x1360 value with stride 0x4C0. This matches the FH2 interface's lateral-slip operation at slot 0x1D8 (12C5CB0), immediately after the body-vector getter and before the normalized force getter. Contact is checked through 7FC690 / slot 0x480. The greater grounded axle average drives the same FH2 smoothstep: no drift at or below 1, full blend at 1.35, with established recovery smoothing.

The previous slot 0x478 (7FC670) multiplies wheel radius (+0x14B0) by wheel angular velocity (+0x10EC). The angular-speed getter's RPM conversion factor is 9.549296, confirming that this is rolling speed, not lateral slip. It cannot gate chase behavior by grip.

Cockpit behavior deliberately retains that existing rolling-speed gate and its original thresholds, momentum calculation, body-turn correction and springs. Its state is now a single driver record; unused hood/bumper records and obsolete diagnostic messages were removed. Hood and bumper remain excluded.

Validation: test_fm5_camera_grip.py executes the real target, velocity, wheel contact, lateral-slip getter and camera spring code in a private mapped image. It covers gripping tyres with high rolling speed, signed slip, threshold blending, airborne rejection and wrapped targets. Its 180-frame cockpit trace matches the previous DLL byte for byte, including zero-dt post updates. No running game was modified or visually tested. The older comprehensive camera test's startup/XAML expectations predate later always-on features; the current binding integration test is used for startup/menu regression instead.
