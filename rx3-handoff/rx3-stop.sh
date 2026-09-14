#!/bin/bash
# Stop the RX3 player and its helpers, then release overlays/binds so shutdown never waits on them.
. "$(dirname "$(readlink -f "$0")")/rx3-env.sh"
R=$RX3_ROOT
pkill -x rbp-pi; pkill -f "^python3 $RX3_HOME/controller-bridge"; pkill -x rx3-fb-present; pkill -f "^$RX3_BINDIR/rx3-touch-bridge"
for i in 1 2 3 4 5; do pgrep -x rbp-pi >/dev/null || break; sleep 1; done
pgrep -x rbp-pi >/dev/null && pkill -9 -x rbp-pi
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
