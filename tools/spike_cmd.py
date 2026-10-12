"""Send one command to a TaskPal spike firmware over serial and print its output.

Opens the port without toggling DTR/RTS (so the M5Paper is not reset), sends the
command characters, and reads until a line containing END (default "done") or the timeout.

Usage:  conda run -n taskpal python tools/spike_cmd.py COM3 t [--timeout 300] [--until "SP-01 done"]
        conda run -n taskpal python tools/spike_cmd.py COM3 "T 07:00\n" --until "time set"   (app commands)
"""
import argparse
import sys
import time

import serial


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("port")
    ap.add_argument("command")
    ap.add_argument("--timeout", type=float, default=300)
    ap.add_argument("--until", default="done")
    args = ap.parse_args()

    s = serial.Serial()
    s.port, s.baudrate, s.timeout = args.port, 115200, 0.2
    s.dtr = False
    s.rts = False
    s.open()
    s.reset_input_buffer()
    # "\n" in the argument means newline (conda run cannot pass real newlines on Windows).
    s.write(args.command.replace("\\n", "\n").encode())
    buf, end = "", time.time() + args.timeout
    while time.time() < end:
        chunk = s.read(4096).decode("utf-8", "replace")
        if not chunk:
            continue
        sys.stdout.write(chunk)
        sys.stdout.flush()
        buf += chunk
        if args.until in buf:
            break
    s.close()


if __name__ == "__main__":
    main()
