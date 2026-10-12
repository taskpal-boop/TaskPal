"""PWR-06: interrupt the M5Paper in the middle of saving, N times, and check the files survive.

Each round: reset the device (RTS/EN), wait for the boot report "[app] files: ...", start the 'W'
stress command (back-to-back progress saves), then reset again at a random moment during the saves.
The next boot reports whether settings/lists/progress decoded ("ok") or were unreadable ("BAD"),
and how many leftover .tmp files it removed. An EN reset stops the chip mid-write like a power cut
(the supply itself stays on).

Usage:  conda run -n taskpal python tools/pwr06_cut_test.py COM3 [rounds]
"""
import random
import re
import sys
import time

import serial

BOOT_RE = re.compile(r"\[taskpal\] boot .*?\((\d+) tmp removed\)")
FILES_RE = re.compile(r"\[app\] files: settings (\w+), lists (\w+), progress (\w+)")


def reset(s):
    s.rts = True
    time.sleep(0.1)
    s.rts = False


def read_until(s, pattern, timeout):
    buf, end = "", time.time() + timeout
    while time.time() < end:
        chunk = s.read(4096).decode("utf-8", "replace")
        buf += chunk
        m = re.search(pattern, buf)
        if m:
            return m, buf
    return None, buf


def main():
    port = sys.argv[1]
    rounds = int(sys.argv[2]) if len(sys.argv) > 2 else 20
    s = serial.Serial()
    s.port, s.baudrate, s.timeout = port, 115200, 0.05
    s.dtr = False
    s.rts = False
    s.open()
    results = []
    for i in range(rounds + 1):
        reset(s)
        m, buf = read_until(s, FILES_RE.pattern + r"[\s\S]*?\[taskpal\] boot", 20)
        files = FILES_RE.search(buf)
        boot = BOOT_RE.search(buf)
        if not files or not boot:
            print(f"round {i}: no boot report (device asleep or hung?). Serial tail:")
            print("    " + buf[-1500:].replace("\n", "\n    "))
            results.append((i, "?", "?", "?", "?"))
            continue
        st = files.groups()
        tmp = boot.group(1)
        results.append((i, *st, tmp))
        print(f"round {i:2d}: settings {st[0]:7s} lists {st[1]:7s} progress {st[2]:7s} tmp removed {tmp}")
        if i == rounds:
            break
        time.sleep(1.5)  # let the first screen render
        s.reset_input_buffer()
        s.write(b"W\n")
        m, _ = read_until(s, r"stress saves start", 5)
        if not m:
            print(f"round {i}: stress command not acknowledged")
            continue
        time.sleep(random.uniform(0.05, 2.0))  # cut somewhere inside the save loop
    s.close()
    bad = [r for r in results if "BAD" in r or "?" in r]
    tmp_total = sum(int(r[4]) for r in results if r[4].isdigit())
    print(f"\n{len(results) - 1} cuts, {len(bad)} rounds with a BAD/unknown file, {tmp_total} .tmp files cleaned up")
    print("PWR-06 PASS" if not bad else "PWR-06 FAIL")


if __name__ == "__main__":
    main()
