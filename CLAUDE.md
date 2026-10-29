# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

A differential-drive robot with an ESP32-S3, a GY-85 IMU, a TB6612FNG motor driver, and two geared motors with encoders.
The repository is also a tutoring course. The goal is that the robot knows its pose (x, y, theta) and drives to a point at a commanded speed.
The course plan is in `syllabus.md`. The log and the open items are in `progress.md`. The pin map is in `pins.txt`.

## How to work here

- The user writes the firmware. Claude explains, asks questions, checks the code, and finds bugs. Do not edit the user's `main.c`, `motor.c/h`, or `odom.c/h`.
  The user asked Claude to write `net.c/h`, `tools/robot_map.py`, and the docs. Those are allowed.
- The live code is in the main checkout (`~/robot_learner`, branch `main`). It is often not committed. A Claude session in a worktree cannot edit there.
  To save the work, copy the code files into the worktree and commit them there.
- Never copy or commit `wifi_secret.h`. It holds the Wi-Fi password and it is in `.gitignore`. Only `wifi_secret.h.example` is tracked.
- `reference/` is an answer key (an old PlatformIO project and `esp-idf-gy85`). Do not copy code from it.
- Write English in ASD-STE100 (Simplified Technical English): short sentences, active voice, one word for one meaning.
  Chat replies follow the language setting of the session (Chinese at the time of writing).

## Commands

All firmware commands run in `firmware/Motor_Drive_With_IDF`. The other folders in `firmware/` are separate learning examples.

```bash
. ~/.espressif/tools/activate_idf_v5.3.2.sh   # ESP-IDF v5.3.2. The VS Code extension uses v6.0.2, but the verified builds use v5.3.2.
idf.py build
idf.py -p <port> flash monitor                # find <port> with: lsusb; ls /dev/ttyACM* /dev/ttyUSB*
python3 tools/robot_map.py [host]             # live x-y map. Also: --demo (fake robot), --selftest (no window)
```

- There is no firmware unit test. Build, then test on the robot.
- Serial port rule: the board has two USB ports. The native port (`303a`) only shows output. Key input works only on the WCH port (`1a86`).
  The error "Writing to serial is timing out" means the wrong port.
- JTAG needs the OpenOCD udev rules. Copy `60-openocd.rules` from `~/.espressif/tools/openocd-esp32/<version>/openocd-esp32/share/openocd/contrib/` to `/etc/udev/rules.d/`.
- Wi-Fi: copy `main/wifi_secret.h.example` to `main/wifi_secret.h` and set the SSID and password. Use a 2.4 GHz network.
  The robot prints its IP address at boot (`got ip: ...`). The address comes from DHCP and can change.

## Architecture

`firmware/Motor_Drive_With_IDF/main/` is the live project.

- `main.c` sets up the pins, the LEDC PWM, and the PCNT encoders (4x decode). Then it runs a 20 ms loop (`vTaskDelayUntil`).
  Each cycle: read one key, check the battery, run `motor_update` for both motors, run `odom_update`, print.
- `motor.c/h` has one `motor_t` per motor. `motor_update` does: measure (PCNT count, wrap, rpm, distance) -> feed-forward -> P + I -> clamp -> dead zone -> `motor_drive`.
  `motor_kp` and `motor_ki` are globals shared by both motors.
- `odom.c/h` has `d = (dL + dR) / 2` and `dtheta = (dR - dL) / TRACK_WIDTH`. It updates x, y, and theta.
- `net.c/h` runs Wi-Fi (station mode) and a TCP server on port 3333. The server accepts ONE client at a time.
  Queues keep the control loop from waiting for the network. The API is `net_init`, `net_printf`, `net_getc`, and `net_connected`.
- `tools/robot_map.py` runs on the PC. It reads the text lines from the robot and draws the map. It sends the keys back on the same connection.
  Use it instead of `nc`, because the robot accepts only one client. For `nc` in single-key mode: `stty -icanon -echo; nc <ip> 3333; stty sane`.

Couplings. A change in one place breaks the other:
- The text that `main.c` prints (`Odom: X: ...` and `Target RPM: ...`) is parsed by regular expressions in `tools/robot_map.py`.
- The key letters in `main.c` (`w a s d`, `r` = zero the odometry, space = stop) match `SEND_KEYS` in `tools/robot_map.py`.
- `TRACK_WIDTH` in `odom.h` is used by the odometry. `robot_map.py` has its own size constants, only for drawing.

## Hardware facts that the code does not show

- Motor A is the LEFT wheel. Motor B is the RIGHT wheel. The code follows the PCB silkscreen.
- Encoder: 11 PPR x 4 (decode) x 45 (gear) = 1980 ticks per wheel turn. Wheel diameter: 68.5 mm (measured).
- Size: 209 mm over the outer faces of the wheels, 157 mm between the wheels, 26 mm per wheel, 213 mm long.
  The drive wheels are at the front. The dead (caster) wheel is at the rear. The odometry point is the midpoint of the drive axle.
- Track width (centre to centre of the wheels): 183 mm from the sizes. The effective value is larger. The calibration is in progress (about 0.19 m). Read `progress.md` for the latest value.
- PWM below about 15 % does not turn the motor (dead zone). 15 % gives about 21 rpm. The feed-forward `(|rpm| + 9.9) / 2.01` comes from an unloaded fit. Measure it again on the floor.
- The PCNT limit is +-32767. `motor_update` handles the wrap. Do not remove this code.
- Battery sense is on GPIO 14 (ADC2). ADC2 and Wi-Fi share hardware. When Wi-Fi is on, `adc_oneshot_read` can return `ESP_ERR_TIMEOUT`.
  Do not use `ESP_ERROR_CHECK` on that call.
- GPIO 48 (RGB LED) floats and flickers. It is not fixed.
- The "HMC5883L" on the GY-85 is a QMC5883L (I2C address 0x0D). Remember this for the IMU work.
- Safety: the robot has no dead-man timeout yet. If the Wi-Fi link drops, the robot keeps its last command. Test with the wheels off the floor.
