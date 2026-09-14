#!/bin/bash
# udev helper: a known DJ controller's sound card appeared. If the player is not already running on a controller
# with its MIDI bridge alive, restart the service so it moves onto it (from the loopback fallback, or after the
# controller was unplugged while in use, which kills the bridge and the firmware's ALSA handles).
sleep 3
. "$(dirname "$(readlink -f "$0")")/rx3-env.sh"
systemctl is-active --quiet rx3 || exit 0
det=$(python3 $RX3_HOME/controllers.py detect 2>/dev/null) || exit 0
card=${det##*alsa=}
if grep -q "CARD=$card" $RX3_ROOT/etc/rx3-ctl 2>/dev/null && pgrep -x rbp-pi >/dev/null && pgrep -f "^python3 $RX3_HOME/controller-bridge" >/dev/null; then exit 0; fi
logger -t rx3 "$(echo "$det" | sed 's/.*name=\([^ ]*\).*/\1/') appeared; restarting rx3 onto it"
systemctl restart rx3
