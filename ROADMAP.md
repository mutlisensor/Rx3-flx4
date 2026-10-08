# Roadmap

Planned work, in phases. Nothing here is implemented yet; each phase is meant to be done, tested on the Pi 5 +
FLX4 + Touch Display 2, and merged before the next. Effort: S = an evening, M = a few sessions, L = open-ended
(reverse engineering involved).

## Phase 1 — Polish and safety — done 2026-10-09

Done: stick names (on the sidebar USB STOP buttons; the firmware's SOURCE screen only ever draws slot names),
auto-recovery (`rx3-watchdog.sh`), `/dev/gpiodrv` in RAM, `RX3_QUANTIZE`, the sidebar status line, and `perf`
merged into `7inch`. Also done from phase 2: loop adjust on SHIFT + LOOP IN/OUT, loop/reloop/Beat FX lights,
PAD FX2. The original plan:

| Item | Why | Effort |
|---|---|---|
| **Volume labels on the SOURCE screen** | Shows USB1/USB2 instead of each stick's name. The firmware probes `/dev/<partition>` with libblkid for the label; find what it actually reads (blkid cache, `/dev/disk/by-label`, the export's device name) and provide it | M |
| **Auto-recovery** | A crashed player, or the audio crash loop seen once, needs a manual restart. Restart the service on a player exit; on "audio thread stopped writing" offer or perform a restart | S |
| **`/dev/gpiodrv` off the SD card** | The firmware keeps writing GPIO bytes to a regular file on the SD card; bind it to a small tmpfs file like `/dev/fb0` | S |
| **Quantize at start-up** | Deck 1 starts with quantize on, deck 2 off. Make it a setting (`RX3_QUANTIZE=on/off/firmware`) applied when the engine is up | S |
| **Merge `perf` into `7inch`** | `perf` is `7inch` plus the display/heat work and is what runs on the rig; fold it back so there is one touch branch | S |
| **Status/temperature on screen** | Show throttling, under-voltage and "audio stopped" as a small sidebar indicator instead of only in logs | S |

## Phase 2 — Controller completeness

| Item | Why | Effort |
|---|---|---|
| Unmapped SHIFT layer | SHIFT + CUE (to start), SHIFT + BEAT FX ON (all off), SHIFT + browse turn (waveform zoom), beat jump size on SHIFT + pads 7/8 | S–M |
| SLIP LOOP pad mode | The RX3's fourth pad mode (LED id 16) has no FLX4 button; put it on PAD FX1 or SAMPLER | S |
| Remaining lights | SMART CFX, track-loaded lights (`0x9F`) | S |
| MASTER TEMPO / VINYL mode | No FLX4 control; candidates: SHIFT + pad mode buttons, or the touch UI only | S |
| DDJ-400 on hardware | Mapped from Pioneer's layout and Mixxx, never connected | S (needs a unit) |
| Key map as data | Move the mapping into a per-controller table file so other controllers need no code | M |

## Phase 3 — Robustness and long sessions

| Item | Why | Effort |
|---|---|---|
| Multi-hour soak | Memory growth, FUSE overlay behaviour, LED/VU threads, thermal steady state with and without a fan | M |
| Reproduce the audio crash loop | `BeatSync::checkPrecision` null read; find the trigger (deck states, sync/master changes) and avoid it | L |
| SD card wear and power loss | Read-only root or overlay for the OS, so pulling the plug mid-set cannot corrupt anything | M |
| USB edge cases | Sticks without a rekordbox export, exFAT, very large libraries, removal during load | M |
| Wi-Fi | Sessions dropped with power save on at −70 dBm; disable power save permanently or document Ethernet | S |

## Phase 4 — RX3 features not yet exercised

| Item | Why | Effort |
|---|---|---|
| Recording | The REC tab is there; where does the firmware write, and can it reach a stick through the overlay? | M |
| MIC / AUX | The FLX4 has a mic input; route its capture channels to the RX3's MIC channel | M |
| Device Library Plus sticks | Firmware 1.19 reads only the classic `export.pdb`; sticks exported for Device Library Plus only show nothing. Convert or document | M |
| PRO DJ LINK / rekordbox link | Network features over Ethernet: link export, history | L |
| Sampler / pad FX | The RX3 has no sampler pads; an engine-side sampler would be new work | L |

## Phase 5 — Platform (deferred from earlier planning)

| Item | Why | Effort |
|---|---|---|
| Other displays | Any DSI/HDMI size via the layout code; one branch with the layout chosen in `rx3.conf` instead of `main`/`7inch` | M |
| Other boards | Pi 4 / Pi 3: see [Raspberry Pi 4 and 3](#raspberry-pi-4-and-3) below | M |
| Other controllers | DDJ-FLX6, DDJ-REV1 etc. through the key map table (phase 2) | M each |
| Other XDJ firmware | Other models' firmware: the addresses in the shims are 1.19-specific | L |
| One-step image | A flashable SD image instead of clone + install | M |

## Raspberry Pi 4 and 3

Planning only; neither board has been tried. Numbers are scaled from the Pi 5 measurements (2026-10-08) by typical
per-core speed: a Pi 4 (Cortex-A72, 1.5–1.8 GHz) is roughly half a Pi 5 core, a Pi 3B+ (Cortex-A53, 1.4 GHz)
roughly a fifth, with less memory bandwidth on both.

**Where the time goes on a Pi 5, and what that becomes**

| Work | Pi 5 (measured) | Pi 4 (estimate) | Pi 3B+ (estimate) |
|---|---|---|---|
| Firmware drawing, deck playing (`gui_task`, full rate ~54 fps) | 61 % of a core | ~130 %: cannot keep up, drops to ~40 fps by itself | ~300 %: ~18 fps at best |
| Firmware drawing, static screen (paced to 20 fps) | 12–24 % | ~30–50 % | ~60–100 % |
| Shim frame compare + copy, per frame | ~1 ms | ~2.5 ms | ~6 ms |
| Presenter, deck playing | 18 % | ~40 % | ~90 % at 20 fps |
| Audio thread (JuceALSA) | 6 % | ~12 % | ~30 % |

The audio thread runs on its own core at real-time priority, so audio should hold on both; what suffers first is
how smooth and how quickly the screen follows.

**Do the performance changes still apply? Yes, and they matter more:**

- *Completed frames, partial redraw, change detection in the shim, idle pacing* (`perf`): all board-independent,
  and the saving is a larger share of a smaller CPU. Keep them.
- *CPU clock cap* (`RX3_CPU_MAX_MHZ=2000`): only a Pi 5 benefits. A Pi 4/3 runs at or below 2.0 GHz anyway, and
  `rx3-start.sh` never raises the limit above the board's maximum, so the cap is already a no-op there. Those
  boards need every MHz; do not lower it further.
- *Firmware frame cap* (`RX3_FW_FPS`): optional on a Pi 5, but needed on slower boards, so the firmware draws at a
  steady rate it can sustain instead of falling behind unevenly: **30 on a Pi 4, 15–20 on a Pi 3B+**.
- *Presenter filter*: bilinear on a Pi 5/4; `RX3_FILTER=nearest` on a Pi 3B+.
- *NEON paths* in the presenter are 64-bit only: use 64-bit Raspberry Pi OS on both boards.

**Board-specific differences to handle**

| Item | Pi 4 | Pi 3B+ |
|---|---|---|
| Wedged-controller power cut | No `USB_VBUS_EN`; `rx3-start.sh` falls back to `uhubctl` (the Pi 4 root hub switches all ports together) | `uhubctl` on the LAN951x hub |
| USB | Separate USB 3 controller: fine for FLX4 + two sticks | One USB 2 bus shared with Ethernet and both sticks: use Wi-Fi, keep the sticks small and fast |
| Display | Touch Display 2 (DSI) and HDMI both work; vsync ioctl may be missing, presenter falls back to a timer | Prefer a 720p HDMI mode or the original 800×480 DSI panel; less to scale |
| Power monitor | No PMIC readout: judge by `vcgencmd measure_temp` / `get_throttled` | Same |
| Heat | Throttles at 80 °C; heatsink or fan advised | Throttles at 80 °C; heatsink needed under this load |
| RAM | 2 GB+ fine | 1 GB: fits, nothing else running |

**Plan (when a board is available)**

1. Detect the model (`/proc/device-tree/model`) in `rx3-env.sh` and pick defaults: Pi 5 as now; Pi 4
   `RX3_FW_FPS=30`; Pi 3 `RX3_FW_FPS=20`, `RX3_FILTER=nearest`. All still overridable in `rx3.conf`.
2. Measure on the board: per-thread CPU (`thr.sh`), presenter minute report, hot-cue latency, audio peaks over a
   30-minute playing session; adjust the defaults from what is measured.
3. Check the board-specific items above (power cut, display vsync, USB load with both sticks playing).
4. Write the result into `INSTALL.md` ("Running on a Raspberry Pi 4 / 3B+").

