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
/* In the same page as the sequence word: a marker, then for each row the sequence value of the last frame that changed
   it. The shim copies only changed rows into the snapshot, so a reader that last took frame S needs only the rows whose
   value is above S (and no longer has to scan the whole picture to find them). */
#define RX3_FB_ROWS_MAGIC_OFF (RX3_FB_SEQ+4)
#define RX3_FB_ROWS_MAGIC 0x53574f52u            /* "ROWS" */
#define RX3_FB_ROWSEQ (RX3_FB_SEQ+64)            /* u32[RX3_FB_H] */
#endif
