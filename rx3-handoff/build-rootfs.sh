#!/bin/bash
# Assemble the isolated RX3 chroot from the recovered firmware. Run as your normal user (no root needed).
set -euo pipefail
. "$(dirname "$(readlink -f "$0")")/rx3-env.sh"
H=$RX3_HOME
X=$H/extracted
R=$RX3_ROOT
# Preflight: the firmware must already be recovered, or the chroot ends up an empty directory.
for need in "$X/runtime-files" "$X/player/pdj/rbp" "$H/runtime-symlinks.json"; do
  [ -e "$need" ] && continue
  echo "ERROR: missing $need" >&2
  echo "Run these first, in this order, from $H:" >&2
  echo "  python3 recover-firmware.py     # downloads + decrypts the official firmware" >&2
  echo "  python3 extract_cramfs.py       # produces extracted/runtime-files and runtime-symlinks.json" >&2
  exit 1
done
command -v arm-linux-gnueabi-gcc >/dev/null || { echo "ERROR: arm-linux-gnueabi-gcc not installed (apt install gcc-arm-linux-gnueabi)" >&2; exit 1; }

echo "== base runtime files"
mkdir -p $R
rsync -a --delete $X/runtime-files/ $R/
echo "== symlinks"
python3 - "$H/runtime-symlinks.json" "$R" <<'PY'
import json,os,sys
links=json.load(open(sys.argv[1]));root=sys.argv[2];n=0
for path,target in links.items():
    p=os.path.join(root,path)
    os.makedirs(os.path.dirname(p),exist_ok=True)
    if os.path.islink(p) or os.path.exists(p): os.remove(p)
    os.symlink(target,p);n+=1
print("created",n,"symlinks")
PY
echo "== player + gui + settings"
mkdir -p $R/root/pdj $R/root/gui $R/root/settings $R/root/rbp $R/tmp $R/var/tmp $R/media/usb1 $R/media/usb2 $R/media/usb3 $R/media/usb4 $R/mnt
rsync -a $X/player/pdj/ $R/root/pdj/
rsync -a $X/gui/ $R/root/gui/
echo "== patched player"
mkdir -p $H/pi-runtime
cp -f $X/player/pdj/rbp $H/pi-runtime/rbp
cd $H && python3 patch-player.py && cp -f rbp-pi $R/root/pdj/rbp-pi && chmod 755 $R/root/pdj/rbp-pi
echo "== shim"
cd $H && arm-linux-gnueabi-gcc -march=armv7-a -shared -fPIC -O2 -fomit-frame-pointer -fno-builtin -nostdlib -o $R/lib/fbshim.so fbshim.c control-shim.c
echo "== disable vendor GPU driver (software DirectFB)"
mkdir -p $R/usr/lib/disabled
[ -e $R/usr/lib/directfb-1.4-0/gfxdrivers/libdirectfb_gal.so ] && mv $R/usr/lib/directfb-1.4-0/gfxdrivers/libdirectfb_gal.so $R/usr/lib/disabled/ || true
echo "== emulated devices"
D=$R/dev
rm -rf $D; mkdir -p $D
for f in null zero urandom random full; do touch $D/$f; done   # bind-mount targets
mkdir -p $D/snd $D/shm $D/pts
python3 -c "open('$D/fb0','wb').write(bytes(1280*800*4))"
python3 -c "open('$D/gpiodrv','wb').write(bytes([1])*4096)"
for f in tsc2007_2-0048 rx3-control subucom_spi1.0 subucom_spi2.0 subucom_spi_rdy3.0 subucom_spi_rdy4.0; do mkfifo -m 666 $D/$f; done
python3 -c "import struct;open('$D/rx3-ui-state','wb').write(struct.pack('<I6fII',0x52583332,1,.6,0,1,.5,.5,0,1))"
echo "== fake proc/sys"
P=$R/proc; rm -rf $P; mkdir -p $P/jog $P/self
for f in udev_usb1 udev_usb2 udev_usbctn1 udev_usbctn2; do mkfifo -m 666 $P/$f; done
for f in pulse.0 pulse.1 result.0 result.1 slow.0 slow.1 start; do echo 0 > $P/jog/$f; done
cat > $P/cpuinfo <<'CPU'
Processor	: ARMv7 Processor rev 10 (v7l)
processor	: 0
BogoMIPS	: 1581.05

processor	: 1
BogoMIPS	: 1581.05

Features	: swp half thumb fastmult vfp edsp neon vfpv3 
CPU implementer	: 0x41
CPU architecture: 7
CPU variant	: 0x2
CPU part	: 0xc09
CPU revision	: 10

Hardware	: Freescale i.MX 6Quad/DualLite (Device Tree)
Revision	: 63012
Serial		: 0000000000000000
CPU
cat > $P/mounts <<'M'
rootfs / rootfs rw 0 0
proc /proc proc rw,relatime 0 0
sysfs /sys sysfs rw,relatime 0 0
tmpfs /tmp tmpfs rw,relatime 0 0
ubi6:settings /root/settings ubifs rw,relatime 0 0
ubi10:gui /root/gui ubifs ro,relatime 0 0
M
ln -sf /proc/mounts $R/etc/mtab
S=$R/sys; rm -rf $S; mkdir -p $S/class/paudiog/paudiog0 $S/devices/platform/pwm-backlight.1/backlight/pwm-backlight.1
echo 0 > $S/class/paudiog/paudiog0/connect
echo 255 > $S/devices/platform/pwm-backlight.1/backlight/pwm-backlight.1/brightness
echo 255 > $S/devices/platform/pwm-backlight.1/backlight/pwm-backlight.1/max_brightness
echo "== version files"
echo "1.19" > $R/tmp/smdj.rev; echo "1.19 [1.19:1.19]" > $R/tmp/smdj2.rev
echo "== asound.conf"
cp -f $H/asound.conf $R/etc/asound.conf
echo "== directfbrc"
cat > $R/etc/directfbrc <<'DFB'
system=fbdev
fbdev=/dev/fb0
no-vt
no-vt-switch
no-cursor
no-sighandler
no-deinit-check
mode=1280x800
depth=32
pixelformat=ARGB
DFB
echo "== done"; du -sh $R
