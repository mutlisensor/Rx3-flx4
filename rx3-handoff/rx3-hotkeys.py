#!/usr/bin/env python3
"""Debug hotkeys on a USB keyboard, independent of the firmware (which never sees the keyboard).
Useful when the player hangs and there is no SSH to hand.

  ESC held 1 s   stop the player (systemctl stop rx3): the screen drops to the text console
  F5             restart the player
  F12            write a diagnostic snapshot to ~/rx3-diag-<time>.txt (journal, processes, mounts, cards)

usage: rx3-hotkeys.py /dev/input/eventN   (started by input-hotplug.sh for every keyboard; runs as root)"""
import os, struct, sys, time, subprocess, select
import rx3_env

EV_KEY, KEY_ESC, KEY_F5, KEY_F12 = 1, 1, 63, 88
FMT = 'llHHi'; SZ = struct.calcsize(FMT)
HOLD = 1.0
dev = sys.argv[1]

def log(msg):
    subprocess.run(['logger', '-t', 'rx3', 'hotkeys: ' + msg])

def diag():
    path = os.path.join(rx3_env.USERHOME, time.strftime('rx3-diag-%Y%m%d-%H%M%S.txt'))
    cmds = [['uptime'], ['systemctl', 'status', 'rx3', '--no-pager'], ['journalctl', '-u', 'rx3', '-t', 'rx3', '-b', '--no-pager', '-n', '200'],
            ['ps', '-eo', 'pid,ppid,pcpu,pmem,stat,etime,comm,args', '--sort=-pcpu'], ['findmnt', '-n', '-o', 'TARGET,SOURCE,FSTYPE'],
            ['cat', '/proc/asound/cards'], ['lsusb'], ['vcgencmd', 'get_throttled'], ['free', '-m'], ['dmesg', '--time-format', 'ctime']]
    with open(path, 'w') as f:
        for c in cmds:
            f.write('\n===== ' + ' '.join(c) + '\n')
            try: f.write(subprocess.run(c, capture_output=True, text=True, timeout=20).stdout)
            except Exception as e: f.write('(%s)\n' % e)
        for name in ('rx3-player.log', 'rx3-flx4.log', 'rx3-present.log', 'rx3-touch.log'):
            p = os.path.join(rx3_env.USERHOME, name)
            if os.path.exists(p):
                f.write('\n===== tail %s\n' % name)
                with open(p, errors='replace') as src: f.write(''.join(src.readlines()[-100:]))
    os.chown(path, rx3_env.UID if hasattr(rx3_env, 'UID') else os.stat(rx3_env.HOME).st_uid, -1)
    log('diagnostic snapshot written to ' + path)

fd = os.open(dev, os.O_RDONLY)
log('watching %s (ESC hold 1 s = stop, F5 = restart, F12 = diagnostics)' % dev)
esc_down = None; fired = False
while True:
    r, _, _ = select.select([fd], [], [], 0.1)
    if r:
        data = os.read(fd, SZ)
        if len(data) != SZ: break                       # keyboard unplugged
        _, _, etype, code, value = struct.unpack(FMT, data)
        if etype != EV_KEY: continue
        if code == KEY_ESC:
            if value == 1: esc_down = time.monotonic(); fired = False
            elif value == 0: esc_down = None
        elif value == 1 and code == KEY_F5:
            log('F5: restarting the player'); subprocess.Popen(['systemctl', 'restart', 'rx3'])
        elif value == 1 and code == KEY_F12:
            diag()
    if esc_down and not fired and time.monotonic() - esc_down >= HOLD:
        fired = True; log('ESC held: stopping the player'); subprocess.Popen(['systemctl', 'stop', 'rx3'])
