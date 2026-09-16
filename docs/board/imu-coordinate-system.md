<!-- SPDX-License-Identifier: MPL-2.0 -->

# IMU orientation and display convention

The Tilt page uses the QMI8658 accelerometer and gyroscope to drive an LVGL
cuboid. The IMU module estimates a complete 3D orientation, so rotation about
all three axes remains visible. Without a magnetometer or an external heading
reference, yaw is relative and may drift over time.

## Data flow

```text
QMI8658 accelerometer + gyroscope configured at 112 Hz
	-> hardware FIFO
	-> periodic timer gives the IMU wake semaphore
	-> dedicated high-priority IMU thread drains the FIFO over I2C
	-> sensor-to-body coordinate mapping and unit conversion
	-> FusionBias run-time gyroscope offset correction
	-> FusionAhrs six-axis update
        -> renderer coordinate mapping
        -> Fusion quaternion-to-matrix conversion
        -> valid + row-major 3x3 rotation matrix over zbus
        -> LVGL renders the newest valid sample every 100 ms
```

Physical sampling, FIFO retrieval, and rendering are independent. The QMI8658
samples at the output data rate configured in
`boards/esp32s3_lichuang.overlay`. The timer period only controls how quickly
the host drains the FIFO. Fusion processes every FIFO sample using the sensor
ODR period; LVGL independently renders the newest result at
`CONFIG_OSKEY_IMU_RENDER_INTERVAL_MS`.

## Timestamp contract

The current FIFO backend does not present the QMI8658A `TIMESTAMP[H:M:L]`
register as a nanosecond timestamp. The datasheet defines that register as a
24-bit sample output count, not as a fixed-frequency clock. More importantly,
hardware validation on the current board found that it remains zero in the
Non-SyncSample mode required by FIFO. Enabling SyncSample is not a workaround:
the datasheet explicitly states that FIFO is unsupported in that mode.

The backend therefore assigns a Zephyr monotonic timestamp immediately after
reading the FIFO count. That timestamp is an upper-bound anchor for the newest
frame already present in the FIFO; earlier frames in the same batch are spaced
backwards using the configured sensor ODR. Diagnostic builds measure host
arrival after the I2C FIFO transfer, so a sample timestamp is never later than
the measured arrival. Without DRDY, the exact sub-period sampling phase remains
unobservable and must not be described as a hardware capture timestamp.

References: [QMI8658A datasheet](https://www.qstcorp.com/upload/pdf/202301/13-52-25%20QMI8658A%20Datasheet%20Rev%20A.pdf)
(sections 5.5, 6.1, and 8.8) and Zephyr's
[64-bit uptime tick API](https://docs.zephyrproject.org/latest/kernel/services/timing/clocks.html#uptime).

## Acquisition wakeup boundary

The acquisition mechanism has one deliberate replacement boundary. No
`.submit`, RTIO context, or driver-owned work queue participates in this path:

```text
Current board without DRDY:  k_timer expiry -> k_sem_give(IMU wakeup)
Future board with DRDY:       GPIO ISR       -> k_sem_give(IMU wakeup)
                                              |
                                              v
                         same IMU thread -> read FIFO -> Fusion
```

The timer callback and a future GPIO ISR only signal the semaphore. They must
never perform I2C/SPI transfers, decode samples, publish zbus messages, or run
Fusion. The high-priority IMU thread exclusively owns those operations. A
future DRDY-capable board therefore replaces the wakeup producer, not the FIFO,
timestamp validation, synchronous source API, Fusion, or application data path.

This software-timer fallback does not claim interrupt-level acquisition
determinism. The sensor FIFO keeps physical sample spacing independent of host
scheduling jitter, while retrieval latency remains subject to I2C bus and
thread scheduling latency. DRDY makes the wakeup phase observable and removes
timer polling; the same thread may continue using the synchronous FIFO read.

The QMI backend follows the complete CTRL9 handshake for FIFO commands: wait
for command completion, acknowledge it, then wait for the completion bit to
clear. Its 100 ms fault timeout matches the bound used by the vendor-derived
[SensorLib QMI8658 implementation](https://github.com/lewisxhe/SensorLib/blob/master/src/SensorQMI8658.hpp);
the normal path does not sleep when the completion bit is already set.

After enabling both sensors, the backend waits for the QMI8658A gyroscope
turn-on interval (`150 ms + 3/ODR`, datasheet section 7.3), then resets the FIFO.
This discards unsettled startup frames and prevents an early FIFO Read Mode
request from blocking while the gyroscope starts. The delay belongs to the
sensor backend; the portable acquisition thread does not encode it.

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
page starts rendering, while bias estimation continues whenever IMU acquisition
is active (the UI or entropy collector may keep it active). There is no
application-defined calibration-complete state or
calibration button.

## Page lifecycle

The UI and entropy collector publish START and STOP commands over zbus. The
listener atomically updates one desired-state bit per client and wakes the IMU
thread. No command queue or duplicate runtime client state is needed: START and
STOP are idempotent level changes. The thread exclusively starts and stops the
source, timer, sampling, AHRS, and state transitions. One consumer therefore
cannot stop acquisition while the other still needs it.

The AHRS uses the library default gain, NWU convention, a 10-degree acceleration
rejection threshold, a five-second rejection timeout, and the 512-degree-per-
second gyroscope range configured in devicetree. Linear movement can still
cause a brief tilt disturbance because an accelerometer measures total specific
force rather than gravity alone; acceleration rejection limits this effect.

## Units and coordinate mapping

The portable source contract normalizes acceleration to micro-g and angular
rate to 10-microdegrees per second. The application converts these integer
values to the units required by Fusion: g and degrees per second.

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
not compile the QMI8658A FIFO backend or link Fusion. The IMU sampling thread
uses a 4 KiB stack. The application no longer depends on zscilib.

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

Two 10-minute-22-second automatic soak tests were run on the current ESP32-S3
board. Each test covered seven UI-to-entropy client handoffs and seven complete
source stop/restart cycles. The first test exposed a repeatable startup defect:
requesting FIFO Read Mode before the datasheet gyroscope turn-on interval had
elapsed blocked the host for 74.8--86.6 ms and left only about 542 samples in
the first five-second window.

After moving the datasheet turn-on wait and startup FIFO reset into the QMI
backend, the second test passed all handoffs and restarts. A complete restart
now deliberately takes about 179 ms. Every first and steady five-second window
contained 552 samples at 112.1 Hz, with `errors=0` and `gaps=0`. Maximum
assigned-timestamp-to-I2C-arrival age was about 1.1--1.3 ms for single-frame
batches and 11.0--11.6 ms when a startup batch contained two frames. These age
figures describe host retrieval only; they do not make the unobservable physical
sampling phase into a hardware capture timestamp.
