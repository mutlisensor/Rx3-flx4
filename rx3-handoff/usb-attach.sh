#!/bin/bash
# Present a block device or image to the RX3 player as USB1/USB2 via a copy-on-write overlay (runs as root).
# usage: usb-attach.sh <device-or-image> [usb1|usb2]
# The mountpoint uses the real partition name (/media/usb1/sda1, /media/usb2/sdb1 ...) and a matching block node is
# created in the chroot's /dev, because the firmware probes /dev/<basename of mount path> with libblkid.
set -e
. "$(dirname "$(readlink -f "$0")")/rx3-env.sh"
SRC=$1; PORT=${2:-usb1}; R=$RX3_ROOT; H=$RX3_HOME
PART=sda1; [ -b "$SRC" ] && PART=$(basename "$SRC")
MP=$R/media/$PORT/$PART
# Upper/work layers are keyed by the filesystem UUID so one device's firmware writes never shadow another's contents.
KEY=$(blkid -s UUID -o value "$SRC" 2>/dev/null); KEY=${KEY:-$(basename "$SRC")}
LOWER=$RX3_USB/$PORT/lower; UPPER=$RX3_USB/$PORT/cow-$KEY/upper; WORK=$RX3_USB/$PORT/cow-$KEY/work
mkdir -p $LOWER $UPPER $WORK $MP
for old in $R/media/$PORT/*; do mountpoint -q "$old" && umount -l "$old"; done
mountpoint -q $LOWER && umount -l $LOWER
OPT="ro"; [ -f "$SRC" ] && OPT="ro,loop"
# FAT stores long names in UTF-16; without iocharset=utf8 the kernel shows anything non-ASCII as "?", and the
# firmware (which asks for UTF-8 paths) reports E-8306 NO FILE for tracks such as "Beto’s Horns".
[ "$(blkid -s TYPE -o value "$SRC" 2>/dev/null)" = vfat ] && OPT="$OPT,iocharset=utf8"
mount -o $OPT,uid=$RX3_UID,gid=$RX3_UID "$SRC" $LOWER 2>/dev/null || mount -o $OPT "$SRC" $LOWER
chown $RX3_UID:$RX3_UID $UPPER $WORK
# Kernel overlayfs rejects case-insensitive (FAT) lower layers, so use fuse-overlayfs for the copy-on-write view.
# fuse-overlayfs daemonises into the caller's cgroup, and systemd tears that cgroup down when a udev-spawned helper
# exits (the overlay vanished 0.1 s after every hot-plug attach), so run it as its own transient unit instead.
UNIT=rx3-overlay-$PORT
systemctl stop $UNIT.service 2>/dev/null || true; systemctl reset-failed $UNIT.service 2>/dev/null || true
systemd-run --quiet --unit=$UNIT --collect -- fuse-overlayfs -f -o lowerdir=$LOWER,upperdir=$UPPER,workdir=$WORK,allow_other,squash_to_uid=$RX3_UID,squash_to_gid=$RX3_UID $MP
for i in $(seq 1 50); do mountpoint -q $MP && break; sleep 0.1; done
mountpoint -q $MP || { echo "overlay for $PORT failed:"; journalctl -u $UNIT --no-pager | tail -5; exit 1; }
# Read-only block node for the firmware's libblkid probe (volume label / fs type); group 44 = the player's gid.
if [ -b "$SRC" ]; then rm -f $R/dev/$PART; mknod $R/dev/$PART b 0x$(stat -c %t "$SRC") 0x$(stat -c %T "$SRC"); chown root:44 $R/dev/$PART; chmod 640 $R/dev/$PART; fi
$H/rx3-mtab.sh
echo "overlay mounted at $MP (lower=$SRC ro, upper=$UPPER, unit $UNIT)"
