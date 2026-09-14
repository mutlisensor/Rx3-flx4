#!/bin/bash
# Start the right input helpers for the RX3 screen. Idempotent; run at start-up and from udev on hot-plug.
#   pointer : a touchscreen if present, otherwise a USB mouse  -> rx3-touch-bridge  (unit rx3-pointer)
#   keyboard: every real keyboard                              -> rx3-hotkeys.py    (unit rx3-hotkeys-eventN)
# Each runs as its own transient systemd unit: started from a udev helper they would otherwise die with the
# helper's cgroup, and the hotkeys must outlive "systemctl stop rx3" so F5 can start the player again.
. "$(dirname "$(readlink -f "$0")")/rx3-env.sh"
R=$RX3_ROOT; B=$RX3_BINDIR/rx3-touch-bridge
touch=""; mouse=""; keyboards=""
for ev in /dev/input/event*; do
  p=$(udevadm info -q property -n $ev 2>/dev/null); name=$(cat /sys/class/input/$(basename $ev)/device/name 2>/dev/null)
  echo "$p" | grep -q "ID_INPUT_TOUCHSCREEN=1" && [ -z "$touch" ] && touch=$ev
  echo "$p" | grep -q "ID_INPUT_MOUSE=1" && [ -z "$mouse" ] && mouse=$ev
  echo "$p" | grep -q "ID_INPUT_KEYBOARD=1" && ! echo "$name" | grep -qiE "hdmi|pwr|button" && keyboards="$keyboards $ev"
done

# --- keyboard hotkeys (root; independent of the player) ---
for ev in $keyboards; do
  unit=rx3-hotkeys-$(basename $ev)
  systemctl is-active -q $unit.service && continue
  systemd-run --quiet --unit=$unit --collect -p WorkingDirectory=$RX3_HOME python3 $RX3_HOME/rx3-hotkeys.py $ev \
    && logger -t rx3 "hotkeys watching $ev ($(cat /sys/class/input/$(basename $ev)/device/name))"
done

# --- pointer bridge (only while the player runs) ---
pgrep -x rbp-pi >/dev/null || exit 0
if [ -n "$touch" ]; then mode=""; dev=$touch; elif [ -n "$mouse" ]; then mode="--mouse"; dev=$mouse; else exit 0; fi
cur=$(pgrep -a -f "^$B" | head -1)
case "$cur" in *"$dev"*) exit 0;; esac          # already bridging this device
systemctl stop rx3-pointer.service 2>/dev/null; pkill -f "^$B"; sleep 0.3
systemd-run --quiet --unit=rx3-pointer --collect -p User=$RX3_USER -p StandardError=append:$RX3_USERHOME/rx3-touch.log \
  -E RX3_FB="$RX3_FB" -E RX3_ROTATE="$RX3_ROTATE" $B $mode $dev $R/dev/tsc2007_2-0048
logger -t rx3 "pointer bridge started: $mode $dev"
