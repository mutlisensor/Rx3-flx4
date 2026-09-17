/* Layout of the chroot's /dev/fb0 file, shared by the player shim (fbshim.c, writer) and the presenter
   (fb-present.c, reader). The firmware mmaps the first RX3_FB_BYTES and draws into them whenever it likes;
   the shim, when the firmware signals the end of a frame (FBIO_WAITFORVSYNC), copies that picture into the
   snapshot and bumps the sequence word: odd while the copy runs, even once it is complete, then wakes any
   waiter with FUTEX_WAKE. Readers work from the snapshot between two equal, even readings of the sequence. */
#ifndef RX3_FB_FRAME_H
#define RX3_FB_FRAME_H
#define RX3_FB_W 1280
#define RX3_FB_H 800
#define RX3_FB_BYTES (RX3_FB_W*RX3_FB_H*4)
#define RX3_FB_SNAP RX3_FB_BYTES                 /* offset of the completed-frame snapshot */
#define RX3_FB_SEQ (2*RX3_FB_BYTES)              /* offset of the 32-bit sequence word */
#define RX3_FB_FILE_BYTES (2*RX3_FB_BYTES+4096)  /* mount-rx3.sh sizes the file to this */
#endif
