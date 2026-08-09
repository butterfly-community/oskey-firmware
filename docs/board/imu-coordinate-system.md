# IMU orientation and display convention

The Tilt page uses the QMI8658 accelerometer and gyroscope to drive one LVGL
cuboid. It intentionally exposes only a validity flag and a quaternion; numeric
tilt, direction, and raw gyroscope diagnostics are not part of the data path.

This is a 6-axis orientation without a magnetometer. Gravity corrects roll and
pitch, while rotation about gravity has no absolute reference and may drift.

## Data flow

```text
QMI8658 accelerometer + gyroscope at 56 Hz
        -> sensor-to-fusion coordinate mapping
        -> zscilib AQUA quaternion update
        -> fusion-to-render quaternion mapping
        -> valid + quaternion over zbus
        -> LVGL renders the newest valid sample every 100 ms
```

Sampling and rendering are independent. The filter uses the 56 Hz sensor output
data rate configured in `boards/esp32s3_lichuang.overlay`; the page defaults to
a 100 ms render interval.

## zscilib integration

zscilib is pinned as the `lib/zscilib` git submodule and registered through
`ZEPHYR_EXTRA_MODULES`. The application uses the AQUA filter with an
accelerometer gain of 0.02 and no magnetometer.

The IMU module deliberately contains no local gyro-bias estimator, motion
classifier, acceleration rejection window, Euler-angle calculation, or
inversion-specific prediction. AQUA receives each mapped accelerometer and
gyroscope sample directly.

Before the first AQUA update, `imu.c` waits for an acceleration magnitude
within 20 percent of standard gravity. This prevents AQUA's one-time gain
initialization from being disabled by an obviously invalid first sample.

Fusion is implemented directly in `src/imu/imu.c`; there is no separate
orientation adapter or unit-test interface.

## Coordinate mapping

The QMI8658 package axes do not match the display axes. Accelerometer and
gyroscope data use the same mapping:

```text
fusion = (sensor_y, -sensor_x, sensor_z)
```

The AQUA quaternion is converted once for the renderer:

```text
render = (aqua_w, -aqua_x, aqua_y, aqua_z)
```

The renderer consumes this quaternion directly and does not convert through
Euler pitch or roll. When looking at the display, raising a physical edge must
raise the same cuboid edge.

## Invalid output

After every AQUA update, `imu.c` verifies that all quaternion components are
finite. Otherwise the sample is marked invalid and LVGL does not draw the
cuboid for that frame. The quaternion is reset to identity so a later valid
sample can resume the display.

There is no separate 180-degree or anti-parallel special case.

## Hardware validation

1. Open Tilt with the display face up; the cuboid should appear and follow the
   same physical edge movements.
2. Rotate continuously through 90, 180, 270, and 360 degrees; rotation should
   continue in the same direction.
3. If an invalid orientation temporarily hides the cuboid, move the board back
   to a valid orientation and confirm that rendering resumes.
