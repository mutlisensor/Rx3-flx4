#!/usr/bin/env python3
"""DJ controller -> XDJ-RX3 firmware control bridge (DDJ-FLX4, DDJ-400; see controllers.py).

Reads raw MIDI from the controller and translates it into the RX3 firmware's
queued key messages on the chroot control FIFO (see control-shim.c):
  struct command {int key,operation,channel,value; float analog; int extra;}
  operation: 0 press, 2 release, 4 rotary/analog, 5 switch/tempo.  channel: 0 global, 1 deck1, 2 deck2.

MIDI map is the DDJ-400 family layout (documented by Pioneer, cross-checked against Mixxx's mappings):
  note-on/off on ch0/ch1 = deck 1/2 buttons, ch6 = mixer/browse buttons, ch4/5 = Beat FX,
  ch7/ch9 = deck 1/2 pads (ch8/10 with SHIFT), CC on ch0/ch1 = deck knobs/faders, ch6 = master.
Usage: controller-bridge.py [/dev/snd/midiC?D0]   (auto-detects the first connected known controller)
       RX3_CONTROLLER=flx4|ddj400 forces the controller profile; a device of "-" reads MIDI from stdin and writes
       outgoing MIDI (LEDs, keep-alive) to $RX3_MIDI_OUT, for testing a mapping without the hardware.
"""
import glob, os, struct, sys, time, threading

import rx3_env, controllers
ROOT = rx3_env.ROOT
FIFO = ROOT + '/dev/rx3-control'

# ---- RX3 firmware key ids (keycodes.txt) ----
K = dict(play=0x4101, cue=0x4102, shift=0x4103, vinyl=0x4104, temporange=0x4107, mastertempo=0x4108,
         tempo=0x4109, quantize=0x410b, loopin=0x410c, loopout=0x410d, reloop=0x410e, slip=0x4110,
         master=0x4111, sync=0x4112, hotcue=0x4113, autobeatloop=0x4114, beatjump=0x4116,
         searchfwd=0x411f, searchrev=0x4120, rotary=0x420c, back=0x420d, trackfwd=0x4214, trackrev=0x4215,
         jog=0x4305, jogtouch=0x4306, load=0x4311, masterlv=0x4403, hpmix=0x4405, hplv=0x4406,
         fxonoff=0x448d, fxtime=0x448e, fxdepth=0x448f, beatprev=0x4490, beatnext=0x4491, fxselect=0x448b, fxch=0x448c,
         callnext=0x4322, callprev=0x4323,
         trim=0x5019, eqh=0x501a, eqm=0x501b, eql=0x501c, fader=0x501e, hpcue=0x5020, color=0x509d,
         cfxfilter=0x50a6, cfxknob=0x50a7, cross=0x6017, browse=0x202, usb1=0x209, mastercue=0x4407)
PAD = [0x4117 + i for i in range(8)]

fifo = os.open(FIFO, os.O_RDWR | os.O_NONBLOCK)
LOG = os.environ.get('RX3_BRIDGE_LOG')
def send(key, op, ch=0, value=0, analog=0.0):
    if LOG: print('%s key=%04x op=%d ch=%d val=%d a=%.3f' % (time.strftime('%H:%M:%S'), key, op, ch, value, analog), flush=True)
    try: os.write(fifo, struct.pack('<iiiifi', key, op, ch, value, analog, 0))
    except BlockingIOError: pass
def press(key, ch, down): send(key, 0 if down else 2, ch)
def analog(key, ch, v): send(key, 4, ch, 0, v)

# ---- is the firmware showing its deck (play) screen? ----
# The deck screen always has the same white labels in its two bottom panels ("TRACK", "TEMPO" for each deck),
# whether or not tracks are loaded or playing, and with the INFO panel open. Measured on firmware 1.19: 75, 75,
# 128, 128 lit pixels in these boxes; the library, source and shortcut screens have nothing like it. Reading the
# firmware's own 1280x800 picture is self-correcting, unlike tracking screen changes we cannot all see.
FW_FB = ROOT + '/dev/fb0'
DECK_LABELS = (((36, 648, 80, 661), 75), ((676, 648, 720, 661), 75), ((396, 648, 441, 661), 128), ((1036, 648, 1081, 661), 128))
def deck_showing():
    try: fd = os.open(FW_FB, os.O_RDONLY)
    except OSError: return False
    try:
        for (x0, y0, x1, y1), expect in DECK_LABELS:
            lit = 0
            for y in range(y0, y1):
                row = os.pread(fd, (x1 - x0) * 4, (y * 1280 + x0) * 4)   # BGRX
                lit += sum(1 for k in range(0, len(row) - 3, 4) if row[k] > 170 and row[k + 1] > 170 and row[k + 2] > 170)
            if abs(lit - expect) > expect // 4: return False
        return True
    finally: os.close(fd)

# ---- MIDI note -> (key, channel-kind) for deck note channels (0x90/0x91) ----
DECK_NOTES = {0x0B: 'play', 0x0C: 'cue', 0x3F: 'shift', 0x10: 'loopin', 0x11: 'loopout', 0x4D: 'reloop',
              0x58: 'sync', 0x5C: 'master', 0x60: 'temporange', 0x54: 'hpcue', 0x36: 'jogtouch', 0x68: 'quantize',
              0x40: 'searchfwd', 0x3D: 'searchfwd', 0x3E: 'searchrev', 0x51: 'callprev', 0x53: 'callnext'}
# SHIFT+PLAY (censor) is added per controller at start-up: 0x0E on the FLX4, 0x47 on the DDJ-400.
MIXER_NOTES = {0x46: ('load', 1), 0x47: ('load', 2), 0x41: ('rotary_press', 0), 0x42: ('back', 0)}
DECK_CC_14 = {0x00: 'tempo'}                       # MSB 0x00 + LSB 0x20 (14-bit)
DECK_CC = {0x04: 'trim', 0x07: 'eqh', 0x0B: 'eqm', 0x0F: 'eql', 0x13: 'fader'}
MASTER_CC = {0x1F: 'cross', 0x0C: 'hpmix', 0x0D: 'hplv', 0x08: 'masterlv'}
JOG_CC = {0x21: 'bend', 0x22: 'scratch', 0x23: 'bend', 0x29: 'search'}

msb = {}
rotary_forwarding = [False]    # browse knob: did the matching press go to the firmware's select key?
# The RX3 treats jog ticks as an ongoing rotation until it sees a zero-value jog report (the physical jog reports its
# stopping). These controllers only send ticks while turning, so report zero when the wheel has been idle for a moment.
jog_last = {1: 0.0, 2: 0.0}; jog_active = {1: False, 2: False}
def jog_stop(deck):
    if jog_active[deck]: jog_active[deck] = False; send(K['jog'], 4, deck, 0, 0.0)
def jog_moved(deck): jog_last[deck] = time.time(); jog_active[deck] = True
def jog_watchdog():
    while True:
        time.sleep(0.03); now = time.time()
        for d in (1, 2):
            if jog_active[d] and now - jog_last[d] > 0.08: jog_stop(d)
threading.Thread(target=jog_watchdog, daemon=True).start()
jog_scale = float(os.environ.get('RX3_JOG_SCALE', '1'))

def note(status, n, vel):
    ch = status & 0x0F; down = (status & 0xF0) == 0x90 and vel > 0
    if ch in (0, 1) and n in PAD_MODES:
        if down: select_pad_mode(ch + 1, n)
        return
    if ch in (0, 1):
        deck = ch + 1
        name = DECK_NOTES.get(n)
        if name == 'jogtouch':
            if not down: jog_stop(deck)     # the RX3 needs a zero jog report or Play stays blocked after a scratch
            press(K['jogtouch'], deck, down); return
        if name == 'hpcue': press(K['hpcue'], deck, down); return
        if name: press(K[name], deck, down); return
    if ch in (4, 5):                        # BEAT FX section
        global beatfx_index
        if n == 0x47: press(K['fxonoff'], 0, down); return
        if n == 0x4A: press(K['beatprev'], 0, down); return
        if n == 0x4B: press(K['beatnext'], 0, down); return
        # RX3 BEAT FX list (switch keys use operation 5 with the value): 0 DELAY 1 ECHO 2 PING PONG 3 SPIRAL 4 HELIX 5 REVERB
        # 6 FLANGER 7 PHASER 8 FILTER 9 TRANS 10 ROLL 11 SLIP ROLL 12 PITCH 13 VINYL BRAKE
        if n == 0x63 and down: beatfx_index = (beatfx_index + 1) % 14; send(K['fxselect'], 5, 0, beatfx_index, float(beatfx_index)); return
        if n == 0x64 and down: beatfx_index = (beatfx_index - 1) % 14; send(K['fxselect'], 5, 0, beatfx_index, float(beatfx_index)); return
        if n in (0x10, 0x11, 0x14) and down:   # CH SELECT slide -> RX3 values: 0 CH1, 1 CH2, 2 MIC, 3 CF.A, 4 CF.B, MASTER = FXCH_MASTER
            sel = CTL['fxch'].get((ch, n), 'master'); sel = FXCH_MASTER if sel == 'master' else sel
            send(K['fxch'], 5, 0, sel, float(sel)); return
        return
    elif ch == 6:
        global cfx_index
        if n == 0x63: press(K['mastercue'], 0, down); return                               # MASTER CUE
        if n == 0x00 and down: cfx_index = (cfx_index + 1) % len(CFX); select_cfx(); return  # SMART CFX: next colour FX
        m = MIXER_NOTES.get(n)
        if m:
            key, deck = m
            if key == 'rotary_press':
                # Browse knob push: the firmware uses it to select/open on every screen except the deck screen,
                # where it does nothing; there, open the library instead, like the BROWSE key.
                if down:
                    rotary_forwarding[0] = not deck_showing()
                    if rotary_forwarding[0]: press(K['rotary'], 0, True)
                    else: send(K['browse'], 0); send(K['browse'], 2)
                elif rotary_forwarding[0]: press(K['rotary'], 0, False)
            else: press(K[key], deck, down)
            return
    elif ch in (7, 9):                      # performance pads, deck 1 / deck 2
        deck = 1 if ch == 7 else 2
        if n < 0x08 or 0x20 <= n < 0x28 or 0x60 <= n < 0x68:
            press(PAD[n & 7], deck, down); return
    elif ch in (8, 10):                     # shift + pads
        deck = 1 if ch == 8 else 2
        if n < 0x08: press(K['shift'], deck, True); press(PAD[n & 7], deck, down); press(K['shift'], deck, False); return

def cc(status, c, v):
    ch = status & 0x0F
    if ch in (0, 1):
        deck = ch + 1
        if c in DECK_CC_14: msb[(ch, c)] = v; return
        if c in (0x20,):                    # tempo LSB
            m = msb.get((ch, 0x00), 0); val = (m << 7 | v) / 16383.0
            # The engine's tempo slider is signed: -1 = full minus, 0 = centre, +1 = full plus (DjEngineIF::getTempoSlider
            # reads back exactly what is sent). Feeding it the raw 0..1 fader made the centre detent +half range and the
            # top 0 %. The FLX4 sends 0 at the top, which is the minus end, like the RX3's own fader.
            send(K['tempo'], 5, deck, 0, (val - 0.5) * 2.0); return   # op 5 only
        if c in DECK_CC: analog(K[DECK_CC[c]], deck, v / 127.0); return
        if c in (0x24, 0x27, 0x2B, 0x2F, 0x33): return   # LSB echoes of the knobs, ignore
        if c in JOG_CC:
            delta = v - 64                  # the controller sends 64 +/- ticks
            if c == 0x29: delta *= 10
            send(K['jog'], 4, deck, int(delta * jog_scale), float(delta)); jog_moved(deck); return
    elif ch == 4:
        if c == 0x02: msb[(4, 2)] = v; return
        if c == 0x22: analog(K['fxdepth'], 0, (msb.get((4, 2), 0) << 7 | v) / 16383.0); return
    elif ch == 6:
        if c == 0x17: analog(K['color'], 1, v / 127.0); return
        if c == 0x18: analog(K['color'], 2, v / 127.0); return
        if c in (0x37, 0x38): return
        if c == 0x40: send(K['rotary'], 4, 0, v if v < 64 else v - 128, 0.0); return   # relative encoder: 1..63 cw, 127..65 ccw
        if c in MASTER_CC: analog(K[MASTER_CC[c]], 0, v / 127.0); return

found = controllers.detect()
forced = os.environ.get('RX3_CONTROLLER')
if forced:
    ctl_id = forced
elif found:
    ctl_id = found[0][1]
    if len(found) > 1: print('controller-bridge: %d controllers connected, using the first plugged in (%s)' % (len(found), controllers.CONTROLLERS[ctl_id]['name']), flush=True)
else:
    print('controller-bridge: no known DJ controller connected (%s)' % ', '.join(c['name'] for c in controllers.CONTROLLERS.values()), file=sys.stderr); sys.exit(1)
CTL = controllers.CONTROLLERS[ctl_id]
dev = sys.argv[1] if len(sys.argv) > 1 else (controllers.midi_device(found[0][0]) if found else None)
if not dev: print('controller-bridge: %s has no MIDI device' % CTL['name'], file=sys.stderr); sys.exit(1)
print('controller-bridge: %s on %s' % (CTL['name'], dev), flush=True)
DECK_NOTES[CTL['censor']] = 'slip'          # SHIFT+PLAY (censor) drives the RX3's SLIP; the note differs per controller

import threading
try: midi_out = os.open(dev if dev != '-' else os.environ.get('RX3_MIDI_OUT', '/dev/null'), os.O_WRONLY | (0 if dev != '-' else os.O_CREAT | os.O_APPEND), 0o644)   # keep-alive, init, LEDs
except OSError as e: print('controller-bridge: cannot open MIDI out:', e, file=sys.stderr, flush=True); sys.exit(3)
out_lock = threading.Lock()
def midi_write(b):
    with out_lock:
        try: os.write(midi_out, b)
        except OSError as e: print('controller-bridge: MIDI out failed:', e, file=sys.stderr, flush=True); os._exit(3)
if CTL['init']: midi_write(CTL['init'])
if CTL['keepalive']:
    def keepalive():
        while True: midi_write(CTL['keepalive']); time.sleep(CTL['keepalive_period'])
    threading.Thread(target=keepalive, daemon=True).start()

# ---- LED feedback (the firmware's own panel LEDs are not available to us yet, so we mirror what we know locally) ----
def led(status, note, on):
    midi_write(bytes([status, note, 0x7F if on else 0x00]))

PAD_MODES = [0x1B, 0x6D, 0x20, 0x22, 0x1E, 0x69, 0x6F]      # hot cue, beat loop, beat jump, sampler, pad fx1, keyboard, key shift
# The FLX4 pads send a different note range per pad mode, and only switch range when the host lights that mode's
# button, so every mode press relights the mode buttons. Three modes exist on the RX3 too; the rest have no RX3
# equivalent and their pads are ignored.
PAD_LAYER = {0x1B: 0x00, 0x6D: 0x60, 0x20: 0x20}             # FLX4 pad note base for the modes the RX3 has
RX3_PAD_MODE = {0x1B: ('hotcue', 14), 0x6D: ('autobeatloop', 15), 0x20: ('beatjump', 17)}   # RX3 key, firmware LED id
pad_mode = {1: 0x1B, 2: 0x1B}
rx3_mode = {1: 0x1B, 2: 0x1B}       # last RX3 mode we set; only used while the firmware LED table is unavailable
def show_pad_mode(deck):
    for n in PAD_MODES: led(0x90 + deck - 1, n, n == pad_mode[deck])
def select_pad_mode(deck, n):
    pad_mode[deck] = n; show_pad_mode(deck)
    for k in [k for k in led_sent if k[0] == 0x97 + 2 * (deck - 1)]: del led_sent[k]   # the new layer's pads: resend
    if n not in RX3_PAD_MODE: return
    key, led_id = RX3_PAD_MODE[n]
    # Press the RX3's own mode key only on positive evidence that it is in another mode (its mode lights are blank for
    # a moment at start-up), or in GATE CUE when HOT CUE is wanted: its HOT CUE key toggles HOT CUE <-> GATE CUE, and
    # the HOT CUE light is white in HOT CUE and yellow-green in GATE CUE.
    if firmware_leds.read():
        if firmware_leds.lit(led_id, deck, steady=True):
            if led_id != 14 or firmware_leds.rgb(14, deck) == (255, 255, 255): return
        elif not any(firmware_leds.lit(other, deck, steady=True) for _, other in RX3_PAD_MODE.values() if other != led_id): return
    elif rx3_mode[deck] == n: return
    rx3_mode[deck] = n
    press(K[key], deck, True); press(K[key], deck, False)

# ---- Button lights from the firmware (control-shim.c publishes its panel LED table to /tmp/rx3-leds) ----
# Per LED id and deck: present, state (1 lit, 2 blinking), brightness (0 full, 1 dim), r, g, b, blink period in ms.
# The FLX4's buttons and pads are single-colour on/off; "dim" means off (the FLX4 keeps its own low backlight).
class FirmwareLeds:
    PATH = ROOT + '/tmp/rx3-leds'
    def __init__(self): self.data = None; self.seq = None
    def read(self):
        try:
            with open(self.PATH, 'rb') as f: d = f.read(16 + 64 * 16 + 8)
        except OSError: self.data = None; return False
        if len(d) < 16 + 64 * 16 or d[:4] != b'RXL1': self.data = None; return False
        self.data = d; return True
    def level_db(self, deck):
        # Mixer channel level in dB (DjEngineIF::getInputChLevelMono), published after the LED table.
        o = 16 + 64 * 16 + 4 * (deck - 1)
        return struct.unpack_from('<i', self.data, o)[0] if len(self.data) >= o + 4 else None
    def rgb(self, led_id, deck):
        o = 16 + (led_id * 2 + deck - 1) * 8; return tuple(self.data[o + 3:o + 6])
    def lit(self, led_id, deck, steady=False, now=None):
        o = 16 + (led_id * 2 + deck - 1) * 8; e = self.data[o:o + 8]
        if not e[0] or e[2] != 0: return False
        if e[1] == 2 and not steady:
            period = e[6] | e[7] << 8 or 500
            return ((now or time.time()) * 1000) % period < period / 2
        return e[1] in (1, 2)
firmware_leds = FirmwareLeds()
DECK_LEDS = {1: 0x0B, 2: 0x0C, 4: 0x58, 7: 0x10, 8: 0x11, 50: 0x54}   # play, cue, beat sync, loop in, loop out, headphone cue
MASTER_CUE_LED = 51                                                    # mixer channel (0x96) note 0x63
# VU meters: the RX3's own channel meter steps (ui::Mixer::MonoLvMeter::calcLedValue: 0-11 segments for -24..+14 dB,
# table from the firmware), sent as 0-127 on CC 0x02 like Mixxx does; the FLX4 picks its segments from that.
def meter_step(db):
    if db is None or db < -24: return 0
    if db > 14: return 11
    return (1, 1, 1, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 2, 2, 3, 3, 3, 4, 4, 4, 5, 5, 5, 6, 6, 6, 7, 7, 7, 8, 8, 8,
            9, 9, 9, 10, 10, 10)[db + 24]
led_sent = {}
def led_set(status, note, on):
    if led_sent.get((status, note)) != on: led_sent[(status, note)] = on; led(status, note, on)
def led_loop():
    time.sleep(1.0)
    for d in (1, 2): show_pad_mode(d)
    # Start both decks in HOT CUE, matching the FLX4's pad layer, once the firmware's LEDs say what the RX3 is in.
    for i in range(150):
        if firmware_leds.read() and any(firmware_leds.lit(m, 1, steady=True) for m in (14, 15, 17)): break
        time.sleep(0.1)
    if firmware_leds.data:
        print('controller-bridge: button lights follow the firmware (%s)' % firmware_leds.PATH, flush=True)
        for d in (1, 2): select_pad_mode(d, 0x1B)
    else: print('controller-bridge: no firmware LED table (old player shim?): only the pad mode buttons are lit', flush=True)
    while True:
        time.sleep(0.03)
        if not firmware_leds.read(): continue
        now = time.time()
        led_set(0x96, 0x63, firmware_leds.lit(MASTER_CUE_LED, 1, now=now))
        for d in (1, 2):
            vu = round(meter_step(firmware_leds.level_db(d)) * 127 / 11)
            if led_sent.get(('vu', d)) != vu: led_sent[('vu', d)] = vu; midi_write(bytes([0xB0 + d - 1, 0x02, vu]))
            for led_id, n in DECK_LEDS.items(): led_set(0x90 + d - 1, n, firmware_leds.lit(led_id, d, now=now))
            base = PAD_LAYER.get(pad_mode[d])
            if base is None: continue
            for i in range(8): led_set(0x97 + 2 * (d - 1), base + i, firmware_leds.lit(18 + i, d, now=now))
threading.Thread(target=led_loop, daemon=True).start()

# ---- Sound Color FX: the RX3 needs an effect selected before the per-channel COLOR knobs do anything ----
# Engine effect slots (SoundColorFxManager ctor order = type number): 1 FILTER, 2 NOISE, 3 SWEEP, 4 DUB ECHO, 5 SPACE,
# 6 CRUSH. Keys: 0x50a6 filter, 0x50a4 noise, 0x50a3 sweep, 0x50a2 dub echo, 0x50a1 space, 0x50a5 crush (pressing the
# active effect's key again switches it off). Start on FILTER; the SMART CFX button cycles from there.
CFX = [0x50a6, 0x50a1, 0x50a2, 0x50a3, 0x50a4, 0x50a5]
cfx_index = 0
def select_cfx():
    for ch in (1, 2):                       # effect type is per mixer channel; the key must carry the channel
        press(CFX[cfx_index], ch, True); time.sleep(0.05); press(CFX[cfx_index], ch, False)
threading.Timer(2.0, select_cfx).start()
beatfx_index = 0
FXCH_MASTER = int(os.environ.get('RX3_FXCH_MASTER', '5'))

buf = b''
with open(dev if dev != '-' else 0, 'rb', buffering=0) as f:
    while True:
        try: data = f.read(64)
        except OSError as e: print('controller-bridge: MIDI in failed:', e, file=sys.stderr, flush=True); sys.exit(3)
        if not data: time.sleep(.01); continue
        buf += data
        while buf:
            s = buf[0]
            if s < 0x80: buf = buf[1:]; continue          # skip stray data bytes
            if s >= 0xF0: buf = buf[1:]; continue          # ignore system messages
            if len(buf) < 3: break
            d1, d2 = buf[1], buf[2]; buf = buf[3:]
            kind = s & 0xF0
            if LOG: print('%s midi %02x %02x %02x' % (time.strftime('%H:%M:%S'), s, d1, d2), flush=True)
            if kind in (0x80, 0x90): note(s, d1, d2)
            elif kind == 0xB0: cc(s, d1, d2)
