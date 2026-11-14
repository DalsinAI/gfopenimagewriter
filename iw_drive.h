/* GFOpenImageWriter: which drives it offers, how it names them and what it refuses. Plain C, no Amiga headers,
 * so the host tests (tests/test_iw_drive.c) run it as the Amiga build does.
 *
 * The storage model (amicradle-teams DECISION-STORAGE-MODEL-20261113-1736): a unit on acscsi.device is a
 * physical drive (0-15, 0 the boot drive, ID 7 the host adapter) and DH0:, DH1: ... are volumes on it. The
 * target is always a drive: a device and a unit, shown by its model and serial, with the volumes on it named as
 * what will be lost. A DH name never selects a drive.
 *
 * MIT, Copyright (c) 2026 AmiCradle Team. */
#ifndef IW_DRIVE_H
#define IW_DRIVE_H

#include <stdint.h>

#define IW_MAX_UNITS 16                 /* acscsi.device's units, 0-15 */
#define IW_ACSCSI_HOST_ID 7             /* the host adapter's ID on acscsi.device: never a drive */
#define IW_ACSCSI_BOOT_UNIT 0           /* the boot drive on acscsi.device */
#define IW_MAX_PARTS 16
#define IW_LISTED 24                    /* units listed at most */

typedef struct {
    uint32_t unit;
    uint32_t block_bytes;
    uint64_t blocks;
    char model[48];                     /* from INQUIRY: "vendor product revision", or "" */
    char serial[40];                    /* from INQUIRY's page $80, or "" */
    char parts[IW_MAX_PARTS][32];       /* the volumes mounted from it, by their DOS names ("Work") */
    int nparts;
    int system;                         /* SYS: is on it */
} iw_drive;

/* 1 when dev is acscsi.device (the storage model's units) */
int iw_is_acscsi(const char *dev);

/* The units to look at on dev, into out (at most max): acscsi.device's 0-15 except 7; any other device 0-7, as
 * before. Then each mounted unit not already there (a mount entry's Unit=), except acscsi.device's 7 and any
 * past 15. Returns how many. */
int iw_units(const char *dev, const uint32_t *mounted, int nmounted, uint32_t *out, int max);

/* The model from n bytes of INQUIRY data: vendor, product and revision, trimmed and joined by single spaces.
 * "" when there are fewer than 36 bytes or the peripheral qualifier says no device is there. */
void iw_inquiry_model(const uint8_t *inq, int n, char *out, int size);

/* The serial from n bytes of INQUIRY's page $80 (Unit Serial Number), trimmed; "" when it isn't that page */
void iw_vpd_serial(const uint8_t *vpd, int n, char *out, int size);

/* "512 KB", "12.3 MB", "32 GB" */
void iw_size_text(uint64_t bytes, char *out, int size);

/* Why the image can't be written to d on dev, or NULL when it can: an empty image, acscsi.device's boot drive
 * (unit 0), the drive the system started from, or a drive smaller than the image. A refusal that needs sizes is
 * written into buf. */
const char *iw_refuse(const iw_drive *d, const char *dev, uint64_t image_bytes, char *buf, int size);

/* The list's line for d: "Unit 3: SanDisk Ultra 1.00, serial 4C53, 29.8 GB. Volumes: Work:, Games:" */
void iw_label(const iw_drive *d, char *out, int size);

/* The question before a write: the drive by device, unit, model and serial, and the volumes that are lost */
void iw_confirm(const iw_drive *d, const char *dev, uint64_t image_bytes, char *out, int size);

#endif
