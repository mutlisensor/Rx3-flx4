# Roadmap

Planned work, in phases. Nothing here is implemented yet; each phase is meant to be done, tested on the Pi 5 +
FLX4 + Touch Display 2, and merged before the next. Effort: S = an evening, M = a few sessions, L = open-ended
(reverse engineering involved).

## Phase 1 — Polish and safety (small, high value)

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
| Unmapped SHIFT layer | SHIFT + CUE (to start), loop in/out adjust, SHIFT + RELOOP, SHIFT + BEAT FX ON (all off), SHIFT + browse turn (waveform zoom), beat jump size on SHIFT + pads 7/8 | S–M |
| SLIP LOOP pad mode | The RX3's fourth pad mode (LED id 16) has no FLX4 button; put it on PAD FX1 or SAMPLER | S |
| PAD FX2 button | Not in the bridge's pad mode list, so it does not even light | S |
| Remaining lights | Beat FX ON/OFF (firmware id 48), SMART CFX, loop/reloop states on SHIFT layers, track-loaded lights (`0x9F`) | S |
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
| Other boards | Pi 4 / other ARM boards: the VBUS trick and the CPU cap are Pi 5 specific | M |
| Other controllers | DDJ-FLX6, DDJ-REV1 etc. through the key map table (phase 2) | M each |
| Other XDJ firmware | Other models' firmware: the addresses in the shims are 1.19-specific | L |
| One-step image | A flashable SD image instead of clone + install | M |
