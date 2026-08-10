# IMU orientation and display convention

The Tilt page uses the QMI8658 six-axis IMU to drive one LVGL cuboid. The fusion
path and renderer use a complete 3D orientation quaternion, so physical rotation
about any of the three axes remains visible. Without a magnetometer or another
external direction reference, yaw is relative to the orientation at
initialization and may drift slowly over time.

## Data flow

```text
QMI8658 accelerometer + gyroscope at 56 Hz
        -> sensor-to-fusion coordinate mapping
        -> subtract the user-triggered stationary gyroscope bias
        -> Madgwick IMU update using zscilib vector/quaternion primitives
        -> fusion-to-render coordinate mapping
        -> valid + quaternion over zbus
        -> LVGL renders the newest valid sample every 100 ms
```

Sampling, fusion, and rendering are independent. The filter uses the 56 Hz
sensor output data rate configured in `boards/esp32s3_lichuang.overlay`; the
page defaults to a 100 ms render interval.

## Gyroscope calibration

The Tilt page requires an explicit calibration before it displays the cuboid.
Pressing **Calibrate** discards any previous bias and collects 168 consecutive
samples, approximately three seconds at the configured sensor rate. The device
must remain still: acceleration must stay within 10 percent of standard gravity
and angular speed must remain below 5 degrees per second. A sample outside
either limit resets the collection window.

The page publishes `APP_IMU_COMMAND_CALIBRATE` on the IMU command channel. The
IMU module owns the `UNCALIBRATED -> CALIBRATING -> READY` transition and
publishes it on the state channel; the page only consumes that state. Neither
module calls into the other.

The mean of the three mapped gyroscope axes becomes the software bias and is
subtracted before every fusion update. The result remains in RAM for the rest
of the boot and may be replaced by pressing **Calibrate** again. It is not
written to persistent storage and does not provide an absolute yaw reference.
Leaving the page while calibration is running cancels the partial sample set.

## Fusion initialization and update

Before initialization, `imu.c` waits for an acceleration magnitude within 20
percent of standard gravity. The first valid acceleration sample directly
constructs the shortest body-to-earth rotation that aligns measured gravity
with world +Z, with yaw initialized to zero. The page can therefore start while
the board is already vertical or tilted without waiting for convergence from
the identity quaternion.

Subsequent samples run the IMU form of Madgwick with `beta = 0.1`, using zscilib
vector and quaternion primitives. The pinned zscilib feed function is not used
because its gravity objective, Jacobian, and angular-velocity multiplication do
not share one quaternion convention. `imu.c` contains one minimal IMU-only
update in the body-to-earth convention; there is no alternative fusion branch.

The update has no magnetometer path, continuous bias estimator, acceleration
rejection window, or per-sample Euler conversion.

## Coordinate mapping

The QMI8658 package axes do not match the display axes. Accelerometer and
gyroscope data use the same mapping:

```text
fusion = (sensor_y, -sensor_x, sensor_z)
```

The internal quaternion is body-to-earth and includes gyro-integrated yaw. The
complete quaternion is published after the following renderer-axis mapping:

```text
render = (fusion_w, fusion_x, -fusion_y, -fusion_z)
```

LVGL converts that quaternion with `zsl_quat_to_rot_mtx()` and does not pass
through Euler pitch or roll. When looking at the display, raising a physical
edge must raise the same cuboid edge. Horizontal or vertical translation may
briefly disturb the indicated tilt because an accelerometer measures total
specific force, not gravity in isolation. Rotation about gravity comes from the
gyroscope; the three-second bias calibration prevents the large startup drift,
but cannot create an absolute heading reference.

## Inversion and invalid output

The renderer does not reconstruct orientation from gravity, switch formulas, or
use an inverted dead zone. It consumes the continuous fusion quaternion, so
crossing 90 or 180 degrees does not introduce an Euler-angle singularity. If
fusion initializes with the acceleration vector nearly opposite world +Z, it
deterministically chooses a 180-degree rotation about X; subsequent motion
continues through normal quaternion integration.

After every update, `imu.c` verifies that all published quaternion components
are finite. Otherwise the sample is marked invalid, LVGL skips it, and fusion is
reset so a later valid sample can initialize again.

## Hardware validation

1. Open Tilt, keep the board still, and press **Calibrate**. The cuboid remains
   hidden until the three-second calibration reaches `READY`.
2. Raise each physical edge separately. The same cuboid edge must rise, with no
   doubled angle.
3. Leave the board still for at least 30 seconds. The cuboid must not rotate in
   its plane rapidly; small jitter and slow long-term yaw drift are possible
   without an absolute direction reference.
4. Rotate the board about the display normal. The cuboid must preserve and show
   that rotation instead of removing it.
5. Rotate through vertical and inversion. Motion must continue in the same
   direction without angle doubling, snapping, or switching algorithms.
