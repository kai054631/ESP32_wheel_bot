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
