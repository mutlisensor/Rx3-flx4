#!/bin/bash
# udev helper: a known DJ controller's sound card appeared.
#  - The player is running on this same card (it was unplugged and plugged back in): the player keeps its decks and
#    reconnects its audio by itself (fbshim.c), so only the MIDI bridge needs starting again.
#  - Otherwise (the player started without a controller, or on a different one): restart the player onto it.
sleep 3
. "$(dirname "$(readlink -f "$0")")/rx3-env.sh"
systemctl is-active --quiet rx3 || exit 0
det=$(python3 "$RX3_HOME/controllers.py" detect 2>/dev/null) || exit 0
card=${det##*alsa=}; name=$(echo "$det" | sed 's/.*name=\([^ ]*\).*/\1/')
if grep -qx "hw:CARD=$card" "$RX3_ROOT/etc/rx3-ctl" 2>/dev/null && pgrep -x rbp-pi >/dev/null; then
  logger -t rx3 "$name reconnected; restarting its MIDI bridge (the player keeps running)"
  echo "== $(date '+%F %T') $name reconnected" >> "$RX3_LOGDIR/rx3-controller.log"
  exec "$RX3_HOME/bridge-start.sh"
fi
logger -t rx3 "$name appeared; restarting rx3 onto it"
systemctl restart rx3
