#!/bin/bash
# Launch the XDJ-RX3 player (firmware 1.19) inside its chroot on this Raspberry Pi 5. Runs as root.
# Layout is resolved by rx3-env.sh: chroot $RX3_ROOT, tools $RX3_HOME, logs $RX3_LOGDIR/rx3-*.log
set -u
. "$(dirname "$(readlink -f "$0")")/rx3-env.sh"
R=$RX3_ROOT; H=$RX3_HOME; U=$RX3_USER
LOG=$RX3_LOGDIR/rx3-player.log
if pgrep -x rbp-pi >/dev/null; then echo "RX3 player already running"; exit 0; fi

# --- host preparation ---------------------------------------------------------
$H/mount-rx3.sh >/dev/null
# Root helper that performs the firmware's own USB STOP unmounts (see rx3-priv.sh); its FIFO must exist before launch.
systemctl is-active -q rx3-priv.service || systemd-run --quiet --unit=rx3-priv --collect -p Restart=on-failure $H/rx3-priv.sh
for i in $(seq 1 20); do [ -p $R/dev/rx3-priv ] && break; sleep 0.1; done
# Keep the kernel default RT throttle (95%) so runaway SCHED_RR firmware threads cannot starve Wi-Fi/USB work.
sysctl -q -w kernel.sched_rt_runtime_us=950000
touch $R/dev/printkdrv0; mountpoint -q $R/dev/printkdrv0 || mount --bind /dev/null $R/dev/printkdrv0

# --- audio card: the first connected known DJ controller (controllers.py), else the ALSA loopback ---------
CARD=""; CONTROLLER=""
for wait in $(seq 1 30); do                       # the controller can enumerate a few seconds after boot
  det=$(python3 $H/controllers.py detect 2>/dev/null) && { CARD=${det##*alsa=}; CONTROLLER=$(echo "$det" | sed 's/.*name=\([^ ]*\).*/\1/'); }
  [ -n "$CARD" ] && break
  # A bus-powered controller that was attached while the Pi powered up often comes up wedged: lit, but never
  # signalling on USB, and nothing short of removing its power revives it. The Pi 5 has no per-port power
  # switching (uhubctl "off" only disables the port; the device stays lit), but the RP1 has one USB_VBUS_EN
  # line for all ports, so cut that for a few seconds. Every USB device re-enumerates afterwards, which is
  # why this only runs before any media is attached, and at most twice.
  if { [ $wait = 10 ] || [ $wait = 22 ]; } && ! findmnt -rn -o TARGET | grep -q "^$R/media/"; then
    chip=$(gpioinfo 2>/dev/null | awk '/^gpiochip/{c=$1} /USB_VBUS_EN/{print c}' | head -1)
    if [ -n "$chip" ] && command -v gpioset >/dev/null; then
      logger -t rx3 "no DJ controller after ${wait}s: cutting USB power (USB_VBUS_EN on $chip) for 5 s"
      timeout 5 gpioset -c "$chip" USB_VBUS_EN=0; timeout 1 gpioset -c "$chip" USB_VBUS_EN=1
    elif command -v uhubctl >/dev/null; then
      # Other boards: uhubctl can switch real power on some hubs (Pi 4 root hub, powered hubs).
      for hub in $(uhubctl 2>/dev/null | sed -n 's/^Current status for hub \([^ ]*\).*/\1/p'); do
        logger -t rx3 "no DJ controller after ${wait}s: power-cycling hub $hub"
        uhubctl -l "$hub" -a cycle -d 5 >/dev/null 2>&1
      done
    fi
  fi
  sleep 1
done
if [ -z "$CARD" ]; then
  lsmod | grep -q snd_aloop || modprobe snd_aloop pcm_substreams=1
  CARD=Loopback
fi
echo "hw:CARD=$CARD" > $R/etc/rx3-ctl
sed "s/hw:2,0/hw:CARD=$CARD,DEV=0/" $H/asound.conf > $R/etc/asound.conf
echo "audio card: $CARD${CONTROLLER:+ ($CONTROLLER)}"

# --- reset interim UI state, then start the firmware -------------------------------------------
python3 - <<'PY'
import os,struct
with os.fdopen(os.open(os.environ['RX3_ROOT']+'/dev/rx3-ui-state',os.O_RDWR|os.O_CREAT,0o600),'r+b') as f:
    f.write(struct.pack('<I6fII',0x52583332,1,.6,0,1,.5,.5,0,1))
PY
chown $U:$U $R/dev/rx3-ui-state
ulimit -r 99; ulimit -l unlimited; ulimit -c 0
cd $RX3_USERHOME
nohup chroot --userspec=$RX3_UID:$RX3_GID --groups=$RX3_GROUPS $R /bin/busybox sh -c \
  "cd /root/pdj && exec env LD_PRELOAD=/lib/fbshim.so /root/pdj/rbp-pi -a" > $LOG 2>&1 < /dev/null &
echo "player started (pid $!)"

# --- host-side helpers: controller MIDI bridge, display presenter, touch bridge ------------------
sleep 4
if [ -n "$CONTROLLER" ]; then
  pgrep -f "^python3 $RX3_HOME/controller-bridge" >/dev/null || nohup sudo -u $U env RX3_BRIDGE_LOG=1 python3 $H/controller-bridge.py > $RX3_USERHOME/rx3-controller.log 2>&1 < /dev/null &
fi
# A framebuffer only exists for a display that was connected at boot: with nothing plugged in, the kernel
# finds no CRTC and creates none, so there is nothing for the presenter to draw on. rx3-env.sh picks the
# DSI touch panel when there is one, else the first framebuffer (RX3_FB overrides).
if [ -z "$RX3_FB" ] || [ ! -e "$RX3_FB" ]; then
  echo "no framebuffer: the player is running but nothing can be shown."
  echo "  Connect a display (HDMI or the DSI touch panel) and reboot: framebuffers are created at boot, not on hotplug."
  for c in /sys/class/drm/card*/card*-*; do
    [ -e "$c/status" ] && echo "  $(basename "$c"): $(cat "$c/status")"
  done
elif [ ! -x $RX3_BINDIR/rx3-fb-present ]; then
  echo "rx3-fb-present is missing from $RX3_BINDIR: run ./install.sh to build it."
else
  echo "display: $RX3_FB ($(cat /sys/class/graphics/$(basename $RX3_FB)/name 2>/dev/null) $(cat /sys/class/graphics/$(basename $RX3_FB)/virtual_size 2>/dev/null))${RX3_ROTATE:+ rotate=$RX3_ROTATE}"
  pgrep -x rx3-fb-present >/dev/null || nohup sudo -u $U env RX3_FB="$RX3_FB" RX3_ROTATE="$RX3_ROTATE" RX3_FONT="${RX3_FONT:-}" $RX3_BINDIR/rx3-fb-present $R/dev/fb0 > $RX3_USERHOME/rx3-present.log 2>&1 < /dev/null &
fi
$H/input-hotplug.sh     # touchscreen if present, else USB mouse

# --- USB media: attach the first removable partition via copy-on-write overlay and notify -------
( sleep 8; for dev in /dev/sd?1; do [ -b "$dev" ] && $H/usb-hotplug.sh add "$dev"; done ) > $RX3_USB.log 2>&1 < /dev/null &
exit 0
