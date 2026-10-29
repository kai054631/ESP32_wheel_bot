#!/usr/bin/env python3
"""Live x-y map of the robot, read from its Wi-Fi TCP stream (port 3333).

Parses the lines the firmware prints:
  Odom: X: 83,   Y:53,    theta:65.3
  Target RPM: 35.0, RPM MOTOR A : 34.8485   RPM MOTOR B : 33.3333   VOLT:11.48
and draws the pose (position + heading) and the trail.

The robot serves ONE TCP client, so this window also forwards the keys
w a s d and SPACE (stop) to the robot (use it instead of `nc`).

  python3 tools/robot_map.py                  # 192.168.68.148:3333
  python3 tools/robot_map.py 192.168.68.150   # other address
  python3 tools/robot_map.py --demo           # fake robot driving a circle
  python3 tools/robot_map.py --selftest       # no window, checks the parser
"""
import argparse
import collections
import math
import re
import socket
import sys
import threading
import time

ODOM_RE = re.compile(r"Odom:\s*X:\s*(-?[\d.]+),\s*Y:\s*(-?[\d.]+),\s*theta:\s*(-?[\d.]+)")
RPM_RE = re.compile(
    r"Target RPM:\s*(-?[\d.]+),\s*RPM MOTOR A\s*:\s*(-?[\d.]+)\s+RPM MOTOR B\s*:\s*(-?[\d.]+)\s+VOLT:\s*(-?[\d.]+)"
)
SEND_KEYS = "wasdr +-"  # forwarded as they are (space = stop, r = zero the odometry, + and - = speed)


def parse_line(line):
    """Return ('odom', x_mm, y_mm, theta_deg) / ('rpm', target, a, b, volt) / None."""
    m = ODOM_RE.search(line)
    if m:
        return ("odom",) + tuple(float(v) for v in m.groups())
    m = RPM_RE.search(line)
    if m:
        return ("rpm",) + tuple(float(v) for v in m.groups())
    return None


class State:
    """Latest robot data, shared between the network thread and the GUI."""

    def __init__(self):
        self.lock = threading.Lock()
        self.pose = (0.0, 0.0, 0.0)  # x_m, y_m, theta_deg (raw, robot frame)
        self.origin = (0.0, 0.0)  # 'c' key moves the map origin here
        self.trail = collections.deque(maxlen=5000)  # (x_m, y_m), relative to origin
        self.rpm = None  # (target, a, b, volt)
        self.connected = False
        self.last_rx = 0.0

    def feed(self, line):
        item = parse_line(line)
        if item is None:
            return
        with self.lock:
            self.last_rx = time.time()
            if item[0] == "odom":
                x, y, th = item[1] / 1000.0, item[2] / 1000.0, item[3]
                self.pose = (x, y, th)
                self.trail.append((x - self.origin[0], y - self.origin[1]))
            else:
                self.rpm = item[1:]

    def reset_origin(self, origin=None):
        with self.lock:
            self.origin = origin or self.pose[:2]
            self.trail.clear()
            self.trail.append((0.0, 0.0))


class Link(threading.Thread):
    """Connects to the robot, feeds State, reconnects forever."""

    def __init__(self, host, port, state):
        super().__init__(daemon=True)
        self.host, self.port, self.state = host, port, state
        self.sock = None

    def send(self, key):
        s = self.sock
        if s is None:
            return
        try:
            s.sendall(key.encode())
        except OSError:
            pass  # the read loop notices the drop and reconnects

    def run(self):
        while True:
            try:
                s = socket.create_connection((self.host, self.port), timeout=3)
                s.settimeout(1.0)
                self.sock = s
                self.state.connected = True
                buf = b""
                while True:
                    try:
                        data = s.recv(1024)
                    except socket.timeout:
                        continue
                    if not data:
                        break
                    buf += data
                    while b"\n" in buf:
                        line, buf = buf.split(b"\n", 1)
                        self.state.feed(line.decode("ascii", "replace"))
            except OSError:
                pass
            self.sock = None
            self.state.connected = False
            time.sleep(1.0)


class Demo(threading.Thread):
    """Fake robot: drives a circle of radius 0.5 m, prints the same text lines."""

    def __init__(self, state):
        super().__init__(daemon=True)
        self.state = state

    def run(self):
        self.state.connected = True
        t = 0.0
        while True:
            t += 0.1
            x, y = 0.5 * math.sin(t * 0.4), 0.5 * (1 - math.cos(t * 0.4))
            th = math.degrees(t * 0.4)
            self.state.feed("Odom: X: %.0f,   Y:%.0f,    theta:%.1f" % (x * 1000, y * 1000, th))
            self.state.feed("Target RPM: 35.0, RPM MOTOR A : 34.8   RPM MOTOR B : 36.1   VOLT:11.48")
            time.sleep(0.1)


# Robot outline in metres. Robot frame: +x is forward.
# The origin is the midpoint of the drive axle. The odometry tracks this point.
# Measured sizes: 209 mm over the outer faces of the wheels, 157 mm between the wheels.
# Each wheel is 26 mm wide. The track width (centre to centre) is 183 mm.
# The robot is 213 mm long. The drive wheels are at the front. The dead wheel is at the rear.
TRACK, WHEEL_W, WHEEL_D, LENGTH, GAP = 0.183, 0.026, 0.0685, 0.213, 0.157
HALF_W = GAP / 2 + WHEEL_W
BODY = [(0.0, HALF_W), (-LENGTH, HALF_W), (-LENGTH, -HALF_W), (0.0, -HALF_W)]
WHEELS = [
    [(WHEEL_D / 2, yc + WHEEL_W / 2), (-WHEEL_D / 2, yc + WHEEL_W / 2),
     (-WHEEL_D / 2, yc - WHEEL_W / 2), (WHEEL_D / 2, yc - WHEEL_W / 2)]
    for yc in (TRACK / 2, -TRACK / 2)
]
CASTER = [(-LENGTH + 0.025, 0.0)]
NOSE = [(0.075, 0.0), (0.02, 0.03), (0.02, -0.03)]  # points forward


def place(pts, x, y, th_deg):
    """Move robot-frame points to the map: rotate by theta, then shift to (x, y)."""
    th = math.radians(th_deg)
    c, s = math.cos(th), math.sin(th)
    return [(x + px * c - py * s, y + px * s + py * c) for px, py in pts]


def build_ui(state, link):
    import matplotlib.pyplot as plt
    from matplotlib.patches import Polygon
    from matplotlib.ticker import MultipleLocator

    for k in list(plt.rcParams):  # drop matplotlib's own shortcuts ('s' = save ...)
        if k.startswith("keymap."):
            plt.rcParams[k] = []

    fig, ax = plt.subplots(figsize=(7, 7))
    ax.set_aspect("equal")
    ax.set_xlabel("x forward (m)")
    ax.set_ylabel("y left (m)")
    ax.xaxis.set_major_locator(MultipleLocator(0.5))
    ax.yaxis.set_major_locator(MultipleLocator(0.5))
    ax.grid(True, alpha=0.4)
    ax.plot([0], [0], "ko", ms=5)  # origin
    (trail_line,) = ax.plot([], [], "-", color="tab:blue", lw=1.5)
    body = Polygon(place(BODY, 0, 0, 0), closed=True, fill=False, ec="0.3", lw=1.5, zorder=4)
    wheels = [Polygon(place(w, 0, 0, 0), closed=True, color="0.2", zorder=5) for w in WHEELS]
    arrow = Polygon(place(NOSE, 0, 0, 0), closed=True, color="tab:red", zorder=6)
    for patch in [body, arrow] + wheels:
        ax.add_patch(patch)
    (caster,) = ax.plot([], [], "o", color="0.5", ms=5, zorder=5)
    info = ax.text(0.01, 0.99, "", transform=ax.transAxes, va="top", family="monospace", fontsize=9)
    ax.set_title("keys: w a s d, SPACE = stop, r = zero odometry -> robot   c = new origin   q = quit")

    def update(_frame=None):
        with state.lock:
            x = state.pose[0] - state.origin[0]
            y = state.pose[1] - state.origin[1]
            th = state.pose[2]
            trail = list(state.trail)
            rpm = state.rpm
            age = time.time() - state.last_rx
        if trail:
            trail_line.set_data([p[0] for p in trail], [p[1] for p in trail])
        body.set_xy(place(BODY, x, y, th))
        arrow.set_xy(place(NOSE, x, y, th))
        for patch, shape in zip(wheels, WHEELS):
            patch.set_xy(place(shape, x, y, th))
        cx_, cy_ = place(CASTER, x, y, th)[0]
        caster.set_data([cx_], [cy_])
        pts = trail + [(x, y)]
        half = max(1.0, max(max(abs(p[0]), abs(p[1])) for p in pts) + 0.3)  # square, grows with the trail
        cx = (max(p[0] for p in pts) + min(p[0] for p in pts)) / 2
        cy = (max(p[1] for p in pts) + min(p[1] for p in pts)) / 2
        ax.set_xlim(cx - half, cx + half)
        ax.set_ylim(cy - half, cy + half)
        if not state.connected:
            status = "DISCONNECTED (retrying)"
        elif age > 3:
            status = "connected, no data for %.0f s" % age
        else:
            status = "connected"
        text = "x %7.3f m\ny %7.3f m\nth %6.1f deg\n" % (x, y, th)
        if rpm:
            text += "target %5.1f rpm\nA %7.2f  B %7.2f\nbattery %.2f V\n" % rpm
        info.set_text(text + status)
        return trail_line, body, arrow, caster, info

    def on_key(event):
        if event.key in list(SEND_KEYS):
            link.send(event.key)
            if event.key == "r":  # the robot sets x, y to 0, so the map starts again at 0
                state.reset_origin((0.0, 0.0))
        elif event.key == "c":
            state.reset_origin()
        elif event.key == "q":
            plt.close(fig)

    fig.canvas.mpl_connect("key_press_event", on_key)
    return fig, update


def selftest():
    import matplotlib

    matplotlib.use("Agg")
    assert parse_line("Odom: X: 83,   Y:53,    theta:65.3") == ("odom", 83.0, 53.0, 65.3)
    assert parse_line("Odom: X: -311,   Y:2,    theta:-0.4") == ("odom", -311.0, 2.0, -0.4)
    r = parse_line("Target RPM: 35.0, RPM MOTOR A : 34.8485   RPM MOTOR B : 33.3333   VOLT:11.48\n")
    assert r == ("rpm", 35.0, 34.8485, 33.3333, 11.48), r
    assert parse_line("battery LOW VOLT:9.90") is None
    assert parse_line("Kp=0.50") is None
    st = State()
    st.connected = True
    for i in range(30):
        st.feed("Odom: X: %d,   Y:%d,    theta:%.1f" % (i * 20, i * 5, i * 2.0))
    st.feed("Target RPM: 35.0, RPM MOTOR A : 34.8   RPM MOTOR B : 36.1   VOLT:11.48")
    assert len(st.trail) == 30 and abs(st.pose[0] - 0.58) < 1e-9
    st.reset_origin()
    st.feed("Odom: X: 700,   Y:150,    theta:60.0")
    assert abs(st.trail[-1][0] - 0.12) < 1e-9  # relative to the new origin
    sent = []

    class FakeLink:
        send = staticmethod(sent.append)

    fig, update = build_ui(st, FakeLink)
    update()
    fig.canvas.draw()
    from matplotlib.backend_bases import KeyEvent

    for k in ("w", " ", "x"):  # 'x' is not a robot key
        fig.canvas.callbacks.process("key_press_event", KeyEvent("key_press_event", fig.canvas, k))
    assert sent == ["w", " "], sent
    nose = place(NOSE, 0, 0, 90)[0]
    assert abs(nose[0]) < 1e-9 and nose[1] > 0  # theta 90 deg: nose points to +y
    rear = place([(-LENGTH, 0.0)], 1.0, 2.0, 90)[0]
    assert abs(rear[0] - 1.0) < 1e-9 and abs(rear[1] - (2.0 - LENGTH)) < 1e-9  # tail trails behind
    assert abs((HALF_W * 2) - 0.209) < 1e-9 and abs((TRACK / 2 + WHEEL_W / 2) - HALF_W) < 1e-9
    print("selftest OK")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("host", nargs="?", default="192.168.68.148")
    ap.add_argument("--port", type=int, default=3333)
    ap.add_argument("--demo", action="store_true", help="fake robot, no network")
    ap.add_argument("--selftest", action="store_true", help="check the parser, no window")
    args = ap.parse_args()
    if args.selftest:
        selftest()
        return
    state = State()
    link = Link(args.host, args.port, state)
    (Demo(state) if args.demo else link).start()
    fig, update = build_ui(state, link)
    from matplotlib.animation import FuncAnimation
    import matplotlib.pyplot as plt

    anim = FuncAnimation(fig, update, interval=100, cache_frame_data=False)  # noqa: F841 (keep a reference)
    plt.show()


if __name__ == "__main__":
    sys.exit(main())
