#!/bin/bash
# udev helper: when the DDJ-FLX4 sound card appears and the player is not using it, restart the service onto it.
sleep 3
. "$(dirname "$(readlink -f "$0")")/rx3-env.sh"
systemctl is-active --quiet rx3 || exit 0
# Already on the FLX4 with the player and the MIDI bridge alive: nothing to do. If the controller was unplugged
# while in use, the bridge has exited and the firmware's ALSA handles are dead, so that case restarts too.
if grep -q FLX4 $RX3_ROOT/etc/rx3-ctl 2>/dev/null && pgrep -x rbp-pi >/dev/null && pgrep -f "^python3 $RX3_HOME/flx4-bridge" >/dev/null; then exit 0; fi
logger -t rx3 "DDJ-FLX4 appeared; restarting rx3 onto it"
systemctl restart rx3
