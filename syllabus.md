# Robot Learning Course — Self-Localizing Differential-Drive Robot

**Hardware:** ESP32-S3, GY-85 IMU (ADXL345 accelerometer + ITG3205 gyroscope + HMC5883L magnetometer, all on one I2C breakout), TB6612FNG dual motor driver, 2 DC gear motors with wheel encoders, 2-wheel differential drive chassis.

**Toolchain:** Native ESP-IDF (VS Code Espressif extension), already installed. FreeRTOS/component-based style (`idf.py`, `menuconfig`) — not Arduino framework.

**Teaching style:** Socratic / guided lookup. Don't hand over finished code or wiring diagrams outright — point to datasheets, library docs, and what to check, let the user do the lookup/wiring/typing/debugging, then review what they found and correct misunderstandings.

**Stage 1 end goal:** the robot maintains a continuously updated estimate of its own pose (x, y, heading θ) on the floor plane, fused from wheel encoder odometry and the GY-85 IMU, printed/logged in real time. "It knows where it is" = it can report this estimate.

## Modules — REORDERED 2026-08-09, see below

**As of 2026-08-09 the module order below was superseded by a drive-first tutorial track** (user
request: get the robot driving as a 2-wheel differential robot first, defer IMU/localization).
The content is unchanged, just resequenced. See `~/.claude/plans/wild-puzzling-map.md` for the
full plan and `guide.html` for the live tutorial index.

**Updated 2026-08-13 — now 8 tutorials, motor work resolved out of order:**
T1 ESP-IDF bring-up → T2 wiring/pin/power audit → T3 TB6612+LEDC per-wheel (T2–T3 done informally
via code review of hand-written driver code, ahead of standalone pages — GPIO18 fake-ground fault
found and fixed, PWM channel split, deadband/scale bugs fixed) → **T4/T7 open-loop drive(v,ω) —
robot runs** (page written as `t07-inverse-kinematics.html`, **inverse kinematics only** —
`(v, ω) → wheel speeds`; forward kinematics/odometry, i.e. `ticks → x,y,θ`, is explicitly deferred
past this track) → T5 encoders (PCNT, `t05-encoders-pcnt.html`) → T6 ticks→metres+speed
calibration (`t06-speed-and-distance.html`) → **T8 closed-loop PI + square capstone**
(`t08-closed-loop-pi.html`, new — added on request after the original 7-tutorial plan).
Sequence is encoders → speed/distance → inverse kinematics → closed-loop, i.e. T5 → T6 → T7 → T8
in file-number order even though T4/T7 is conceptually where the robot first drives; see
`guide.html`'s numbering note. GY-85/IMU/fusion work (originally modules 2, 3, 7, 8) and the
stretch module (9) all move to *after* this track, unchanged in content.

**Original order, kept for reference:**

0. **Toolchain & board bring-up** — ESP-IDF project for ESP32-S3, verify `idf.py build/flash/monitor` round-trip.
1. **Hardware orientation & wiring** — pinout for ESP32-S3, GY-85 (I2C: SDA/SCL/VCC/GND), TB6612 (AIN1/AIN2/PWMA, BIN1/BIN2/PWMB, STBY, VM/VCC), encoder signal lines. Power budget sanity check (motor supply vs logic supply).
2. **I2C bus & GY-85 raw communication** — I2C scan, addresses for ADXL345/ITG3205/HMC5883L, reading raw registers, understanding each chip is a separate I2C device on the same bus.
3. **Sensor calibration & units** — accelerometer counts → g, gyro counts → °/s (and bias/zero-rate offset), magnetometer raw → heading (+ hard/soft iron calibration basics).
4. **Motor driver control (TB6612)** — direction + PWM speed control, STBY handling, verifying each wheel spins the correct way at a commanded speed.
5. **Wheel encoders** — interrupt-driven tick counting, converting ticks → wheel distance/speed, quadrature vs single-channel considerations.
6. **Differential drive kinematics** — wheel speeds → robot linear/angular velocity → pose integration (encoder-only odometry). This alone gives a first (drifty, wheel-slip-prone) position estimate.
7. **IMU-based heading** — gyro integration for heading, complementary/Kalman fusion of gyro + magnetometer heading, discussion of gyro drift vs magnetometer noise/distortion.
8. **Sensor fusion for pose** — combine encoder-derived (x, y) with IMU-corrected heading into one pose estimate; discuss where each sensor's error shows up and why fusion beats either alone.
9. **Stretch:** logging/visualizing the pose estimate over time (e.g. plotting a driven path), basic EKF framing.

## Progress
See `progress.md`.
