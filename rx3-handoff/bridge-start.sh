#!/bin/bash
# Start the controller MIDI bridge as its own unit (idempotent). Called by rx3-start.sh, and by
# controller-hotplug.sh when the controller is plugged back in: the bridge exits when its MIDI device disappears,
# while the player itself keeps running and reconnects its audio (fbshim.c).
. "$(dirname "$(readlink -f "$0")")/rx3-env.sh"
systemctl is-active -q rx3-bridge.service && exit 0
systemctl reset-failed rx3-bridge.service 2>/dev/null
exec systemd-run --quiet --unit=rx3-bridge --collect -p User="$RX3_USER" -p WorkingDirectory="$RX3_HOME" \
  -p StandardOutput=append:"$RX3_LOGDIR/rx3-controller.log" -p StandardError=append:"$RX3_LOGDIR/rx3-controller.log" \
  -E RX3_BRIDGE_LOG=1 -E RX3_ROOT="$RX3_ROOT" /usr/bin/python3 "$RX3_HOME/controller-bridge.py"
