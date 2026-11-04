# GFOpenImageWriter

Disk images to whole drives, and drives to images, for GFOS. CD, DVD and Blu-ray writing are planned.

MIT (see `LICENSE`). Part of AmiCradle.

`SYS:Tools/ImageWriter` writes a disk image to a whole drive, block for block, and reads a drive into an image. It's for SD and CF cards, USB sticks and hard drives, and works with `.img`, `.iso`, `.hdf` or any raw image. Its window is GFOpenLayout UI. Dale, 4 Nov 2026: "then tackle gfopenexchange, hd toolbox, imager write...".

- **Write image to drive** writes the image you chose to the drive you chose.
  - It asks first and names the volumes on the drive that will be lost.
  - It refuses the drive that holds the volume the system started from, and any drive smaller than the image.
  - While it writes, the drive's mounted partitions are inhibited, so no handler touches the drive. They are given back afterwards.
  - With **Read it back to check** on (the default), each 256 KB piece is read again and compared.
  - A last piece that isn't a whole number of blocks is filled out with zeros.
- **Read drive to image** saves the whole drive to a file.
- **Stop** stops either one between pieces.
- Offsets past 4 GB go through NSCMD_TD_READ64/WRITE64 when the device has them, and TD64 otherwise, as GFOpenFormat does.
- The devices listed are `DEVICE` (a tool type or a command-line argument; scsi.device when it isn't given) and every device a mounted drive uses, apart from trackdisk.device. The units listed are 0 to 7, plus those mounted drives use. Each unit shows its size and what is mounted on it.

Images are read through dos.library's 32-bit sizes, so an image can be up to 4 GB. Compressed images (`.gz`, `.xz`) and burning CDs are not done.

## Tests

In lab t3-openpaint-20261031 (a copy of Instance-32) I mounted RAD: (ramdrive.device unit 0, 880 KB) and ran it with `DEVICE=ramdrive.device`.

1. I wrote a 317,580-byte file (GFOpenHDToolBox) to the drive with checking on. It was written and checked, and the last piece was filled out to whole blocks.
2. Read drive to image gave `drive.img`, 901,120 bytes, with the program's `$VER` in it.
3. I wrote `drive.img` back to the drive with checking on, then read the drive to `drive2.img`. Both files have SHA-256 `10728cb4...91de2f5`.

## Build

    sh tools/openimagewriter/build-amiga.sh

It builds with the os32-gcc16 stove, 68040 soft float and `-Werror`, and writes `Tools/ImageWriter`. The size is 270,604 bytes, stripped, with GFOpenLayout UI linked in.
