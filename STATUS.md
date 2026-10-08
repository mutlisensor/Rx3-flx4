# Status — 2026-10-09

What works, what is unfinished, and what has not been tried. Controls are listed in [`KEYMAP.md`](KEYMAP.md);
planned work is in [`ROADMAP.md`](ROADMAP.md); the technical detail behind each item is in
`rx3-handoff/PI-SETUP-NOTES.md`.

## Branches

| Branch | Display | Notes |
|---|---|---|
| `main` | HDMI, any size | Firmware picture plus an on-screen panel of buttons and sliders |
| `7inch` | Raspberry Pi Touch Display 2 | Firmware picture scaled to the panel, four-button touch sidebar |
| `perf` | Touch Display 2 | `7inch` plus the low-latency, low-heat display pipeline (completed frames, partial redraw, idle frame pacing) |
| `Dev_tools` | HDMI | `main` plus debug aids; the aids themselves are merged into every branch |

Everything below applies to all branches unless marked. The Pi 5 + FLX4 + Touch Display 2 on `perf` is the test rig.

## Working and verified on hardware

**System**
- The real XDJ-RX3 1.19 firmware runs in its chroot and draws its own UI; starts at boot (`rx3` service).
- Start-up: player at ~22 s after power-on, controls live at ~26 s, both USB sticks at ~30 s. A controller that
  comes up wedged at power-on is revived by cutting USB power once, straight away.
- `./install.sh` builds and installs everything (presenter, touch bridge, player shim, udev rules, service);
  `install.sh doctor` checks prerequisites, `install.sh clean` removes it.
- Clean exit: ESC (held 1 s) or Ctrl+C on a USB keyboard stops the player and hands the screen back to the
  console; F5 restarts it, F12 writes a diagnostic snapshot.
- No blinking console cursor over the UI.
- Watchdog: if the player exits, or the firmware's audio thread stops writing, the player is restarted within
  ~15 s (`RX3_AUTO_RESTART`, default on; logged to `rx3-watchdog.log`).
- `/dev/gpiodrv` (written constantly by the firmware) lives in RAM, not on the SD card.

**Display and touch**
- `7inch`/`perf`: Touch Display 2, rotation set in `rx3.conf` (default 90° for the panel mounted on the FLX4).
  Touch on the firmware's own UI works everywhere (verified by a person), plus the sidebar: SOURCE, BROWSE
  (hold 2 s: SHORTCUT settings), USB STOP 1/2 (hold 2 s) showing each stick's volume label, and a status line
  with the SoC temperature (orange from 78 °C, red "HOT" when throttling) or LOW POWER on under-voltage.
- `perf`: no tearing on the zoomed waveform; only finished frames are shown and only changed parts redrawn.
- `main`: HDMI with an on-screen panel (SOURCE, BROWSE, BACK, UP/DOWN, ENTER, LOAD, PLAY, USB STOP, sliders).
- USB mouse works as a pointer when there is no touch panel.

**USB media**
- Two sticks as USB1 and USB2 through a copy-on-write overlay, so the sticks themselves are never written.
- Hot-plug, USB STOP (hold), physical removal and re-insert, all without a restart.
- Library browsing (rekordbox export), track load, playback on both decks, waveforms, BPM, hot cues.
- FAT sticks with non-ASCII file names (curly quotes, accents) load (no more E-8306 NO FILE).

**Audio**
- Through the FLX4: master on 1/2, headphones on 3/4, both at unity gain (the old fixed −12 dB cut is gone).
- Deck 1 → CH1, deck 2 → CH2, crossfader A/B.
- Unplugging the FLX4 mid-set keeps the decks playing silently in time; plugging it back in restores audio and
  controls within seconds with the same tracks loaded.
- Hot cue: command to sound in 1.4–5 ms; output buffer 2 × 128 frames at 44.1 kHz.

**DDJ-FLX4**
- Every deck, mixer, browse, Beat FX and colour FX control in [`KEYMAP.md`](KEYMAP.md), including the
  browse knob (on the deck screen it opens the library; elsewhere it selects, respecting focus).
- Button lights follow the firmware's own LED state: PLAY, CUE, BEAT SYNC, headphone CUE per deck, MASTER CUE,
  pad mode buttons, and the pads (set hot cues, beat loop, beat jump), blinking where the firmware blinks.
  LOOP IN/OUT light only while a loop runs (blinking as on the RX3, fast during loop adjust), RELOOP/EXIT while a
  loop is stored, and BEAT FX ON/OFF flashes while an effect is on.
- Loop adjust: during a loop, LOOP IN or LOOP OUT (with or without SHIFT) enters the RX3's in/out adjust; turn the
  jog, press again to finish.
- Quantize is set the same on both decks once the sticks are attached (`RX3_QUANTIZE`, default off).
- VU meters show the RX3's own channel meter.
- Pad modes HOT CUE, BEAT LOOP and BEAT JUMP switch the RX3 too, and the pads keep working after visiting
  PAD FX / SAMPLER.
- Vendor keep-alive, so the FLX4 never stops sending MIDI or mutes itself.

**Heat and power**
- CPU capped at 2.0 GHz while the player runs (`RX3_CPU_MAX_MHZ`); static screens redraw at 20 fps
  (`perf`, `RX3_FW_IDLE_FPS`). Core power playing: 2.58 W → 2.06 W (1.61 W with the optional
  `RX3_FW_FPS=30`). Idle ~1.3 W.
- Supply: no under-voltage on the current power supply (`vcgencmd get_throttled` = 0x0, 5.14 V).
- The presenter logs the SoC temperature once a minute and flags throttling.

## Known issues

- **Heat without a fan**: in a closed pod during a heat wave the SoC reached 85 °C and throttled to 1.5 GHz.
  Much better since the heat work, but a fan or the official Active Cooler is still recommended.
- **Audio crash loop (seen once)**: the firmware's audio thread got stuck in its beat-sync code and all sound
  stopped until a restart. Not reproduced in four attempts and a soak; the player log now says
  "audio thread has stopped writing" if it happens.
- **Device names on the SOURCE screen**: the firmware reads each stick's label but its SOURCE screen draws the
  slot name (USB1/USB2) regardless; the names are shown on the sidebar's USB STOP buttons instead (`7inch`,
  `perf`).
- **SMART CFX light** stays dim (the button works: it steps through the colour effects).

## Not yet tried by a person

Recording, MIC and AUX inputs, PRO DJ LINK / export features, multi-hour sessions, the DDJ-400 (mapped from
Pioneer's layout and Mixxx, but no unit has been connected).
