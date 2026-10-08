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
# /dev/gpiodrv: the firmware's GPIO lines. It writes their states to this file many times a second, at ever larger
# offsets (megabytes a day), so keep it in RAM instead of on the SD card. Inputs read as 1 (not pressed).
if ! mountpoint -q $R/dev/gpiodrv; then
  python3 -c "open('/run/rx3-gpiodrv','wb').write(bytes([1])*4096)" && chown $RX3_UID:$RX3_GID /run/rx3-gpiodrv && chmod 664 /run/rx3-gpiodrv
  [ -f $R/dev/gpiodrv ] || : > $R/dev/gpiodrv
  mount --bind /run/rx3-gpiodrv $R/dev/gpiodrv
fi
# A real procfs for the player shim's own use (control-shim.c reads its memory map to find the firmware's LED state).
# The chroot's /proc stays the firmware's fake one (mounts table, udev FIFOs).
mkdir -p $R/hostproc; mountpoint -q $R/hostproc || mount -t proc proc $R/hostproc
mountpoint -q $R/tmp || mount -t tmpfs -o size=256m,mode=1777,uid=$RX3_UID,gid=$RX3_UID tmpfs $R/tmp
echo "1.19" > $R/tmp/smdj.rev; echo "1.19 [1.19:1.19]" > $R/tmp/smdj2.rev; chown $RX3_UID:$RX3_UID $R/tmp/*.rev
echo mounts-ok
