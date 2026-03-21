#!/usr/bin/env python3
import curses
import os
import signal
import argparse

# -------------------------
# INI file helpers
# -------------------------

def load_config(path):
    cfg = {}
    current = None
    with open(path) as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith("#") or line.startswith(";"):
                continue
            if line.startswith("[") and line.endswith("]"):
                current = line[1:-1]
                cfg[current] = {}
                continue
            if "=" in line and current:
                k, v = line.split("=", 1)
                cfg[current][k.strip()] = v.strip()
    return cfg


def save_config(path, cfg):
    with open(path, "w") as f:
        for section, kv in cfg.items():
            f.write(f"[{section}]\n")
            for k, v in kv.items():
                f.write(f"{k} = {v}\n")
            f.write("\n")


# -------------------------
# SIGHUP helper
# -------------------------

def send_sighup(pid):
    try:
        os.kill(pid, signal.SIGHUP)
    except Exception as e:
        print("Could not send SIGHUP:", e)


# -------------------------
# Bar drawing helper
# -------------------------

def draw_bar(stdscr, x, y, height, value, min_val, max_val, label, selected):
    # Numeric value ABOVE the bar
    if isinstance(value, float):
        stdscr.addstr(y - height - 2, x - 3, f"{value:5.1f}")
    else:
        stdscr.addstr(y - height - 2, x - 3, f"{value:5d}")

    # Compute fill
    ratio = (value - min_val) / (max_val - min_val)
    ratio = max(0.0, min(1.0, ratio))
    filled = int(ratio * height)

    # Draw bar
    for i in range(height):
        if i < filled:
            if ratio < 0.5:
                color = curses.color_pair(1)
            elif ratio < 0.8:
                color = curses.color_pair(2)
            else:
                color = curses.color_pair(3)
            stdscr.addstr(y - i, x, "█", color)
        else:
            stdscr.addstr(y - i, x, " ")

    # Label BELOW the bar
    if selected:
        stdscr.addstr(y + 1, x - len(label)//2, f"[{label}]")
    else:
        stdscr.addstr(y + 1, x - len(label)//2, label)


# -------------------------
# Help popup
# -------------------------

def show_help(stdscr):
    h, w = stdscr.getmaxyx()
    win_h, win_w = 12, 50
    y0 = max(0, h // 2 - win_h // 2)
    x0 = max(0, w // 2 - win_w // 2)
    win = curses.newwin(win_h, win_w, y0, x0)
    win.box()

    help_lines = [
        "Gain Mixer Help",
        "",
        "Left/Right : Select control",
        "Up/Down    : Adjust value",
        "Tab        : Next control",
        "",
        "b          : Toggle Bias-T",
        "a          : Toggle AGC (RTL-SDR)",
        "p/P        : Adjust PPM (RTL-SDR)",
        "",
        "s          : Send SIGHUP",
        "q          : Quit",
    ]

    for i, line in enumerate(help_lines):
        if 1 + i < win_h - 1:
            win.addstr(1 + i, 2, line[:win_w - 4])

    win.refresh()
    win.getch()
    del win


# -------------------------
# Gain Mixer UI
# -------------------------

def mixer(stdscr, config_path, pid):
    curses.curs_set(0)
    curses.start_color()

    curses.init_pair(1, curses.COLOR_GREEN, curses.COLOR_BLACK)
    curses.init_pair(2, curses.COLOR_YELLOW, curses.COLOR_BLACK)
    curses.init_pair(3, curses.COLOR_RED, curses.COLOR_BLACK)

    bar_height = 12
    selected = 0

    while True:
        h, w = stdscr.getmaxyx()
        if h < 25 or w < 40:
            stdscr.clear()
            stdscr.addstr(1, 2, "Terminal too small.")
            stdscr.addstr(2, 2, "Resize to at least 40x25.")
            stdscr.refresh()
            key = stdscr.getch()
            if key == ord('q'):
                return
            continue

        cfg = load_config(config_path)

        if "rtlsdr" in cfg:
            dev = "rtlsdr"
        elif "airspy" in cfg:
            dev = "airspy"
        else:
            raise RuntimeError("Config must contain [rtlsdr] or [airspy] section")

        s = cfg[dev]

        frequency = int(s.get("frequency", "1090000000"))

        gain = float(s.get("gain", "20")) if dev == "rtlsdr" else None
        ppm = int(s.get("ppm", "0")) if dev == "rtlsdr" else None
        bias = (s.get("bias_tee", "0") == "1") if dev == "rtlsdr" else None
        agc = (s.get("agc", "0") == "1") if dev == "rtlsdr" else None

        lna = int(s.get("lna_gain", "5")) if dev == "airspy" else None
        mixer_gain = int(s.get("mixer_gain", "5")) if dev == "airspy" else None
        vga = int(s.get("vga_gain", "5")) if dev == "airspy" else None
        bias_airspy = (s.get("bias_tee", "0") == "1") if dev == "airspy" else None

        stdscr.clear()
        stdscr.addstr(1, 2, f"Gain Mixer — {dev.upper()}  (h=help)")
        stdscr.addstr(3, 4, f"Frequency:   {frequency} Hz")

        if dev == "rtlsdr":
            bars = [
                ("GAIN", gain, 0, 49.6),
            ]
        else:
            bars = [
                ("LNA", lna, 0, 15),
                ("MIX", mixer_gain, 0, 15),
                ("VGA", vga, 0, 15),
            ]

        for i, (label, val, mn, mx) in enumerate(bars):
            x = 6 + i * 10
            draw_bar(stdscr, x, 18, bar_height, val, mn, mx, label, selected == i)

        if dev == "rtlsdr":
            stdscr.addstr(20, 4, f"PPM:    {ppm}   (p/P)")
            stdscr.addstr(21, 4, f"Bias-T: {'ON ' if bias else 'OFF'}   (b)")
            stdscr.addstr(22, 4, f"AGC:    {'ON ' if agc else 'OFF'}   (a)")
        else:
            stdscr.addstr(20, 4, f"Bias-T: {'ON ' if bias_airspy else 'OFF'}   (b)")

        footer_y = h - 2
        footer = "←/→ select   ↑/↓ adjust   h help   s SIGHUP   q quit"
        stdscr.addstr(footer_y, 2, footer[:max(0, w - 4)])

        stdscr.refresh()

        key = stdscr.getch()
        changed = False

        if key == ord('q'):
            break

        if key == ord('h'):
            show_help(stdscr)
            continue

        if key == ord('s'):
            send_sighup(pid)

        if key == curses.KEY_RIGHT or key == ord('\t'):
            selected = (selected + 1) % len(bars)

        if key == curses.KEY_LEFT:
            selected = (selected - 1) % len(bars)

        if key == curses.KEY_UP:
            label, val, mn, mx = bars[selected]
            val = min(val + 1, mx)
            if dev == "rtlsdr":
                s["gain"] = f"{val:.1f}"
            else:
                if label == "LNA":
                    s["lna_gain"] = str(val)
                elif label == "MIX":
                    s["mixer_gain"] = str(val)
                elif label == "VGA":
                    s["vga_gain"] = str(val)
            changed = True

        if key == curses.KEY_DOWN:
            label, val, mn, mx = bars[selected]
            val = max(val - 1, mn)
            if dev == "rtlsdr":
                s["gain"] = f"{val:.1f}"
            else:
                if label == "LNA":
                    s["lna_gain"] = str(val)
                elif label == "MIX":
                    s["mixer_gain"] = str(val)
                elif label == "VGA":
                    s["vga_gain"] = str(val)
            changed = True

        if dev == "rtlsdr":
            if key == ord('p'):
                ppm += 1
                s["ppm"] = str(ppm)
                changed = True
            if key == ord('P'):
                ppm -= 1
                s["ppm"] = str(ppm)
                changed = True
            if key == ord('b'):
                s["bias_tee"] = "0" if bias else "1"
                changed = True
            if key == ord('a'):
                s["agc"] = "0" if agc else "1"
                changed = True

        if dev == "airspy":
            if key == ord('b'):
                s["bias_tee"] = "0" if bias_airspy else "1"
                changed = True

        if changed:
            save_config(config_path, cfg)
            send_sighup(pid)


# -------------------------
# Main
# -------------------------

def main():
    parser = argparse.ArgumentParser(description="Gain Mixer — interactive SDR gain control")
    parser.add_argument("--config", required=True, help="Path to SDR config INI file")
    parser.add_argument("--pid", required=True, type=int, help="PID of running daemon")
    args = parser.parse_args()

    curses.wrapper(mixer, args.config, args.pid)


if __name__ == "__main__":
    main()
