# IMU orientation and display convention

The Tilt page uses the QMI8658 accelerometer and gyroscope to drive an LVGL
cuboid. The IMU module estimates a complete 3D orientation, so rotation about
all three axes remains visible. Without a magnetometer or an external heading
reference, yaw is relative and may drift over time.

## Data flow

```text
QMI8658 accelerometer + gyroscope configured at 112 Hz
        -> sensor-to-body coordinate mapping and unit conversion
        -> FusionBias run-time gyroscope offset correction
        -> FusionAhrs six-axis update
        -> renderer coordinate mapping
        -> Fusion quaternion-to-matrix conversion
        -> valid + row-major 3x3 rotation matrix over zbus
        -> LVGL renders the newest valid sample every 100 ms
```

Sampling, fusion, and rendering are independent. The QMI8658 and `FusionBias`
use the nominal output data rate configured in
`boards/esp32s3_lichuang.overlay`; rendering uses
`CONFIG_OSKEY_IMU_RENDER_INTERVAL_MS`. `FusionAhrs` initially uses the nominal
period, then receives the measured interval between each pair of successful
samples through `FusionAhrsSetSamplePeriod()`. Its gyroscope integration
therefore remains correctly scaled when processing and scheduling make the
actual sampling rate lower than the nominal rate.

## Fusion initialization and gyroscope offset

The application uses xioTechnologies/Fusion v1.3.2 and does not implement a
second calibration algorithm. Entering the page restarts `FusionAhrs`. Its
three-second startup gain ramp quickly aligns the quaternion with gravity, and
the cuboid remains hidden while the library reports its `startup` flag.

`FusionBias` owns gyroscope offset estimation. It detects gyroscope readings
below its default stationary threshold for its default three-second stationary
period, then continuously adjusts the offset with a deliberately slow filter.
On the first Tilt-page session after boot, allow about 30 seconds at rest for
the offset to settle. The cuboid becomes visible when the separate three-second
AHRS startup finishes, so some relative-yaw rotation during this initial bias
convergence is expected and accepted. The offset is retained in RAM when
leaving and re-entering the page, but is not stored across a reboot.

These are separate library mechanisms: the AHRS startup flag controls when the
page starts rendering, while bias estimation continues whenever the IMU page
is sampling. There is no application-defined calibration-complete state or
calibration button.

## Page lifecycle

The UI publishes START and STOP commands over zbus. The zbus listener only
copies those commands into an ordered queue; the IMU sampling thread consumes
the queue and exclusively owns the streaming state, sampling schedule, AHRS
restart, and state transitions. The thread has one event loop: while idle it
waits indefinitely for a command; while streaming it waits on the same queue
for one nominal sample period. A command wakes it immediately; a timeout causes
one sample to be processed. A rapid STOP followed by START is therefore
observed as two distinct transitions and always restarts the AHRS for the new
page session. There is no separate timer, deadline compensation, semaphore,
polling set, or nested sampling loop.

The AHRS uses the library default gain, NWU convention, a 10-degree acceleration
rejection threshold, a five-second rejection timeout, and the 512-degree-per-
second gyroscope range configured in devicetree. Linear movement can still
cause a brief tilt disturbance because an accelerometer measures total specific
force rather than gravity alone; acceleration rejection limits this effect.

## Units and coordinate mapping

Zephyr exposes acceleration in metres per second squared and angular rate in
radians per second. The standard Zephyr sensor conversion helpers convert them
to the units required by Fusion: g and degrees per second.

The QMI8658 package axes do not match the display body axes. Accelerometer and
gyroscope use the same mapping:

```text
fusion = (sensor_y, -sensor_x, sensor_z)
```

Fusion publishes a sensor-relative-to-Earth quaternion. `imu.c` applies the
renderer coordinate mapping internally:

```text
render = (fusion_w, fusion_x, -fusion_y, -fusion_z)
```

`imu.c` immediately converts this quaternion with
`FusionQuaternionToMatrix()` and publishes the resulting row-major 3x3 rotation
matrix. The zbus contract and LVGL code therefore do not depend on Fusion types
or quaternion conventions. LVGL applies the matrix directly to the cuboid
vertices and face normals; the chain never converts through Euler angles.
Crossing 90 or 180 degrees therefore does not introduce an Euler singularity.
When looking at the display, raising a physical edge must raise the same cuboid
edge.

## Build boundary

Fusion is pinned as the `lib/Fusion` git submodule. `FusionAhrs.c` and
`FusionBias.c` are compiled into a separate static library and linked to the
application only when `CONFIG_OSKEY_IMU=y`. Builds without the IMU feature do
not compile or link Fusion. The IMU sampling thread uses a 4 KiB stack. The
application no longer depends on zscilib.

## Hardware validation

1. After boot, open Tilt and keep the board still. Rendering starts after the
   three-second AHRS initialization; allow about 30 seconds for the first
   FusionBias convergence before judging relative-yaw stability.
2. Raise each physical edge separately. The same cuboid edge must rise, without
   a doubled angle.
3. Translate the stationary-orientation board horizontally and vertically. A
   short disturbance is possible, but it should settle without a lasting tilt.
4. Rotate about the display normal. The cuboid must show that relative yaw; slow
   long-term drift remains possible without an absolute heading reference.
5. Rotate through vertical and inversion. Motion must remain continuous without
   snapping, angle doubling, or switching formulas.
