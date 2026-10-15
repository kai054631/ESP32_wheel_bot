# Progress Log

## 2026-08-08 — Course started
- Set up course scope, hardware (ESP32-S3 + GY-85 + TB6612 + 2 motors w/ encoders, diff drive), toolchain (PlatformIO), teaching style (Socratic/guided lookup).
- Stage 1 goal defined: fused pose (x, y, θ) estimate from encoders + IMU.
- Toolchain switched to native ESP-IDF (already installed) instead of PlatformIO.
- Board: ESP32-S3-DevKitC clone, two USB-C ports — one wired to the S3's native USB peripheral (idVendor=303a, "USB JTAG/serial debug unit"), one to an onboard WCH USB-serial chip (idVendor=1a86, PID 55d3, "USB Single Serial").
- **Gotcha found & fixed:** `/dev/ttyACM*` never appeared on either port because `cdc_acm` was blacklisted system-wide in `/etc/modprobe.d/blacklist-qcserial.conf` (leftover from [[jz01-openstick-flash]], now finished). Removed the `blacklist cdc_acm` line, `sudo modprobe cdc_acm` — `/dev/ttyACM0` now appears (bound to the 1a86:55d3 WCH chip's port; ch341/ch343 drivers were never actually needed since cdc_acm's alias table already covers that PID).
- Found existing half-done work at ~/Documents/PlatformIO/Projects/wheel_robot: a PlatformIO/Arduino project (platformio.ini targets esp32-s3-devkitc1-n8r8) with src/main.cpp already declaring TB6612 pins (PWMA=4, AIN2=5, AIN1=6, STBY=7, BIN1=15, BIN2=16, PWMB=17, GND=18), encoder pins (enAA=8, enAB=3, enBA=46, enBB=9), and GY-85 I2C pins (sda=11, scl=10) — motor speed/interrupt code commented out, nothing implemented yet. Also had a cloned reference repo `esp-idf-gy85` nested inside it (ready-made ADXL345/ITG3200/HMC5883L/I2Cdev drivers + MadgwickAHRS/Kalman fusion, WiFi streaming examples).
- Decision: redo firmware in native ESP-IDF (porting the pin assignments above, not the Arduino code itself); esp-idf-gy85 set aside as an answer-key only, not to be copied from directly — GY-85 drivers to be written from scratch in Modules 2–3.
- Moved everything into the course folder: `~/robot_learner/reference/wheel_robot_platformio/` (old project) and `~/robot_learner/reference/esp-idf-gy85/` (reference repo).
- Next: Module 0 continued — create a fresh ESP-IDF hello_world project (e.g. `~/robot_learner/firmware/`) and verify `idf.py build/flash/monitor` round-trip on /dev/ttyACM0.

## 2026-08-08 (cont.) — Tutorial series planning
- User asked for a 1-by-1 tutorial series (60-90 min each) ending in the robot driving as a 2-wheel differential robot, before localization work.
- Plan written and approved: reorders syllabus to drive-first — T1 ESP-IDF bring-up, T2 wiring/pin/power audit, T3 TB6612+LEDC per-wheel, T4 open-loop drive(v,ω) [robot runs = milestone], T5 encoders (PCNT), T6 tick→metres calibration, T7 closed-loop PI + square capstone. IMU/GY-85/fusion deferred to after T7.
- **Finding: ESP-IDF was never actually installed on this Linux box.** VS Code Espressif extension present but its globalStorage never initialized; settings.json had stale Windows paths (d:\ESPTOOLS\...) synced from another machine; only export.sh on disk belongs to PlatformIO's bundled framework-espidf (not to be reused as IDF_PATH — it's stripped down). Cleared the stale idf.pythonInstallPath/idf.gitPathWin keys from settings.json (backup at settings.json.bak-robotlearner).
- Started headless install via `~/.espressif/eim_gui/eim install -t esp32s3 -i v5.3.2` (this is infra, driven by tutor not Socratic). First attempt failed: missing system prereqs flex/bison/gperf/ccache. Wrote `fix_idf_prereqs.sh` (apt install, needs sudo) for user to run themselves.
- Next: user runs fix_idf_prereqs.sh, then re-launch eim install in background; once idf.py --version works, write and hand over Tutorial 1 (~/robot_learner/tutorials/t01-*.html).

## 2026-08-09 — ESP-IDF install running
- User ran fix_idf_prereqs.sh: flex/bison/gperf/ccache confirmed installed via dpkg.
- Relaunched `eim install -t esp32s3 -i v5.3.2 -n true --cleanup true` in background (log: idf_install.log). Prereqs check passed, mirrors selected (github.com, pypi.org), cloning esp-idf v5.3.2 now.
- Board currently unplugged (no /dev/ttyACM0) — not blocking, install doesn't need the board.
- Next: poll idf_install.log until done, verify `idf.py --version`, then write Tutorial 1 HTML page.

## 2026-08-09 (cont.) — ESP-IDF install finished, Tutorial 1 unblocked
- eim install completed successfully: "You have successfully installed ESP-IDF" (ESP-IDF v5.3.2, esp32s3 target, ~5.5 min total).
- Verified: `source ~/.espressif/tools/activate_idf_v5.3.2.sh` then `idf.py --version` → "ESP-IDF v5.3.2-dirty" (dirty suffix is harmless, normal for shallow submodule clone). IDF_PATH set correctly.
- One non-fatal install error: couldn't copy OpenOCD udev rules to /etc/udev/rules.d (needs sudo) — irrelevant for now (flashing over WCH UART/cdc_acm, not JTAG); revisit only if JTAG debugging is needed later.
- Tutorial 1 (~/robot_learner/tutorials/t01-esp-idf-bringup.html) is now fully unblocked — toolchain ready, board at /dev/ttyACM0 (when plugged in).

## 2026-08-15 → 2026-09-05 — Firmware stages and the new PCB (from git log)
- 08-15 initial backup of firmware, tutorials, docs, PCB snapshot. Tutorials in the repo: t01, t05–t08 (t02–t04 are not there).
- 08-23 `GY85_Check`: I2C bring-up, then live data from all three sensors. The "magnetometer" is a QMC5883L clone (address 0x0D), not an HMC5883L.
- 08-31 `Motor_Drive_With_IDF` tried on DRV8833, then went back to TB6612FNG on the new board.
- 09-05 combined bring-up test on the new PCB pinout: both motors both ways, both encoders, GY-85 live data, battery voltage sense. Pin map is in `pins.txt`.

## 2026-10-15 — Encoders to 4x, PWM→rpm table, first P speed control
Code lives in `firmware/Motor_Drive_With_IDF/main/main.c` (main checkout, not committed yet).
- **Encoder was only 2x decode.** Each PCNT unit had one channel. Hand-turning the wheel one turn gave ~957 ticks (expected 990 = 11 PPR × 2 × 45). Fix: a second channel per motor on the same unit (edge/level pins swapped). Rule: both channels of a motor use level action (KEEP, INVERSE); their edge actions are swapped; motor B is mirrored so its actions are the opposite of motor A.
- Motor A now gives **1980 ticks per wheel turn** (11 PPR × 4 × 45), checked by hand. Motor B has the same two-channel setup and moved in the same direction as A; its ticks per turn were not reported — **still to verify**.
- Encoder pins are swapped in the `#define`s (A: 11/10, B: 13/12) so forward counts **positive**. The names still say "phase A/B" — they are swapped on purpose.
- Speed: `rpm = delta / 0.02 s / 1980 × 60`, `m/s = rpm × π × 0.065 / 60`. One tick per 20 ms is **1.5 rpm**, so steady readings jump by about ±1.5–2.5 rpm. That is quantisation, not instability.
- **PWM → rpm, wheel in the air, 12.55 V** (steps of 5 %): 0, 5, 10 % → 0 rpm; 15 % → 21.2; 30 % → 50; 50 % → 89.4; 75 % → 137.9; 100 % → 190.9 rpm (0.649 m/s). Dead zone is between 10 and 15 %. Line fit above the dead zone: **rpm ≈ 2.01 × PWM − 9.9** (check: 50 % gives 90.5, measured 89.4). The `rpm/pwm` column is not constant because of the negative offset; the slope is. Floor (loaded) table not measured yet.
- **Battery sense** (GPIO14, ADC2 ch3): divider 100k/22k → ×5.545. The plain `raw/4095 × 3.3 × 5.545` read about 8 % low (10.5 V on an 11.5 V pack, 11.59 V on 12.6 V). A gain of **1.09** fixes it to about ±0.05 V. The IDF curve-fit calibration (used in the 09-05 test) is still the better option.
- **Serial gotcha:** the native USB port (303a, "USB JTAG serial debug unit") is output-only (console is UART0 with USB-JTAG as secondary). Keyboard input in `idf.py monitor` only works on the other USB port (WCH, 1a86). "Writing to serial is timing out" means wrong port.
- **P control with feed-forward:** `pwm = (target + 9.9) / 2.01 + Kp × (target − rpm)`. Hold-the-wheel-then-release test at 40 rpm, 20 ms per line:
  - Kp 0.35: 36 rpm after ~80 ms, no real overshoot.
  - Kp 0.7: ~100 ms, no real overshoot.
  - Kp 1.0: ~80 ms, but overshoots to 45.5 rpm (~14 %) and rings for ~160 ms.
  - Use **Kp ≈ 0.5**. Loop gain = Kp × 2.01; above ~1 you get overshoot. A bigger Kp also makes the 1-tick noise bigger.
  - With the wheel in the air the feed-forward is already almost exact, so the steady error is < 1 rpm. Under load P-only will leave an error → add the I term (t08).
- Two false alarms: the "restart at PWM ≥ 45" was a key press (`0` set PWM to 0), not a reset. The random GPIO 48 RGB LED is a floating pin (not fixed yet: set it as output, low).
- **Open items:** verify motor B (1980 per turn, sign of +PWM vs +rpm — if opposite, the loop runs away), measure the PWM→rpm table on the floor, PI controller (t08), battery check (read every cycle, hysteresis, command 0 when low instead of sleeping), wrap handling for the PCNT counter (±32767, ~5 s at full speed), tidy `main.c` (per-motor struct, named constants, `motor.c/h`).
