# Key map — DDJ-FLX4 → XDJ-RX3 firmware

What each control does, as mapped by `rx3-handoff/controller-bridge.py` (identical on every branch). The DDJ-400 uses
the same layout. "RX3" means the XDJ-RX3 function the firmware performs; everything not listed under
[Not mapped](#not-mapped-yet) reaches the firmware.

## Decks (each side)

| Control | Alone | With SHIFT |
|---|---|---|
| PLAY/PAUSE | Play / pause | SLIP |
| CUE | Cue (RX3 cue behaviour: set, preview, return) | — *(not mapped: jump to start)* |
| Jog wheel | Top: touch-sensitive, scratches/holds like the RX3 jog in VINYL mode; side: pitch bend | Fast search (×10) |
| TEMPO slider | Tempo (signed: centre = 0 %) | — |
| BEAT SYNC | Sync on/off | Tempo range (6/10/16 %/wide) |
| BEAT SYNC, long press | Make this deck the tempo MASTER | — |
| LOOP IN | Loop in. During a loop: loop-in adjust (turn the jog, press again to finish) | Same |
| LOOP OUT | Loop out. During a loop: loop-out adjust — turn the jog to move the loop end (one rotation = 1.8 s; anticlockwise shortens, down to ~7 ms for the stutter/roll effect), press again to finish | Same |
| RELOOP/EXIT | Reloop / exit | Same |
| CUE/LOOP CALL ◀ ▶ | Call previous / next memory cue or loop | Search back / forward (hold) |
| Headphone CUE | Headphone cue on/off for the channel | **QUANTIZE on/off** for that deck (both start as `RX3_QUANTIZE`, default off) |

## Pads

| Pad mode button | FLX4 pads | RX3 |
|---|---|---|
| HOT CUE | Pads 1–8 = hot cues A–H: press to set (empty pad, while playing or paused) or jump; SHIFT + pad deletes | HOT CUE mode |
| BEAT LOOP | Pads 1–8 = RX3 beat loop pads | BEAT LOOP mode |
| BEAT JUMP | Pads 1–8 = RX3 beat jump pads | BEAT JUMP mode |
| PAD FX1, PAD FX2, SAMPLER, KEYBOARD, KEY SHIFT | Lit, but the pads do nothing | No RX3 equivalent |

Lights: LOOP IN/OUT are lit only while a loop runs (blinking like the RX3; fast while adjusting), RELOOP/EXIT
while a loop is stored, BEAT FX ON/OFF flashes while an effect is on.

The pad lights show the firmware's state: set hot cues lit, empty ones dark; the active loop/jump pad in the
other modes. With QUANTIZE on, a hot cue pressed while playing jumps on the next beat (up to half a second at
124 BPM) — that is the RX3's own behaviour, not lag.

## Mixer

| Control | RX3 |
|---|---|
| TRIM, EQ HI/MID/LOW, channel fader (×2) | Same on the RX3's CH1/CH2 |
| CFX knob (×2) | COLOR knob for that channel |
| SMART CFX button | Next colour effect: FILTER → SPACE → DUB ECHO → SWEEP → NOISE → CRUSH (starts on FILTER) |
| Crossfader | Crossfader (CH1 = A, CH2 = B) |
| MASTER LEVEL | Master level |
| MASTER CUE | Master to headphones on/off |
| HEADPHONES LEVEL / MIX | Headphone level / cue–master mix |
| VU meters (lights) | The RX3's channel level meters |

## Browse and load

| Control | RX3 |
|---|---|
| BROWSE turn | Rotary selector (scroll lists) |
| BROWSE push | Deck screen: open the library where you left it. Any other screen: select / open the focused item (e.g. the highlighted USB on the SOURCE screen) |
| SHIFT + BROWSE push | BACK |
| LOAD (deck 1 / deck 2) | Load the highlighted track |

## Beat FX

| Control | RX3 |
|---|---|
| BEAT FX SELECT | Next effect (DELAY, ECHO, PING PONG, SPIRAL, HELIX, REVERB, FLANGER, PHASER, FILTER, TRANS, ROLL, SLIP ROLL, PITCH, VINYL BRAKE) |
| SHIFT + BEAT FX SELECT | Previous effect |
| BEAT ◀ / ▶ | Beat length shorter / longer |
| CH SELECT slide | CH1 / CH2 / MASTER |
| LEVEL/DEPTH | Effect level/depth |
| ON/OFF | Effect on/off |

## On the screen

- **Firmware touch UI** (Touch Display 2): every on-screen control of the RX3 itself — source, library, tabs,
  INFO, SHORTCUT, MY SETTINGS, waveform zoom, deck panels.
- **Sidebar** (`7inch`, `perf`): SOURCE · BROWSE (tap: library/deck; hold 2 s: SHORTCUT settings) ·
  USB STOP 1 and 2 (hold 2 s).
- **On-screen panel** (`main`, `Dev_tools`): SOURCE, BROWSE, BACK, UP, DOWN, ENTER, LOAD 1/2, PLAY/PAUSE 1/2,
  USB STOP 1/2 (hold), and sliders for deck levels, master, headphone mix/level and crossfader.
- **USB mouse** (when no touch panel): left = touch, wheel = browse, right = BACK, middle = ENTER.

## USB keyboard (any branch)

| Key | Action |
|---|---|
| ESC (hold 1 s) or Ctrl+C | Stop the player, back to the console |
| F5 | Restart the player |
| F12 | Diagnostic snapshot to `~/rx3-diag-<time>.txt` |

## Not mapped yet

FLX4 controls that send MIDI the bridge ignores (see [`ROADMAP.md`](ROADMAP.md), phase 2):

- SHIFT + CUE (jump to track start), SHIFT + jog touch, SHIFT + BEAT FX ON/OFF, SHIFT + BROWSE turn (waveform
  zoom in Mixxx).
- SHIFT + pads in BEAT LOOP / BEAT JUMP mode (e.g. beat jump size).
- PAD FX1, PAD FX2, SAMPLER, KEYBOARD, KEY SHIFT pads.

RX3 functions with no FLX4 control: VINYL/CDJ jog mode, MASTER TEMPO (key lock), SLIP LOOP pad mode, TRACK
SEARCH ◀ ▶, REVERSE, beat FX on the MIC channel, MIC/AUX inputs, recording. Several are reachable on the
touch screen.
