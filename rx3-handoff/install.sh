#!/bin/bash
# Install the host-side pieces of the RX3 player: helper binaries, udev rules, systemd unit.
# Run from the directory this file lives in, as a normal user (it will ask for sudo).
#   ./install.sh deps     install the Debian packages this needs
#   ./install.sh doctor   check prerequisites only, change nothing
#   ./install.sh          full install (binaries, udev rules, systemd unit)
#   ./install.sh desktop  stop the player and hand the Pi back to its desktop
#   ./install.sh strays   remove only the stray "$RX3_..." directories an older script version left behind
#   ./install.sh clean    remove everything a previous install left behind (keeps the recovered firmware)
#   ./install.sh clean --all   ... and the recovered firmware too
set -uo pipefail
. "$(dirname "$(readlink -f "$0")")/rx3-env.sh"
ok(){ printf '  \033[32mok\033[0m   %s\n' "$1"; }
bad(){ printf '  \033[31mMISS\033[0m %s\n' "$1"; FAIL=1; }
warn(){ printf '  \033[33mwarn\033[0m %s\n' "$1"; }
FAIL=0

# Directories literally named after a shell variable ($RX3_ROOT, $RX3_USERHOME, ...): scripts between 11 and 14 Sep
# 2026 expanded variables inside quoted Python heredocs, so Python created paths with the literal text, relative to
# whatever the working directory was (the filesystem root under systemd, the handoff dir when run by hand).
stray_dirs(){
  for base in / "$RX3_HOME" "$RX3_USERHOME" "$(pwd)"; do
    for name in '$RX3_ROOT' '$RX3_USERHOME' '$RX3_HOME' '$RX3_USB' '$RX3_BINDIR' '$RX3_LOGDIR' '$R' '$H' '$U'; do
      d="${base%/}/$name"; [ -e "$d" ] && echo "$d"
    done
  done | sort -u
}
remove_strays(){ stray_dirs | while IFS= read -r d; do sudo rm -rf -- "$d" && echo "  removed $d"; done; }

if [ "${1:-}" = strays ]; then
  found=$(stray_dirs)
  [ -z "$found" ] && { echo "No stray directories found (looked in /, $RX3_HOME, $RX3_USERHOME and $(pwd))."; exit 0; }
  echo "Directories literally named after a variable, left by an older script version:"; echo "$found" | sed 's/^/  /'
  printf "Remove them? [y/N] "; read -r a; [ "$a" = y ] || [ "$a" = Y ] || { echo "aborted"; exit 1; }
  sudo -v || exit 1; remove_strays; exit 0
fi

echo "RX3 layout"
echo "  tools   $RX3_HOME"
echo "  user    $RX3_USER"
echo "  chroot  $RX3_ROOT"
echo "  overlays $RX3_USB"
if [ -n "$RX3_FB" ] && [ -e "$RX3_FB" ]; then
  echo "  display $RX3_FB ($(cat /sys/class/graphics/$(basename $RX3_FB)/name 2>/dev/null), $(cat /sys/class/graphics/$(basename $RX3_FB)/virtual_size 2>/dev/null | tr , x) px)${RX3_ROTATE:+ rotate=$RX3_ROTATE}"
else
  echo "  display none yet (connect HDMI or the DSI touch panel and reboot)"
fi
echo

case "$RX3_USERHOME" in
  ''|/|/root|/nonexistent|/usr/sbin|/bin|/dev/null)
    [ -z "${RX3_ALLOW_SYSTEM_USER:-}" ] && bad "owned by '$RX3_USER', whose home is $RX3_USERHOME - run: sudo chown -R \$(id -un):\$(id -gn) \"$RX3_HOME\"";;
esac

det=$(python3 "$RX3_HOME/controllers.py" detect --all 2>/dev/null)
if [ -n "$det" ]; then echo "$det" | sed 's/^/  controller /'; else echo "  controller none connected (known: $(python3 "$RX3_HOME/controllers.py" names))"; fi
echo
echo "Prerequisites"
# Binary name -> apt package, because several differ (arm-linux-gnueabi-gcc lives in
# gcc-arm-linux-gnueabi, 7z in p7zip-full) and that trips people up.
pkg_for(){ case "$1" in
  fuse-overlayfs) echo fuse-overlayfs;;
  rsync) echo rsync;;
  gcc) echo build-essential;;
  arm-linux-gnueabi-gcc) echo gcc-arm-linux-gnueabi;;
  python3) echo python3;;
  uhubctl) echo uhubctl;;
  gpioset) echo gpiod;;
  7z) echo p7zip-full;;
  *) echo "$1";;
esac; }
# sbin is not on a normal user's PATH, so look there too before declaring something missing.
have(){ command -v "$1" >/dev/null || [ -x /usr/sbin/"$1" ] || [ -x /sbin/"$1" ]; }
MISSING=""
need(){ have "$1" && ok "$1" || { bad "$1 not installed (apt package: $(pkg_for "$1"))"; MISSING="$MISSING $(pkg_for "$1")"; }; }
optional(){ have "$1" && ok "$1" || { warn "$1 missing - $2 (apt package: $(pkg_for "$1"))"; MISSING="$MISSING $(pkg_for "$1")"; }; }

for p in fuse-overlayfs rsync gcc arm-linux-gnueabi-gcc python3; do need $p; done
optional uhubctl "only used to power-cycle a stuck controller on boards without USB_VBUS_EN"
optional gpioset "used to cut USB power when a bus-powered controller comes up dead at boot (Pi 5)"
if pkg-config --exists freetype2 2>/dev/null || [ -e /usr/include/freetype2/ft2build.h ]; then
  ok "freetype headers"
else
  bad "FreeType headers missing, rx3-fb-present will not build (apt package: libfreetype6-dev)"
  MISSING="$MISSING libfreetype6-dev pkg-config"
fi
if ls /usr/share/fonts/truetype/*/*.ttf >/dev/null 2>&1; then
  ok "a TrueType font is installed"
else
  bad "no TrueType font found, rx3-fb-present needs one to draw labels (apt package: fonts-dejavu-core)"
  MISSING="$MISSING fonts-dejavu-core"
fi
python3 -c "import PIL" 2>/dev/null && ok "python3 PIL" || { warn "python3-pil missing (screenshot helpers)"; MISSING="$MISSING python3-pil"; }
python3 -c "import cryptography" 2>/dev/null && ok "python3 cryptography" || { warn "python3-cryptography missing (needed by recover-firmware.py)"; MISSING="$MISSING python3-cryptography"; }
have 7z || have bsdtar || { warn "7z missing (needed by recover-firmware.py to unpack the ISO)"; MISSING="$MISSING p7zip-full"; }
echo

stray_dirs | while IFS= read -r d; do warn "stray directory $d (left by an older script; remove with ./install.sh strays)"; done
echo "Recovered firmware"
RF="$RX3_HOME/extracted/runtime-files"
NFILES=$( [ -d "$RF" ] && find "$RF" -type f 2>/dev/null | head -2000 | wc -l || echo 0 )
if [ ! -d "$RF" ]; then
  bad "extracted/runtime-files missing - run: python3 recover-firmware.py && python3 extract_cramfs.py"
elif [ ! -f "$RX3_HOME/runtime-symlinks.json" ]; then
  # extract_cramfs.py writes the symlink map last, so this exact pair means it was interrupted.
  bad "extraction incomplete: runtime-files exists but runtime-symlinks.json does not"
  echo "       extract_cramfs.py writes that file last, so it was interrupted or it failed." >&2
  echo "       Re-run it and let it finish:  python3 extract_cramfs.py" >&2
elif [ "$NFILES" -lt 100 ]; then
  bad "extracted/runtime-files has only $NFILES files - re-run: python3 extract_cramfs.py"
else
  ok "extracted/runtime-files ($NFILES+ files)"
  ok "runtime-symlinks.json"
fi
[ -f "$RX3_HOME/extracted/player/pdj/rbp" ] && ok "recovered player binary" || bad "extracted/player/pdj/rbp missing - run: python3 recover-firmware.py"
[ -d "$RX3_ROOT/root/pdj" ] && ok "chroot built" || warn "chroot not built yet - run ./build-rootfs.sh"
echo

if [ -n "$MISSING" ]; then
  echo "Install what is missing with:"
  echo "  sudo apt install -y $(echo $MISSING | tr ' ' '\n' | sort -u | tr '\n' ' ' | sed 's/ *$//')"
  echo "  (or just run: ./install.sh deps)"
  echo
fi

if [ "${1:-}" = clean ]; then
  # Undo an install so a fresh one starts from nothing: units, rules, overlays, binaries, the chroot, and
  # the odd directories an older script version created by expanding a variable inside a quoted heredoc.
  sudo -v || { echo "This needs sudo. Run it from a terminal where you can enter your password." >&2; exit 1; }
  ALL=0; [ "${2:-}" = --all ] && ALL=1
  echo "This removes:"
  echo "  service rx3 + rx3-priv/rx3-pointer/rx3-overlay-*/rx3-hotkeys-* units, udev rules 97/98/99-rx3-*"
  echo "  $RX3_ROOT (the chroot, rebuilt by build-rootfs.sh)"
  echo "  $RX3_USB (USB copy-on-write layers: your sticks are untouched, only the firmware's edits to them)"
  echo "  $RX3_BINDIR/rx3-fb-present, rx3-touch-bridge, $RX3_LOGDIR/rx3-*.log, pi-runtime/, rbp-pi, build/"
  [ $ALL = 1 ] && echo "  extracted/, runtime-symlinks.json, the downloaded firmware and source ZIPs (--all)"
  stray_dirs | sed 's/^/  stray directory from an old script: /' 
  printf "Continue? [y/N] "; read -r a; [ "$a" = y ] || [ "$a" = Y ] || { echo "aborted"; exit 1; }
  sudo systemctl disable --now rx3 2>/dev/null
  sudo systemctl stop rx3-priv rx3-pointer 'rx3-overlay-*' 'rx3-hotkeys-*' 2>/dev/null
  sudo rm -f /etc/systemd/system/rx3.service /etc/udev/rules.d/97-rx3-input.rules /etc/udev/rules.d/98-rx3-flx4.rules /etc/udev/rules.d/98-rx3-controller.rules /etc/udev/rules.d/99-rx3-usb.rules
  sudo systemctl daemon-reload; sudo udevadm control --reload
  for m in $(findmnt -rn -o TARGET | grep -E "^($RX3_ROOT|$RX3_USB)/" | sort -r); do sudo umount -l "$m" 2>/dev/null; done
  sudo rm -rf "$RX3_ROOT" "$RX3_USB" "$RX3_BINDIR/rx3-fb-present" "$RX3_BINDIR/rx3-touch-bridge" "$RX3_LOGDIR"/rx3-*.log \
    "$RX3_HOME/pi-runtime" "$RX3_HOME/rbp-pi" "$RX3_HOME/build" "$RX3_HOME/__pycache__"
  remove_strays
  [ $ALL = 1 ] && sudo rm -rf "$RX3_HOME/extracted" "$RX3_HOME/runtime-symlinks.json" "$RX3_HOME"/official-source-*.zip "$RX3_HOME"/XDJ-RX3_*.zip "$RX3_HOME/aes256.key"
  ok "clean. Next: git pull, then ./install.sh doctor"
  exit 0
fi
if [ "${1:-}" = desktop ]; then
  # Undo everything the install changed about how the machine boots and who owns the audio devices.
  # The chroot, the scripts and the udev rules are left alone: ./install.sh puts it back.
  # Every step is checked against the resulting state, because a sudo that cannot prompt fails
  # silently and reporting success we did not achieve is worse than reporting the failure.
  sudo -v || { echo "This needs sudo. Run it from a terminal where you can enter your password." >&2; exit 1; }
  RC=0

  echo "== stopping the player"
  sudo systemctl disable --now rx3 >/dev/null 2>&1
  if [ "$(systemctl is-active rx3 2>/dev/null)" != active ] && [ "$(systemctl is-enabled rx3 2>/dev/null)" != enabled ]; then
    ok "rx3 stopped, and no longer starts at boot"
  else
    bad "could not stop or disable rx3 (active=$(systemctl is-active rx3 2>/dev/null), enabled=$(systemctl is-enabled rx3 2>/dev/null))"; RC=1
  fi

  echo "== giving the sound devices back to PipeWire"
  systemctl --user unmask pipewire pipewire-pulse wireplumber pipewire.socket pipewire-pulse.socket 2>/dev/null
  systemctl --user start pipewire pipewire-pulse wireplumber 2>/dev/null
  # Three outcomes, not two: "masked" is a real failure, but an empty answer just means there is no
  # user session bus to ask (running over sudo, or no active login), which is not evidence of failure.
  PWSTATE=$(systemctl --user is-enabled pipewire 2>/dev/null)
  case "$PWSTATE" in
    masked) bad "PipeWire is still masked for $RX3_USER"; RC=1;;
    '')     warn "could not reach $RX3_USER's session bus to check PipeWire; verify with: systemctl --user is-enabled pipewire";;
    *)      ok "PipeWire unmasked for $RX3_USER ($PWSTATE)";;
  esac

  echo "== restoring the desktop"
  sudo systemctl set-default graphical.target >/dev/null 2>&1
  if [ "$(systemctl get-default)" = graphical.target ]; then
    ok "boots to graphical.target"
  else
    bad "default target is still $(systemctl get-default)"; RC=1
  fi
  DM=""
  for d in lightdm gdm3 sddm greetd; do
    systemctl list-unit-files "$d.service" 2>/dev/null | grep -q "^$d.service" && { DM=$d; break; }
  done
  if [ -z "$DM" ]; then
    warn "no display manager installed - the desktop may not be installed (sudo apt install raspberrypi-ui-mods)"
  else
    sudo systemctl enable "$DM" >/dev/null 2>&1
    if [ "$(systemctl is-enabled "$DM" 2>/dev/null)" = enabled ]; then
      ok "$DM enabled"
    else
      bad "$DM is still $(systemctl is-enabled "$DM" 2>/dev/null)"; RC=1
    fi
  fi

  echo
  if [ $RC -eq 0 ]; then
    echo "Done. Start the desktop now without rebooting:  sudo systemctl start ${DM:-lightdm}"
    echo "Or just reboot."
  else
    echo "Some steps did not take effect - see the MISS lines above." >&2
  fi
  echo "To go back to the player:  ./install.sh && sudo systemctl enable --now rx3"
  exit $RC
fi
if [ "${1:-}" = deps ]; then
  echo "== installing packages"
  sudo apt update
  sudo apt install -y fuse-overlayfs uhubctl exfatprogs alsa-utils python3-pil python3-cryptography \
                      gcc build-essential gcc-arm-linux-gnueabi rsync p7zip-full \
                      libfreetype6-dev pkg-config fonts-dejavu-core gpiod || exit 1
  echo "Done. Now run: ./install.sh doctor"
  exit 0
fi
if [ "${1:-}" = doctor ]; then
  [ $FAIL -eq 0 ] && echo "All prerequisites present." || echo "Fix the MISS lines above, then re-run."
  exit $FAIL
fi
[ $FAIL -ne 0 ] && { echo "Prerequisites missing - fix the MISS lines above, then re-run."; exit 1; }

sudo -v || { echo "This needs sudo. Run it from a terminal where you can enter your password." >&2; exit 1; }

echo "== helper binaries"
# fb-present draws its labels with FreeType, whose headers live under /usr/include/freetype2.
FT_CFLAGS=$(pkg-config --cflags freetype2 2>/dev/null || echo -I/usr/include/freetype2)
FT_LIBS=$(pkg-config --libs freetype2 2>/dev/null || echo -lfreetype)
gcc -O2 -DRX3_ROOT_PATH="\"$RX3_ROOT\"" $FT_CFLAGS -o "$RX3_BINDIR/rx3-fb-present" "$RX3_HOME/fb-present.c" $FT_LIBS || {
  echo "Building rx3-fb-present failed. It needs the FreeType headers:  sudo apt install libfreetype6-dev pkg-config" >&2; exit 1; }
gcc -O2 -DRX3_ROOT_PATH="\"$RX3_ROOT\"" -o "$RX3_BINDIR/rx3-touch-bridge" "$RX3_HOME/touch-bridge.c" || exit 1
ok "built rx3-fb-present and rx3-touch-bridge in $RX3_BINDIR"

echo "== udev rules and systemd unit"
tmp=$(mktemp -d); trap 'rm -rf "$tmp"' EXIT
for f in 97-rx3-input.rules 99-rx3-usb.rules; do
  sed "s|@RX3_HOME@|$RX3_HOME|g" "$RX3_HOME/$f.in" > "$tmp/$f"
done
# One rule per known controller (controllers.py is the single list): when its sound card appears, move the player onto it.
{
  echo "# Known DJ controllers ($(python3 "$RX3_HOME/controllers.py" names)): sound card hot-plug -> controller-hotplug.sh"
  for id in $(python3 "$RX3_HOME/controllers.py" usbids); do
    echo "ACTION==\"add\", SUBSYSTEM==\"sound\", KERNEL==\"card*\", ATTRS{idVendor}==\"${id%%:*}\", ATTRS{idProduct}==\"${id##*:}\", RUN+=\"/usr/bin/systemd-run --no-block $RX3_HOME/controller-hotplug.sh\""
  done
} > "$tmp/98-rx3-controller.rules"
sudo rm -f /etc/udev/rules.d/98-rx3-flx4.rules
sed "s|@RX3_HOME@|$RX3_HOME|g" "$RX3_HOME/rx3.service.in" > "$tmp/rx3.service"
sudo install -m 644 "$tmp"/*.rules /etc/udev/rules.d/ || exit 1
sudo install -m 644 "$tmp/rx3.service" /etc/systemd/system/ || exit 1
sudo udevadm control --reload
sudo systemctl daemon-reload
ok "installed udev rules and rx3.service"

echo "== audio: keep PipeWire off the sound cards"
systemctl --user mask --now pipewire pipewire-pulse wireplumber pipewire.socket pipewire-pulse.socket 2>/dev/null
PWSTATE=$(systemctl --user is-enabled pipewire 2>/dev/null)
case "$PWSTATE" in
  masked) ok "PipeWire masked for $RX3_USER";;
  '')     warn "could not reach $RX3_USER's session bus to mask PipeWire; run this as $RX3_USER, or verify with: systemctl --user is-enabled pipewire";;
  *)      warn "PipeWire is $PWSTATE for $RX3_USER - it may grab the controller before the player does";;
esac

echo "== console: give the player the framebuffer"
# The player draws straight to /dev/fb0, so a running desktop would fight it for the display.
if [ "$(systemctl get-default)" != multi-user.target ]; then
  echo "  This Pi currently boots to a desktop. The player needs the framebuffer to itself, so"
  echo "  the desktop will be disabled and the Pi will boot to a console from now on."
  echo "  To hand it back to the desktop later, run:  ./install.sh desktop"
fi
sudo systemctl set-default multi-user.target >/dev/null 2>&1
sudo systemctl disable lightdm >/dev/null 2>&1
if [ "$(systemctl get-default)" = multi-user.target ]; then
  ok "booting to multi-user (no desktop)"
else
  warn "default target is still $(systemctl get-default) - the desktop will compete for the display"
fi

echo
echo "Done. Start it with:  sudo systemctl enable --now rx3"
echo "Logs:  $RX3_LOGDIR/rx3-player.log   journalctl -u rx3 -f"
