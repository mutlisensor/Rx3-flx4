#!/bin/bash
# Stop the RX3 player and its helpers, then release overlays/binds so shutdown never waits on them.
. "$(dirname "$(readlink -f "$0")")/rx3-env.sh"
R=$RX3_ROOT
# A restart (hot-plugged controller, F5, systemctl restart) keeps the last frame on screen until the new player
# draws; only a real stop blanks the panel and hands the screen back to the text console.
RESTART=0; systemctl list-jobs --no-legend 2>/dev/null | grep -q "rx3.service *restart" && RESTART=1
systemctl stop rx3-bridge.service 2>/dev/null
pkill -x rbp-pi; pkill -f "^/usr/bin/python3 $RX3_HOME/controller-bridge"; pkill -f "^python3 $RX3_HOME/controller-bridge"
if [ $RESTART = 1 ]; then pkill -KILL -x rx3-fb-present; else pkill -x rx3-fb-present; fi
pkill -f "^$RX3_BINDIR/rx3-touch-bridge"
for i in 1 2 3 4 5; do pgrep -x rbp-pi >/dev/null || break; sleep 1; done
pgrep -x rbp-pi >/dev/null && pkill -9 -x rbp-pi
# Hand the screen back to the text console. The presenter blanks the panel as it exits (fb-present.c); once it
# is gone, show tty1's cursor again (rx3-start.sh hid it) and repaint the console with a VT round trip.
for i in $(seq 1 20); do pgrep -x rx3-fb-present >/dev/null || break; sleep 0.1; done
if [ $RESTART = 0 ] && [ -w /dev/tty1 ]; then
  [ -w /sys/class/graphics/fbcon/cursor_blink ] && echo 1 > /sys/class/graphics/fbcon/cursor_blink 2>/dev/null
  printf '\033[?25h' > /dev/tty1 2>/dev/null
  command -v chvt >/dev/null && [ "$(fgconsole 2>/dev/null)" = 1 ] && { chvt 2; chvt 1; }
fi
systemctl stop rx3-priv.service rx3-pointer.service 2>/dev/null
for p in usb1 usb2; do
  for mp in $R/media/$p/*; do mountpoint -q "$mp" && umount -l "$mp"; done
  mountpoint -q $RX3_USB/$p/lower && umount -l $RX3_USB/$p/lower
done
rm -f $R/dev/sd??; $RX3_HOME/rx3-mtab.sh
for m in $R/tmp $R/dev/shm $R/dev/snd $R/dev/printkdrv0 $R/dev/null $R/dev/zero $R/dev/urandom $R/dev/random $R/dev/full; do
  mountpoint -q $m && umount -l $m
done
exit 0
