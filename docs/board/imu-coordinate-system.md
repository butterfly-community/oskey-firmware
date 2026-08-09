# IMU coordinate convention

The orientation pipeline has one coordinate conversion point:

```text
QMI8658 accelerometer axes
        -> src/imu/imu.c board/display mapping
        -> app_imu_sample pitch/roll
        -> LVGL cuboid projection (no sign conversion)
```

`app_imu_sample.pitch` and `app_imu_sample.roll` are active rotations of the
displayed model. They are not raw sensor-axis angles. When looking at the
display, the required physical behavior is:

- raise the left edge: the model's left edge rises;
- raise the right edge: the model's right edge rises;
- raise the top edge: the model's top edge rises;
- raise the bottom edge: the model's bottom edge rises.

LVGL screen coordinates use +X to the right and +Y downward. The QMI8658 uses
right-handed sensor coordinates, and its axes also depend on how the package is
mounted on the board. `src/imu/imu.c` accounts for both differences. Do not fix
a direction mismatch by adding a negative sign in `src/display/ui_pages.c`;
change the mapping at the sensor boundary so every attitude consumer receives
the same convention.

The `gyro_x`, `gyro_y`, and `gyro_z` fields remain raw QMI8658 sensor-axis
angular rates converted from radians per second to degrees per second. They are
shown as diagnostics and are not currently integrated into the model attitude.

## Hardware direction check

Start with the display face up on a level surface, then test one edge at a time.
The raised physical edge and the raised model edge must match for all four
directions listed above. Also verify that returning the board to level returns
both displayed angles close to zero. Small zero offsets are normal without a
calibration step.
