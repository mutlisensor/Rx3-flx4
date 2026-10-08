#!/bin/bash
# Restart the RX3 player when it can no longer recover by itself (started by rx3-start.sh as unit rx3-watchdog;
# rx3-stop.sh stops it first, so a deliberate stop or restart never triggers it):
#  - the player process has exited (a crash; rx3.service does not notice, it only tracks the start script);
#  - the player shim reports that the firmware's audio thread stopped writing (seen once: a crash loop in its
#    beat-sync code, after which there is no sound until a restart).
. "$(dirname "$(readlink -f "$0")")/rx3-env.sh"
LOG=$RX3_LOGDIR/rx3-player.log
sleep 60                                            # let start-up finish
seen=$(grep -c "audio thread has stopped writing" $LOG 2>/dev/null)
while sleep 5; do
  systemctl is-active -q rx3 || exit 0
  why=""
  pgrep -x rbp-pi >/dev/null || why="the player exited"
  now=$(grep -c "audio thread has stopped writing" $LOG 2>/dev/null)
  if [ -z "$why" ] && [ "${now:-0}" -gt "${seen:-0}" ]; then
    seen=$now; sleep 5
    tail -1 $LOG | grep -q "writing again" || why="its audio stopped"
  fi
  if [ -n "$why" ]; then
    logger -t rx3 "watchdog: $why; restarting the player"
    echo "$(date '+%F %T') watchdog: $why; restarting the player" >> $RX3_LOGDIR/rx3-watchdog.log
    systemctl restart rx3 --no-block; exit 0
  fi
done
