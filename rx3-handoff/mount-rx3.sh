#!/bin/bash
# Prepare host bind mounts for the RX3 chroot. Runs as root. Idempotent.
set -u
. "$(dirname "$(readlink -f "$0")")/rx3-env.sh"
R=$RX3_ROOT
for f in null zero urandom random full; do mountpoint -q $R/dev/$f || mount --bind /dev/$f $R/dev/$f; done
mountpoint -q $R/dev/snd || mount --bind /dev/snd $R/dev/snd
mountpoint -q $R/dev/shm || mount -t tmpfs -o size=64m,mode=1777 tmpfs $R/dev/shm
# A real procfs for the player shim's own use (control-shim.c reads its memory map to find the firmware's LED state).
# The chroot's /proc stays the firmware's fake one (mounts table, udev FIFOs).
mkdir -p $R/hostproc; mountpoint -q $R/hostproc || mount -t proc proc $R/hostproc
mountpoint -q $R/tmp || mount -t tmpfs -o size=256m,mode=1777,uid=$RX3_UID,gid=$RX3_UID tmpfs $R/tmp
echo "1.19" > $R/tmp/smdj.rev; echo "1.19 [1.19:1.19]" > $R/tmp/smdj2.rev; chown $RX3_UID:$RX3_UID $R/tmp/*.rev
echo mounts-ok
