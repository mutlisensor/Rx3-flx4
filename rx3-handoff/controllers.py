#!/usr/bin/env python3
"""Known DJ controllers and how to find the one that is plugged in.

Every supported controller is a Pioneer/AlphaTheta two-deck unit of the DDJ-400 family: they share one MIDI layout
(deck buttons on channels 0/1, pads on 7-10, Beat FX on 4/5, mixer on 6) and a class-compliant 4-channel sound card
(master on 1/2, headphones on 3/4). What differs is small and lives in the table below.

Detection uses the sound card's USB id (/proc/asound/cardN/usbid), falling back to the card name. Cards are
enumerated in the order they were plugged in, so with two controllers attached the first one wins.

CLI:  controllers.py detect        -> "card=2 id=flx4 name=DDJ-FLX4 alsa=DDJFLX4" for the controller in use (exit 1: none)
      controllers.py detect --all  -> one line per connected controller, priority order
      controllers.py usbids        -> the USB ids, for udev rules
"""
import glob, os, sys

SYSEX_FLX4_KEEPALIVE = bytes([0xF0, 0x00, 0x40, 0x05, 0x00, 0x00, 0x04, 0x05, 0x00, 0x50, 0x02, 0xF7])
SYSEX_DDJ400_INIT    = bytes([0xF0, 0x00, 0x40, 0x05, 0x00, 0x00, 0x02, 0x06, 0x00, 0x03, 0x01, 0xF7])

CONTROLLERS = {
    'flx4': dict(
        name='DDJ-FLX4', usb=('2b73:0045',), alsa_hint='FLX4',
        # Only reports controls (and keeps its audio path alive) while the host polls it every 200 ms (Mixxx: "reverse
        # engineered with Wireshark").
        keepalive=SYSEX_FLX4_KEEPALIVE, keepalive_period=0.2, init=None,
        # BEAT FX CH SELECT: (MIDI channel, note) -> RX3 value 0 CH1, 1 CH2; anything else on those notes = MASTER.
        fxch={(4, 0x10): 0, (5, 0x11): 1},
        censor=0x0E,                 # SHIFT+PLAY on channels 0/1
    ),
    'ddj400': dict(
        name='DDJ-400', usb=('2b73:0017',), alsa_hint='DDJ400',
        # No keep-alive needed (Mixxx runs it without one); it sends this status request once at start-up.
        keepalive=None, keepalive_period=0, init=SYSEX_DDJ400_INIT,
        fxch={(4, 0x10): 0, (4, 0x11): 1, (4, 0x14): 'master'},
        censor=0x47,
    ),
}

def _read(p):
    try: return open(p).read().strip()
    except OSError: return ''

def detect(asound='/proc/asound'):
    """Connected known controllers as [(card_index, controller_id, alsa_card_id)], first-plugged first."""
    found = []
    for d in glob.glob(os.path.join(asound, 'card[0-9]*')):
        try: idx = int(os.path.basename(d)[4:])
        except ValueError: continue
        usbid, alsa = _read(os.path.join(d, 'usbid')).lower(), _read(os.path.join(d, 'id'))
        for cid, c in CONTROLLERS.items():
            if usbid in c['usb'] or (not usbid and c['alsa_hint'] in alsa):
                found.append((idx, cid, alsa)); break
    return sorted(found)

def midi_device(card_index):
    devs = sorted(glob.glob('/dev/snd/midiC%dD*' % card_index))
    return devs[0] if devs else None

if __name__ == '__main__':
    a = sys.argv[1:]
    if a[:1] == ['usbids']:
        print(' '.join(u for c in CONTROLLERS.values() for u in c['usb'])); sys.exit(0)
    if a[:1] == ['names']:
        print(', '.join(c['name'] for c in CONTROLLERS.values())); sys.exit(0)
    if a[:1] == ['detect']:
        found = detect(os.environ.get('RX3_ASOUND', '/proc/asound'))
        if not found: sys.exit(1)
        for idx, cid, alsa in (found if '--all' in a else found[:1]):
            print('card=%d id=%s name=%s alsa=%s' % (idx, cid, CONTROLLERS[cid]['name'], alsa))
        sys.exit(0)
    print(__doc__); sys.exit(2)
