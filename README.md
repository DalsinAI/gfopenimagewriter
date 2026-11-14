# GFOpenImageWriter

Disk images to whole drives, and drives to images, for GFOS. CD, DVD and Blu-ray writing are planned.

MIT (see `LICENSE`). Part of AmiCradle.

`SYS:Tools/ImageWriter` writes a disk image to a whole drive, block for block, and reads a drive into an image. It's for SD and CF cards, USB sticks and hard drives, and works with `.img`, `.iso`, `.hdf` or any raw image. Its window is GFOpenLayout UI. Dale, 4 Nov 2026: "then tackle gfopenexchange, hd toolbox, imager write...".

- **A drive is a unit on a device**, shown by its model and serial (INQUIRY and its page $80) and its size. On acscsi.device (GFOS's drives) the units are 0 to 15; 7 is the host adapter and is never listed. The volumes mounted from a drive are shown only as what a write would lose: a DH name never picks the drive (1.1, the storage model).
- **Write image to drive** writes the image you chose to the drive you chose.
  - It asks first, naming the drive by device, unit, model and serial, and the volumes on it that will be lost.
  - It refuses acscsi.device's boot drive (unit 0), the drive that holds the volume the system started from, and any drive smaller than the image.
  - While it writes, the drive's mounted partitions are inhibited, so no handler touches the drive. They are given back afterwards.
  - With **Read it back to check** on (the default), each 256 KB piece is read again and compared.
  - A last piece that isn't a whole number of blocks is filled out with zeros.
- **Read drive to image** saves the whole drive to a file.
- **Stop** stops either one between pieces.
- Offsets past 4 GB go through NSCMD_TD_READ64/WRITE64 when the device has them, and TD64 otherwise, as GFOpenFormat does.
- The devices listed are `DEVICE` (a tool type or a command-line argument; acscsi.device when it is there, else scsi.device) and every device a mounted drive uses, apart from trackdisk.device. The units listed are 0 to 15 on acscsi.device (never 7), 0 to 7 on any other device, plus those mounted volumes use.

Images are read through dos.library's 32-bit sizes, so an image can be up to 4 GB. Compressed images (`.gz`, `.xz`) and burning CDs are not done.

## Tests

`iw_drive.c` (the units offered, the model and serial, the refusals and the question) has host tests, which CI runs:

    cmake -S . -B build && cmake --build build && ctest --test-dir build

1.0's lab run, before the move to this repository: in lab t3-openpaint-20261031 (a copy of Instance-32) I mounted RAD: (ramdrive.device unit 0, 880 KB) and ran it with `DEVICE=ramdrive.device`.

1. I wrote a 317,580-byte file (GFOpenHDToolBox) to the drive with checking on. It was written and checked, and the last piece was filled out to whole blocks.
2. Read drive to image gave `drive.img`, 901,120 bytes, with the program's `$VER` in it.
3. I wrote `drive.img` back to the drive with checking on, then read the drive to `drive2.img`. Both files have SHA-256 `10728cb4...91de2f5`.

## Build

    sh build-amiga.sh

It builds with the os32-gcc16 stove, 68040 soft float and `-Werror`, and writes `Tools/ImageWriter`. The size is 270,604 bytes, stripped, with GFOpenLayout UI linked in.

## Still to do

- **The drive's model and serial on GFOS.** acscsi.device answers INQUIRY as "AmigaChr ACStorage ID n" and has no page $80, so a GFOS drive shows its unit and size, not the USB stick's or SD card's own model and serial. The host knows them (`drives/<drive>/drive.json` has the serial); acscsi.device needs to pass them on in INQUIRY and page $80. That is a guest and host change, not this repository's.
- **A real drive.** 1.0 was only run against a RAM drive. The steps for a USB stick and an SD card are in `docs/REAL-DRIVE-TEST.md`; Team 5 runs them, and each write to a real drive needs Dale's go.
- **Images over 4 GB.** The image's size is read through dos.library's 32-bit `fib_Size`, so an image can be at most 4 GB. DVD and Blu-ray images need 64-bit sizes.
- **CD, DVD and Blu-ray writing** on USB burners: the design comes first (`designs/GFOPENIMAGEWRITER-OPTICAL-20261114.md` in amicradle-teams).
- **Installing through GFOpenUp**, like the other apps.
