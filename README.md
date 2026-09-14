# Rx3-flx4

Run the **Pioneer XDJ-RX3 firmware (v1.19) player on a Raspberry Pi 5**, with a **Pioneer DDJ-FLX4** or **DDJ-400**
as the controller and sound card (the FLX4 is verified on hardware; the 400 is mapped from Pioneer's layout and
Mixxx's mapping and awaits a real unit). The connected controller is detected automatically.

This is the real RX3 firmware executing in an ARM32 chroot — not Mixxx, not an emulator, not a
reimplementation. This repository holds the host-side scaffolding that makes it run: the chroot
builder, launch and shutdown scripts, the `LD_PRELOAD` shims that stand in for the RX3's missing
hardware, a MIDI bridge that maps the FLX4 onto the firmware's internal key events, USB media
handling with copy-on-write, and the notes that document everything non-obvious.

Branches: `main` (HDMI layout), `7inch` (Touch Display 2 layout), `Dev_tools` (debug aids: keyboard hotkeys
to stop/restart the player and dump diagnostics, `install.sh clean`, `rx3-logs.sh`).

Status: working daily-driver setup. See [`STATUS.md`](STATUS.md) for the feature-by-feature list.

## Firmware is not included

No Pioneer code, firmware image, decryption key, or patched player binary is in this repository, and
none should ever be committed to it — they are Pioneer/AlphaTheta property. `.gitignore` blocks them.

You supply them yourself, from the official sources:

- Firmware: the official XDJ-RX3 v1.19 update package from AlphaTheta support downloads.
- GPL source distribution: Pioneer DJ's open-source code distribution page, which is where the
  per-sector AES key comes from.

`rx3-handoff/ASTRA-PROMPT.md` lists the exact URLs and the recovery sequence, and
`rx3-handoff/recover-firmware.py` plus `firmware_image.py` perform the extraction. Place the results
in `rx3-handoff/extracted/` and `build-rootfs.sh` will assemble the chroot from them.

## Layout

| Path | Purpose |
|------|---------|
| `rx3-handoff/` | Everything that is copied to the Pi, into `~/rx3-handoff` |
| `rx3-handoff/install.sh` | Host-side installer, and `install.sh doctor` to check prerequisites |
| `rx3-handoff/rx3-env.sh`, `rx3_env.py` | Path resolution — why no username is hardcoded |
| `rx3-handoff/*.in` | Templates for the udev rules and systemd unit, filled in by `install.sh` |
| `rx3-handoff/legacy/` | Dead prototype code from the first machine. Ignore it |
| `INSTALL.md` | Step-by-step bring-up, and troubleshooting |
| `rx3-handoff/PI-SETUP-NOTES.md` | The detailed reference: key codes, audio routing, USB semantics, traps |

### The interesting pieces

- **`fbshim.c` / `control-shim.c`** — preloaded into the firmware. They fake the framebuffer and
  device ioctls, redirect ALSA onto the FLX4, inject control events, call the firmware's own mixer
  routing functions, and hand privileged unmounts to a root helper.
- **`controller-bridge.py` / `controllers.py`** — translate the controller's MIDI into the firmware's internal
  key events; the table of known controllers (USB id, keep-alive, the few codes that differ) lives in
  `controllers.py`, and detection picks the first one plugged in.
- **`usb-attach.sh` / `usb-hotplug.sh` / `rx3-mtab.sh` / `rx3-priv.sh`** — present USB sticks to the
  firmware through a copy-on-write overlay so the user's media is never modified, and satisfy the
  firmware's mount-table and unmount expectations.
- **`touch-bridge.c` / `fb-present.c`** — display presenter and touch/mouse input adapter.

## Quick start

Full steps are in **[`INSTALL.md`](INSTALL.md)**. In short, on the Pi:

```bash
git clone https://github.com/mutlisensor/Rx3-flx4.git   # not with sudo: you must own these files
cd Rx3-flx4/rx3-handoff && chmod +x *.sh
./install.sh deps                                       # Debian packages
python3 recover-firmware.py && python3 extract_cramfs.py   # you supply the firmware
./install.sh doctor                                     # checks prerequisites, changes nothing
./build-rootfs.sh                                       # assembles the chroot
./install.sh                                            # udev rules, systemd unit, helper binaries
sudo systemctl enable --now rx3
```

To stop the player and hand the Pi back to its desktop, run `./install.sh desktop`. That also
unmasks PipeWire, without which the desktop comes back silent.

`./install.sh doctor` is the thing to run whenever something is unclear: it reports every
prerequisite as ok or missing, names the apt package or the script that fixes each one, and
changes nothing.

No username is baked in. `rx3-env.sh` and `rx3_env.py` resolve every path from where the scripts
live and who owns them, and each one can be overridden with an `RX3_*` environment variable.

## Legal

The scaffolding here is original work. It is published for interoperability and personal research on
hardware you own. Pioneer/AlphaTheta firmware is not distributed with it and you need a legitimate
copy to use any of this.
