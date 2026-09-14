#!/bin/bash
# Build a small vfat image with generated WAV tracks for headless testing (runs as root).
set -e
. "$(dirname "$(readlink -f "$0")")/rx3-env.sh"
IMG=$RX3_USERHOME/rx3-test-usb.img
# The heredoc is quoted on purpose (Python code), so paths come in through the environment, not by expansion.
RX3_USERHOME="$RX3_USERHOME" python3 - <<'PY'
import struct,math,wave,os
d=os.path.join(os.environ['RX3_USERHOME'],'test-tracks'); os.makedirs(d,exist_ok=True)
for name,freq,secs in (('Tone A 440',440,60),('Tone B 660',660,60)):
    w=wave.open(os.path.join(d,'%s.wav'%name),'wb'); w.setnchannels(2); w.setsampwidth(2); w.setframerate(44100)
    frames=bytearray()
    for i in range(44100*secs):
        beat=1.0 if (i%(44100*60//128))<3000 else 0.3   # 128 BPM click envelope
        v=int(12000*beat*math.sin(2*math.pi*freq*i/44100)); frames+=struct.pack('<hh',v,v)
    w.writeframes(bytes(frames)); w.close()
PY
rm -f $IMG; dd if=/dev/zero of=$IMG bs=1M count=64 status=none; mkfs.vfat -n RX3TEST $IMG >/dev/null
mkdir -p /mnt/rx3-img && mount -o loop $IMG /mnt/rx3-img && mkdir -p "/mnt/rx3-img/Contents/Test Album" && cp $RX3_USERHOME/test-tracks/*.wav "/mnt/rx3-img/Contents/Test Album/" && umount /mnt/rx3-img
echo "image ready: $IMG"
