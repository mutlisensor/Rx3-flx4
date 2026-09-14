# Installing

Run every command **on the Raspberry Pi**, as your normal login user. Nothing here needs a
particular username: the scripts work out where they live and which account owns them.

## 1. Get the files and the packages

```bash
git clone https://github.com/mutlisensor/Rx3-flx4.git
cd Rx3-flx4/rx3-handoff
chmod +x *.sh
./install.sh deps          # installs every Debian package this needs
```

Do not use `sudo` for the clone or the copy. The files must be owned by your own account, because
that is how the scripts work out where to build the chroot.

You can run everything from the clone, as above, or copy `rx3-handoff` somewhere more permanent
such as `~/rx3-handoff` and work there. Either is fine.

If you prefer to install the packages yourself instead of `./install.sh deps`:

```bash
sudo apt update
sudo apt install -y fuse-overlayfs uhubctl exfatprogs alsa-utils python3-pil python3-cryptography \
                    gcc build-essential gcc-arm-linux-gnueabi rsync p7zip-full \
                    libfreetype6-dev pkg-config fonts-dejavu-core gpiod
```

Note that two of these are not named after the command they provide: the `arm-linux-gnueabi-gcc`
compiler comes from **`gcc-arm-linux-gnueabi`**, and `7z` comes from **`p7zip-full`**.

## 2. Recover the firmware

The firmware is not in this repository and never will be — you supply it. These two scripts
download the official packages, decrypt them, and unpack what the player needs.

```bash
python3 recover-firmware.py     # downloads the official firmware + GPL source, decrypts, verifies
python3 extract_cramfs.py       # unpacks the root filesystem into extracted/runtime-files
```

**Run them in that order, and run both.** `recover-firmware.py` alone is not enough:
`extract_cramfs.py` is what produces `extracted/runtime-files/` and `runtime-symlinks.json`,
and the chroot is built from those.

After this you should have:

```
extracted/player/pdj/rbp        the original player binary (hash-verified)
extracted/gui/                  the GUI resources
extracted/runtime-files/        the root filesystem
runtime-symlinks.json           the symlink map
```

## 3. Check you are ready

```bash
./install.sh doctor
```

This changes nothing. It lists each prerequisite as `ok` or `MISS` and tells you which script to
run for anything missing. Fix every `MISS` before going on.

## 4. Build the chroot

```bash
./build-rootfs.sh
```

This assembles the chroot, patches the player, and compiles the preload shim. It stops with an
explicit message if step 2 was incomplete. Expect roughly 6.5 GB in `~/rx3-rootfs` and a minute of
work. You do not run `patch-player.py` yourself — `build-rootfs.sh` calls it at the right moment,
after copying the recovered player into place.

## 5. Install the host side

```bash
./install.sh
```

Builds the two helper binaries, generates the udev rules and the systemd unit with your real paths,
keeps PipeWire off the sound cards, and sets the Pi to boot without a desktop so the player owns the
framebuffer.

## 6. Run it

```bash
sudo systemctl enable --now rx3
journalctl -u rx3 -f
```

Plug in the DDJ-FLX4, an HDMI display, and a USB stick with a rekordbox export. The RX3 interface
appears on the display. A USB mouse works as a pointer until you attach a touchscreen.

---

# Debug tools

**Keyboard hotkeys.** Plug in any USB keyboard, before or after boot. The firmware never sees it; a small
watcher does, and it keeps running even when the player is stopped:

| Key | Effect |
|---|---|
| **ESC held for 1 s** | Stop the player (`systemctl stop rx3`). The screen drops to the text console. |
| **F5** | Restart the player. |
| **F12** | Write a diagnostic snapshot to `~/rx3-diag-<date>.txt`: journal, processes, mounts, sound cards, USB, throttling, and the tails of every rx3 log. Attach that file when reporting a hang. |

**Logs.** `./rx3-logs.sh` follows the player, the USB helpers and the kernel in one stream.

**Cleaning up a previous install.** `./install.sh clean` removes everything an earlier install put on the
machine (units, udev rules, the chroot, the USB overlay layers, binaries, logs) and keeps the recovered
firmware; `./install.sh clean --all` removes that too. Run it before reinstalling if things behave oddly.

---

# Display: HDMI or the Raspberry Pi Touch Display 2

Both work from the same install, with no configuration. The presenter reads the framebuffer's size and
draws the interface to fit, and the touch bridge uses the same geometry so touches land where things
are drawn.

- **HDMI**: any resolution, 16 or 32 bpp. A USB mouse is the pointer unless a touchscreen is present.
- **Touch Display 2** (7", DSI): detected by the firmware on a Pi 5 with nothing added to `config.txt`.
  It is a portrait panel (720x1280), so the interface is drawn rotated 90° into a 1152x720 landscape
  picture, and its Goodix touch controller is picked up automatically as the pointer.
  **It needs its own power cable.** The FFC cable carries video, touch and the backlight control, but the
  backlight is powered by the separate 3-pin lead to the GPIO header: red to pin 2 (5 V), black to pin 6
  (GND). With the USB and Ethernet ports facing down, that is the top-right of the header, cable
  vertical. Without it the Pi detects the panel and touch works, but the screen stays completely black
  with no glow. Raspberry Pi warns that connecting this cable to the wrong pins can damage the display.
  The FFC contacts face away from the display, towards the Ethernet/USB ports.
- **Both connected**: the DSI panel is its own DRM device with its own `/dev/fbN`, so the scripts prefer it
  and HDMI is left alone. Put `RX3_FB=/dev/fb1` (or whichever) in `rx3.conf` to choose otherwise.

Displays must be connected **at boot**; a framebuffer is not created on hotplug.

**If the picture is upside down** for the way the panel is mounted, set the rotation. Create
`rx3-handoff/rx3.conf` with one line and restart the service:

```bash
echo 'RX3_ROTATE=270' > ~/rx3-handoff/rx3.conf     # 0, 90, 180 or 270, clockwise
sudo systemctl restart rx3
```

Portrait panels default to 90, landscape ones to 0. The touch mapping follows the same setting.
`rx3.conf` is also where `RX3_FB` and `RX3_FONT` go; it is sourced by every script.

---

# Controllers

Supported: **DDJ-FLX4** (verified on hardware) and **DDJ-400** (mapped from Pioneer's documented layout and
Mixxx's mapping, tested only with synthetic MIDI, not yet on a real unit). The controller is the sound card as
well as the control surface, so it must be connected when the player starts; the start script waits up to
30 s for one.

Detection is automatic: `controllers.py` recognises a controller by its sound card's USB id, and if two are
attached the one that was plugged in first is used (sound cards are numbered in plug-in order). Only one
controller is driven at a time. `./install.sh doctor` shows what is connected.

The two units share one MIDI layout; the per-controller differences are a handful of entries in
`controllers.py`: the FLX4 needs a keep-alive message every 200 ms and the 400 needs one status request at
start-up, the Beat FX CH SELECT switch and SHIFT+PLAY use different codes. Adding another DDJ-400-family
controller (FLX6, 200 ...) means adding a row there and, if its layout deviates, a case in
`controller-bridge.py`. The udev rule that moves the player onto a freshly plugged controller is generated
from that same table by `install.sh`.

**Testing a mapping without the hardware:** `RX3_CONTROLLER=ddj400 controller-bridge.py -` reads raw MIDI
bytes from stdin and prints the firmware keys it would send when `RX3_BRIDGE_LOG=1` is set. With a real
unit, `RX3_BRIDGE_LOG=1` (the service sets it) makes `rx3-controller.log` show every incoming MIDI message,
which is what to send along if a control does the wrong thing.

---

# Stopping the player, and getting the desktop back

**Just stop it for now** (it will still start at the next boot):

```bash
sudo systemctl stop rx3
```

**Hand the Pi back to its desktop**, which is what you want if you are done with the player:

```bash
cd ~/rx3-handoff && ./install.sh desktop
sudo systemctl start lightdm      # or just reboot
```

That reverses the three things the install changed: it stops the player and takes it out of the boot
sequence, sets the Pi back to booting to `graphical.target` with its display manager enabled, and
unmasks PipeWire. **That last one matters.** The installer masks PipeWire so it cannot claim the
FLX4, and if you restore the desktop without undoing it you get a desktop with no sound at all.
Each step is checked and reported, so you can see what actually took effect.

Nothing is deleted. The chroot, the scripts, the udev rules and the service all stay where they are.

**Go back to the player** whenever you like:

```bash
cd ~/rx3-handoff && ./install.sh && sudo systemctl enable --now rx3
```

**Note on SSH.** Stopping the player does not give you a desktop by itself, because the installer set
the Pi to boot to a console. Until you run `./install.sh desktop`, the screen returns to a text
login, not to the desktop.

---

# Running on a Raspberry Pi 3B+

Everything here was developed and verified on a **Pi 5**. The scripts are board-agnostic and the
display presenter adapts to whatever resolution the screen reports, so a 3B+ should install and
start the same way. What has **not** been tested on a 3B+ is whether it plays music well. Expect to
find out, and expect these to be the limits:

- **USB bandwidth is the real risk.** A 3B+ puts all four USB ports *and* Ethernet behind one shared
  USB 2.0 controller. The FLX4 moves four channels of audio in and out over that same bus as the USB
  stick the music streams from. Audio dropouts under load would not be surprising. Use Wi-Fi rather
  than Ethernet to take one competitor off the bus.
- **Power.** A bus-powered FLX4 on a 3B+ is asking a lot of the supply. Use a powered USB hub for
  the controller. Under-voltage shows up as the controller failing to enumerate, or dropping out
  mid-set.
- **RAM.** A 3B+ has 1 GB, and the chroot mounts a 256 MB tmpfs. It fits, but do not expect to run a
  desktop alongside it. The installer disables the desktop anyway.
- **The interface may feel sluggish.** The presenter composites and scales a full frame in software
  on the CPU.

If audio breaks up, the first things to try are moving the USB stick to a powered hub, switching from
Ethernet to Wi-Fi, and confirming the Pi is not reporting under-voltage:

```bash
vcgencmd get_throttled     # 0x0 is healthy; anything else means power trouble
```

---

# Troubleshooting

**"I ran recover-firmware.py and extract_cramfs.py — what now?"**
`./install.sh doctor`, then `./build-rootfs.sh`, then `./install.sh`. In that order.

**"extraction incomplete: runtime-files exists but runtime-symlinks.json does not"**
`extract_cramfs.py` was interrupted or it failed. It writes the symlink map as its very last step,
so that pair of symptoms means it never reached the end. Run it again:

```bash
python3 extract_cramfs.py
```

It must finish with `Extraction complete.` If it does not, the run did not count.

**`PermissionError: ... extracted/runtime-files/bin/bashbug`** (or any other file there)
A bug in older copies of `extract_cramfs.py`: firmware files are written with their original
read-only modes, so a second run could not overwrite them, and every retry after an interruption
failed at the same place. Pull the latest version and run it again. If you would rather not pull,
`rm -rf extracted/runtime-files` first and the old script will get through.

**"A script made `~/rx3-rootfs` but there is nothing inside it."**
`extracted/runtime-files/` was missing or empty when `build-rootfs.sh` ran, so there was nothing to
copy in. Run `python3 extract_cramfs.py`, confirm `extracted/runtime-files/` has content, then run
`./build-rootfs.sh` again. Current versions of the script refuse to start in this situation instead
of leaving you an empty directory.

**"patch-player.py says the path is wrong."**
Do not run it directly. It expects `pi-runtime/rbp`, which `build-rootfs.sh` puts there by copying
the recovered player just before calling it. Run `./build-rootfs.sh` instead.

**`FileNotFoundError: '$RX3_ROOT/dev/rx3-ui-state'` in the journal, or directories literally named `$RX3_ROOT` / `$RX3_USERHOME` / `$R` appearing in the filesystem root, your home, or `rx3-handoff`**
A bug in the scripts between 11 and 14 September 2026: a variable was written inside a single-quoted
Python heredoc, which the shell never expands, so Python received the literal text and created paths
relative to wherever it was running (the filesystem root under systemd). Fixed on every branch.
`git pull`, then `./install.sh strays` to delete just those directories (it lists them and asks first), or
`./install.sh clean` for a full reset. `./install.sh doctor` warns whenever it sees one.

**"Everything is hardcoded to /home/rx3 or /home/pompu_5."**
Fixed. Paths are resolved at runtime by `rx3-env.sh` and `rx3_env.py` from the location of the
scripts and the account that owns them, so any username works. If you cloned before this change,
pull again. The only remaining `/home/pompu_5` references are in `rx3-handoff/legacy/`, which is
dead prototype code you should ignore — see the README in that directory.

**Overriding the layout.** Export any of these before running anything to place things elsewhere:

| Variable | Default |
|---|---|
| `RX3_HOME` | the directory the scripts are in |
| `RX3_USER` | the account that owns `RX3_HOME` |
| `RX3_ROOT` | `~/rx3-rootfs` |
| `RX3_USB` | `~/rx3-usb` |
| `RX3_BINDIR` | `~` (helper binaries) |
| `RX3_LOGDIR` | `~` (`rx3-*.log`) |

**"WARNING: ... is owned by a system account"**
You copied the files with `sudo`, so the directory belongs to root and the chroot would be built in
root's home instead of yours. Fix it with:

```bash
sudo chown -R $(id -un):$(id -gn) ~/rx3-handoff
```

**`fatal error: ft2build.h: No such file or directory`**
The on-screen labels are drawn with FreeType, so the presenter needs its headers to build:

```bash
sudo apt install libfreetype6-dev pkg-config
```

Then run `./install.sh` again. `./install.sh deps` installs this for you.

**Touch Display 2 completely black, no backlight glow, touch works, `install.sh doctor` shows the display**
The GPIO power lead is missing or loose. The kernel cannot tell: the touch controller and backlight
control run from the FFC, only the backlight LEDs need the 5 V lead. See "Display" above.

**Nothing on screen, and the log says `no /dev/fb0`**
The framebuffer is created at boot only if a display was connected then, and it does not appear on
hotplug. Connect the HDMI display and reboot. Check what the kernel sees with:

```bash
cat /sys/class/drm/card*/card*-HDMI*/status     # should say "connected"
```

**The display stays black and `rx3-fb-present` exits immediately**
It needs a TrueType font and could not find one, which happens on a minimal Raspbian image. Install
one with `sudo apt install fonts-dejavu-core`, or point `RX3_FONT` at a `.ttf` of your choosing. The
presenter names the paths it tried when it fails.

**The FLX4 is lit but not detected after a reboot, until you unplug and re-plug it**
A bus-powered FLX4 that was attached while the Pi powered up often comes up wedged: powered, but never
signalling on USB. Only removing its power revives it. The Pi 5 cannot switch power per port (`uhubctl`
"off" merely disables the port, the controller stays lit), so the start script cuts the RP1's single
`USB_VBUS_EN` line for five seconds when the controller is missing ten seconds into start-up. Every USB
device re-enumerates after that, which is why it happens before any media is attached. Needs the `gpiod`
package; `./install.sh deps` installs it. If it still fails, plug the FLX4 into its own supply on its
DC-IN port so it does not power up together with the Pi.

**Under-voltage warnings or the FLX4 not enumerating.** Use the official 27 W supply or a powered
USB hub. The controller draws enough to brown out a Pi 5 on an underpowered supply.

**The deeper reference.** `rx3-handoff/PI-SETUP-NOTES.md` documents the key codes, audio routing,
USB semantics and every non-obvious trap found while building this.
