#!/bin/bash
# Prepare host bind mounts for the RX3 chroot. Runs as root. Idempotent.
set -u
. "$(dirname "$(readlink -f "$0")")/rx3-env.sh"
R=$RX3_ROOT
for f in null zero urandom random full; do mountpoint -q $R/dev/$f || mount --bind /dev/$f $R/dev/$f; done
mountpoint -q $R/dev/snd || mount --bind /dev/snd $R/dev/snd
mountpoint -q $R/dev/shm || mount -t tmpfs -o size=64m,mode=1777 tmpfs $R/dev/shm
# The firmware's screen is $R/dev/fb0, a plain file it mmaps (fbshim.c). Keep it in RAM: on the SD card its dirty
# pages were written back every 30 s, about 11 GB a day of wear for nothing. The file also carries the shim's
# completed-frame snapshot and sequence word for the presenter (fb-frame.h): two pictures plus one page.
if ! mountpoint -q $R/dev/fb0; then
  truncate -s 8196096 /run/rx3-fb0 && chown $RX3_UID:$RX3_GID /run/rx3-fb0 && chmod 660 /run/rx3-fb0
  [ -f $R/dev/fb0 ] || : > $R/dev/fb0
  mount --bind /run/rx3-fb0 $R/dev/fb0
fi
mountpoint -q $R/tmp || mount -t tmpfs -o size=256m,mode=1777,uid=$RX3_UID,gid=$RX3_UID tmpfs $R/tmp
echo "1.19" > $R/tmp/smdj.rev; echo "1.19 [1.19:1.19]" > $R/tmp/smdj2.rev; chown $RX3_UID:$RX3_UID $R/tmp/*.rev
echo mounts-ok
