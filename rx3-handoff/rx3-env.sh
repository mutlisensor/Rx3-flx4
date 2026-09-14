# Resolve the RX3 install layout. Sourced by every script in this directory:
#   . "$(dirname "$(readlink -f "$0")")/rx3-env.sh"
#
# Nothing here is hardcoded to a particular username. RX3_HOME is wherever this
# file lives; the account that owns it is the account the player runs as, and
# that account's home directory holds the chroot, the USB overlays and the logs.
# Every value can be overridden by exporting it before the script runs.
RX3_HOME="${RX3_HOME:-$(cd "$(dirname "${BASH_SOURCE[0]:-$0}")" && pwd)}"
RX3_USER="${RX3_USER:-$(stat -c %U "$RX3_HOME" 2>/dev/null)}"
: "${RX3_USER:=$(id -un)}"
RX3_USERHOME="${RX3_USERHOME:-$(getent passwd "$RX3_USER" 2>/dev/null | cut -d: -f6)}"
: "${RX3_USERHOME:=$(dirname "$RX3_HOME")}"
RX3_ROOT="${RX3_ROOT:-$RX3_USERHOME/rx3-rootfs}"     # the ARM32 chroot
RX3_USB="${RX3_USB:-$RX3_USERHOME/rx3-usb}"          # USB copy-on-write layers
RX3_LOGDIR="${RX3_LOGDIR:-$RX3_USERHOME}"            # rx3-*.log
RX3_BINDIR="${RX3_BINDIR:-$RX3_USERHOME}"            # rx3-fb-present, rx3-touch-bridge
# Numeric identity of that account, and the groups the firmware needs inside the chroot
# (audio for ALSA, video for the framebuffer, input for the event devices). Resolved by name,
# because the numbers differ between distributions and between machines.
RX3_UID="${RX3_UID:-$(id -u "$RX3_USER" 2>/dev/null || echo 1000)}"
RX3_GID="${RX3_GID:-$( getent group video 2>/dev/null | cut -d: -f3)}"; : "${RX3_GID:=44}"
rx3_gid_of(){  getent group "$1" 2>/dev/null | cut -d: -f3; }
RX3_GROUPS="${RX3_GROUPS:-$(printf '%s,%s,%s' "$(rx3_gid_of audio)" "$(rx3_gid_of video)" "$(rx3_gid_of input)")}"
# Optional per-machine settings (RX3_FB, RX3_ROTATE, RX3_FONT ...): see INSTALL.md "Display".
[ -f "$RX3_HOME/rx3.conf" ] && . "$RX3_HOME/rx3.conf"
# Which framebuffer to draw on. A DSI panel such as the Raspberry Pi Touch Display 2 is its own DRM device
# and gets its own /dev/fbN alongside HDMI, so prefer it when present; otherwise the first framebuffer.
rx3_pick_fb(){
  for f in /sys/class/graphics/fb[0-9]*; do
    case "$(cat "$f/name" 2>/dev/null)" in *dsi*) echo "/dev/$(basename "$f")"; return;; esac
  done
  for f in /dev/fb[0-9]*; do [ -e "$f" ] && { echo "$f"; return; }; done
}
RX3_FB="${RX3_FB:-$(rx3_pick_fb)}"
RX3_ROTATE="${RX3_ROTATE:-}"          # 0/90/180/270 clockwise; empty = portrait panels 90, landscape 0
RX3_FPS="${RX3_FPS:-}"                # presenter rate; empty = 60, synced to the panel's vertical blank
RX3_FILTER="${RX3_FILTER:-}"          # "nearest" to trade picture quality for CPU on slow boards
export RX3_FB RX3_ROTATE RX3_FPS RX3_FILTER

# A clone made with sudo leaves this directory owned by root, which would put the chroot somewhere
# like /root/rx3-rootfs. Judge that by the account's home directory rather than by uid, because the
# first-user uid differs between systems (1000 on Debian, 501 on macOS).
rx3_is_system_home(){ case "$1" in ''|/|/root|/nonexistent|/usr/sbin|/bin|/dev/null|/var/*|/run/*) return 0;; *) return 1;; esac; }
if { [ "${RX3_UID:-1000}" = 0 ] || rx3_is_system_home "$RX3_USERHOME"; } && [ -z "${RX3_ALLOW_SYSTEM_USER:-}" ]; then
  echo "WARNING: $RX3_HOME is owned by '$RX3_USER', whose home is $RX3_USERHOME," >&2
  echo "         so the chroot would be built at $RX3_ROOT." >&2
  echo "         Fix with:  sudo chown -R \$(id -un):\$(id -gn) \"$RX3_HOME\"" >&2
  echo "         Or set RX3_ALLOW_SYSTEM_USER=1 if this is deliberate." >&2
fi

export RX3_HOME RX3_USER RX3_USERHOME RX3_ROOT RX3_USB RX3_LOGDIR RX3_BINDIR RX3_UID RX3_GID RX3_GROUPS
