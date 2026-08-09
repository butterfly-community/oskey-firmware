# IMU orientation and display convention

The Tilt page uses the QMI8658 accelerometer and gyroscope together to maintain
a continuous quaternion. The gyroscope preserves the rotation path through 90
and 180 degrees; the accelerometer continuously corrects roll and pitch against
gravity.

This is a gravity-referenced 6-axis orientation, not a compass. Without a
magnetometer, rotation about gravity has no absolute reference and can drift.
`Tilt` and `Direction` remain gravity quantities, while the cuboid also retains
the short-term rotation history supplied by the gyroscope.

## Data flow

```text
QMI8658 accelerometer + gyroscope at 56 Hz
        -> one sensor-to-fusion coordinate mapping
        -> accelerometer magnitude validation
        -> zscilib AQUA 6-axis quaternion update
        -> fusion-to-render quaternion mapping
        -> app_imu_sample over zbus
        -> LVGL reads the newest sample every 100 ms
        -> rotate vertices and face normals, then draw the cuboid
```

Sampling and rendering are deliberately independent. The QMI8658 output data
rate in `boards/esp32s3_lichuang.overlay` is 56 Hz. The IMU thread runs the
filter at that rate, while `CONFIG_OSKEY_IMU_RENDER_INTERVAL_MS` defaults to
100 ms. Reducing the LVGL interval is therefore not required for correct
integration.

## zscilib integration

zscilib is pinned as the `lib/zscilib` git submodule and registered through
`ZEPHYR_EXTRA_MODULES`. `CONFIG_OSKEY_IMU` selects `CONFIG_ZSL` and
single-precision arithmetic.

The application uses zscilib AQUA without a magnetometer. AQUA integrates the
gyroscope first and applies a small accelerometer correction afterward. Its
accelerometer gain is 0.02.

The pinned zscilib Madgwick implementation was evaluated before integration,
but the project trajectory tests found incorrect static convergence (including
45- and 180-degree cases). AQUA passed the complete 0-to-360-degree trajectories
and is therefore the selected upstream implementation. This choice is covered
by tests rather than hidden behind display sign adjustments.

## Coordinate mapping

The QMI8658 package axes do not match the display axes. Accelerometer and
gyroscope samples are both mapped exactly once in `src/imu/imu_orientation.c`:

```text
fusion = (sensor_y, -sensor_x, sensor_z)
```

Using the same proper rotation for both sensors is mandatory. Swapping only the
accelerometer, negating only the gyroscope, or correcting signs in the LVGL
renderer breaks fusion and commonly produces doubled, reversed, or apparently
rotating tilt.

zscilib AQUA and the renderer use opposite quaternion directions and different
Y/Z display conventions. The adapter converts the normalized AQUA quaternion
once:

```text
render = (aqua_w, -aqua_x, aqua_y, aqua_z)
```

The renderer consumes this quaternion directly. It does not convert through
Euler pitch/roll angles and does not add any board-dependent sign changes.

When looking at the display, the required physical behavior is:

- raising the left edge raises the model's left edge;
- raising the right edge raises the model's right edge;
- raising the top edge raises the model's top edge;
- raising the bottom edge raises the model's bottom edge.

## Initialization and correction

On entering the page, the first near-1 g accelerometer sample initializes the
quaternion. This avoids waiting for a filter started at identity to converge.
If the board starts exactly face down, gravity cannot identify the rotation axis,
so the adapter deterministically chooses the display X axis. Subsequent motion
is continuous because the gyroscope supplies the missing path history.

The first 32 sufficiently stationary samples are averaged as gyroscope bias.
Moving samples are not included, and fusion can already run before the average
is complete.

Acceleration magnitude from 0.60 g through 1.40 g is accepted for correction.
Outside that range, the sample is marked invalid and AQUA continues with the
gyroscope only. This prevents strong translation or shaking from immediately
being interpreted as gravity.

AQUA's algebraic acceleration correction is undefined if the measured and
estimated gravity vectors are exactly opposite. The adapter detects that one
anti-parallel case and skips only the accelerometer correction for that sample.
The quaternion and gyroscope integration continue, preventing NaN values from
reaching LVGL.

## Displayed values

`Tilt` is derived from the fused quaternion. It is zero face up, 90 degrees on
edge, and 180 degrees face down.

`Direction` is the downhill gravity direction in display coordinates:

```text
0 degrees    display top
+90 degrees  display right
+/-180       display bottom
-90 degrees  display left
```

Direction is unavailable when the horizontal gravity component is too small,
which naturally occurs near face up and face down. The cuboid remains visible
after orientation initialization; there is no 180-degree rendering dead zone.

Gyroscope values are displayed in raw QMI8658 sensor axes, converted from
radians per second to degrees per second. They are diagnostic values only; the
fusion adapter performs its own mapped, bias-corrected calculation.

## Automated validation

The native Zephyr test is in `tests/imu_orientation` and uses the same adapter
and zscilib module as the firmware:

```sh
west build -p always -b native_sim/native/64 tests/imu_orientation \
  --build-dir build/imu-orientation-test
west build -d build/imu-orientation-test -t run
```

It covers:

- display X and Y rotations through 90, 180, 270, and 360 degrees;
- quaternion direction and angle accuracy without reversal after 180 degrees;
- gyroscope sign for rotation around gravity;
- static convergence to a 45-degree gravity tilt;
- startup while exactly face down;
- gyro-only continuation while acceleration magnitude is invalid;
- rejection of the anti-parallel acceleration singularity without NaN output.

## Hardware validation

1. Open Tilt with the display face up and leave it still briefly for gyro bias
   collection. Tilt should settle near zero.
2. Raise each physical edge separately. The same cuboid edge must rise.
3. Hold near 45 and 90 degrees. Tilt must settle near those values; the
   90-degree cuboid is a thin edge view.
4. Continue slowly through 135, 180, 225, 270, and back to 360 degrees. The
   cuboid must continue in the same direction instead of reversing at 90 or
   shaking at 180 degrees.
5. Repeat in the opposite direction and around the other display axis.
6. Shake or translate the board, then hold it still. Temporary error is
   acceptable, but the gravity-referenced tilt must recover without persistent
   roll or pitch drift.
