# GFOpenImageWriter on a real USB stick and SD card

These are the steps for Team 5 to run on real hardware. **Every write to a real drive needs Dale's go first.** Use a stick or card that holds nothing anyone wants. Never use Dale's TIS-MIG SSD (2507E9A6948E). The 500 GB test SSD (2519E9BCC602) is the only SSD writes may go to.

## What you need

- An instance or the card running acscsi.device 1.6 or later, with GFOpenImageWriter 1.1 in `SYS:Tools/ImageWriter`.
- A USB stick of 1 GB or more, and an SD card of 1 GB or more in a USB reader. Note each one's model and serial from the host first (`lsblk -o NAME,MODEL,SERIAL,SIZE`).
- On the host, the drive passed to the instance as its own unit (`drives/<name>/drive.json` with its unit and serial), and not unit 0.
- A test image: `dd if=/dev/urandom of=test.img bs=1M count=64`, with its SHA-256 noted. Copy it to `RAM:test.img` in the guest.

## Steps, for each of the stick and the card

1. **The list.** Start ImageWriter and choose acscsi.device. Write down every line in the drive list.
   - Pass: the drive shows on its own unit with the right size, and with the volumes mounted from it (if any) named.
   - Pass: no unit 7 is listed, and unit 0 is the boot drive.
2. **The refusals.**
   - Choose unit 0 and press Write image to drive. Pass: it refuses ("That is the boot drive (unit 0)") before asking anything.
   - Choose an image bigger than the drive. Pass: it refuses, giving both sizes.
3. **Write, with Read it back to check on.** Choose `RAM:test.img` and the drive's unit. Read the question.
   - Pass: the question names acscsi.device, the unit, and the volumes that will be lost. It never names the target by a DH name.
   - Press Write. Pass: "Written and checked", with no error. Write down how long it took.
4. **Check on the host.** Stop the instance, or unbind the drive. Then run `head -c 67108864 /dev/sdX | sha256sum`. Pass: the result is the image's SHA-256.
5. **Read back in the guest.** Bind the drive again. Use Read drive to image to save it to `RAM:back.img` (or to a volume with room). Then on the host compare the first 64 MB with `test.img`. Pass: they are the same. Write down how long it took.
6. **Stop.** Start a write and press Stop after the first few pieces. Pass: it stops between pieces and says "Stopped. The drive holds part of the image." The drive's volumes come back afterwards (they were inhibited while it ran).
7. **Afterwards.** Take the stick or card out and put it back (or reboot). Pass: nothing on the boot drive changed (`acfstest DH0:` as before).

## What to send back

For each drive, send: its model and serial, the list lines, pass or fail for each step, the two timings, and anything ImageWriter said that doesn't match these steps.
