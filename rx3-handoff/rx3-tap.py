#!/usr/bin/env python3
"""Inject a tap at native RX3 screen coordinates (1280x800) through the touch bridge's replay mode.
usage: rx3-tap.py x y [hold_seconds]   (replay mode takes firmware coordinates directly)"""
import rx3_env
import subprocess, struct, time, sys
x, y = int(sys.argv[1]), int(sys.argv[2]); hold = float(sys.argv[3]) if len(sys.argv) > 3 else 0.25
cx, cy = x, y
p = subprocess.Popen([rx3_env.BINDIR + '/rx3-touch-bridge', '--replay', rx3_env.ROOT + '/dev/tsc2007_2-0048'], stdin=subprocess.PIPE, stderr=subprocess.DEVNULL)
def ev(t, c, v): p.stdin.write(struct.pack('llHHi', 0, 0, t, c, v)); p.stdin.flush()
ev(3, 47, 0); ev(3, 57, 100); ev(3, 53, cx); ev(3, 54, cy); ev(0, 0, 0)
time.sleep(hold)
ev(3, 57, -1); ev(0, 0, 0); time.sleep(0.3)
p.stdin.close(); p.wait()
