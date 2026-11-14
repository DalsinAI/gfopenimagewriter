/* Host tests for iw_drive.c: the drives GFOpenImageWriter offers, their names and what it refuses.
 *   cmake -S . -B build && cmake --build build && ctest --test-dir build
 * MIT, Copyright (c) 2026 AmiCradle Team. */
#include "iw_drive.h"
#include <stdio.h>
#include <string.h>

static int fails, checks;
#define CHECK(c, what) do { checks++; if (c) printf("ok   %s\n", what); else { fails++; printf("FAIL %s (line %d)\n", what, __LINE__); } } while (0)

static int has(const uint32_t *u, int n, uint32_t x) { for (int i = 0; i < n; i++) if (u[i] == x) return 1; return 0; }

static void inq_fill(uint8_t *inq, const char *v, const char *p, const char *r)
{
    memset(inq, ' ', 36);
    inq[0] = 0; inq[1] = 0; inq[2] = 2; inq[3] = 2; inq[4] = 31; inq[5] = inq[6] = inq[7] = 0;
    memcpy(inq + 8, v, strlen(v)); memcpy(inq + 16, p, strlen(p)); memcpy(inq + 32, r, strlen(r));
}

static iw_drive drive(uint32_t unit, uint64_t bytes)
{
    iw_drive d;
    memset(&d, 0, sizeof d);
    d.unit = unit; d.block_bytes = 512; d.blocks = bytes / 512;
    return d;
}

int main(void)
{
    uint32_t u[IW_LISTED], mounted[4];
    int n;
    char t[512], b[160];
    const char *why;

    /* the units: acscsi.device's drives are 0-15, never 7 */
    n = iw_units("acscsi.device", NULL, 0, u, IW_LISTED);
    CHECK(n == 15 && has(u, n, 0) && has(u, n, 15) && !has(u, n, 7), "acscsi.device: units 0-15, 15 of them, never 7");
    mounted[0] = 7; mounted[1] = 16; mounted[2] = 3;
    n = iw_units("acscsi.device", mounted, 3, u, IW_LISTED);
    CHECK(n == 15 && !has(u, n, 7) && !has(u, n, 16), "acscsi.device: a mount entry's Unit=7 or 16 adds nothing");
    n = iw_units("scsi.device", NULL, 0, u, IW_LISTED);
    CHECK(n == 8 && has(u, n, 0) && has(u, n, 7) && !has(u, n, 8), "scsi.device: units 0-7, as before");
    mounted[0] = 1; mounted[1] = 20; mounted[2] = 1;
    n = iw_units("scsi.device", mounted, 3, u, IW_LISTED);
    CHECK(n == 9 && has(u, n, 20), "scsi.device: a mounted unit past 7 (20) is added once");
    CHECK(iw_is_acscsi("acscsi.device") && iw_is_acscsi("ACSCSI.device") && !iw_is_acscsi("scsi.device"), "acscsi.device known in any case");
    n = iw_units("acscsi.device", NULL, 0, u, 4);
    CHECK(n == 4, "the list stops at max");

    /* the model and the serial */
    uint8_t inq[36];
    inq_fill(inq, "SanDisk", "Ultra Fit", "1.00");
    iw_inquiry_model(inq, 36, b, sizeof b);
    CHECK(!strcmp(b, "SanDisk Ultra Fit 1.00"), "INQUIRY: vendor, product and revision, trimmed");
    inq_fill(inq, "AmigaChr", "ACStorage ID 12 ", "1.3");
    iw_inquiry_model(inq, 36, b, sizeof b);
    CHECK(!strcmp(b, "AmigaChr ACStorage ID 12 1.3"), "INQUIRY: acscsi.device's drive by its ID");
    iw_inquiry_model(inq, 20, b, sizeof b);
    CHECK(b[0] == 0, "INQUIRY: too short gives no model");
    inq[0] = 0x7f;
    iw_inquiry_model(inq, 36, b, sizeof b);
    CHECK(b[0] == 0, "INQUIRY: no device there (qualifier 3) gives no model");
    uint8_t vpd[16] = { 0, 0x80, 0, 8, ' ', '4', 'C', '5', '3', '0', '0', ' ', ' ' };
    iw_vpd_serial(vpd, 12, b, sizeof b);
    CHECK(!strcmp(b, "4C5300"), "page $80: the serial, trimmed");
    vpd[1] = 0x83;
    iw_vpd_serial(vpd, 12, b, sizeof b);
    CHECK(b[0] == 0, "a page that isn't $80 gives no serial");
    vpd[1] = 0x80; vpd[3] = 40;
    iw_vpd_serial(vpd, 12, b, sizeof b);
    CHECK(!strcmp(b, "4C5300"), "page $80 longer than what came back: only what came back");

    /* sizes, as 1.0 gave them */
    iw_size_text(512 * 1024, b, sizeof b);  CHECK(!strcmp(b, "512 KB"), "512 KB");
    iw_size_text(901120, b, sizeof b);      CHECK(!strcmp(b, "880 KB"), "880 KB");
    iw_size_text(12900000, b, sizeof b);    CHECK(!strcmp(b, "12.3 MB"), "12.3 MB");
    iw_size_text(32ULL << 30, b, sizeof b); CHECK(!strcmp(b, "32 GB"), "32 GB");

    /* refusals */
    iw_drive d = drive(3, 32ULL << 30);
    CHECK(iw_refuse(&d, "acscsi.device", 1 << 20, b, sizeof b) == NULL, "a 1 MB image to a 32 GB drive on unit 3: allowed");
    why = iw_refuse(&d, "acscsi.device", 0, b, sizeof b);
    CHECK(why && strstr(why, "empty"), "an empty image: refused");
    d = drive(0, 32ULL << 30);
    why = iw_refuse(&d, "acscsi.device", 1 << 20, b, sizeof b);
    CHECK(why && strstr(why, "boot drive") && strstr(why, "unit 0"), "acscsi.device unit 0, the boot drive: refused");
    CHECK(iw_refuse(&d, "scsi.device", 1 << 20, b, sizeof b) == NULL, "scsi.device unit 0 with the system elsewhere: allowed");
    d = drive(2, 32ULL << 30); d.system = 1;
    why = iw_refuse(&d, "scsi.device", 1 << 20, b, sizeof b);
    CHECK(why && strstr(why, "system started from"), "the drive SYS: is on: refused");
    d = drive(5, 1 << 20);
    why = iw_refuse(&d, "acscsi.device", 2 << 20, b, sizeof b);
    CHECK(why && strstr(why, "bigger than the drive") && strstr(why, "2.0 MB") && strstr(why, "1.0 MB"), "an image bigger than the drive: refused, with both sizes");
    CHECK(iw_refuse(&d, "acscsi.device", 1 << 20, b, sizeof b) == NULL, "an image exactly the drive's size: allowed");

    /* the drive's line and the question: the drive by unit, model and serial; volumes only as what is lost */
    d = drive(3, 32ULL << 30);
    strcpy(d.model, "SanDisk Ultra Fit 1.00"); strcpy(d.serial, "4C5300");
    strcpy(d.parts[0], "DH4"); strcpy(d.parts[1], "Games"); d.nparts = 2;
    iw_label(&d, t, sizeof t);
    CHECK(!strcmp(t, "Unit 3: SanDisk Ultra Fit 1.00, serial 4C5300, 32 GB. Volumes: DH4:, Games:"), "the line: unit, model, serial, size, volumes");
    iw_confirm(&d, "acscsi.device", 1 << 20, t, sizeof t);
    CHECK(strstr(t, "acscsi.device unit 3") && strstr(t, "SanDisk Ultra Fit 1.00") && strstr(t, "serial 4C5300")
          && strstr(t, "lost") && strstr(t, "DH4:, Games:"), "the question names the drive and the volumes lost");
    CHECK(strncmp(t, "Write 1.0 MB to the drive at acscsi.device unit 3 (", 51) == 0, "the target is the drive, never a DH name");
    d.nparts = 0; d.serial[0] = 0; d.model[0] = 0;
    iw_label(&d, t, sizeof t);
    CHECK(!strcmp(t, "Unit 3: 32 GB. No volume mounted"), "no model, serial or volume");
    iw_confirm(&d, "acscsi.device", 1 << 20, t, sizeof t);
    CHECK(strstr(t, "No volume on it is mounted") != NULL, "the question says when no volume is mounted from it");
    d.system = 1;
    iw_label(&d, t, sizeof t);
    CHECK(strstr(t, "(the system's)") != NULL, "the system's drive is marked");
    for (int i = 0; i < IW_MAX_PARTS; i++) { snprintf(d.parts[i], sizeof d.parts[i], "AVeryLongVolumeName%02d", i); }
    d.nparts = IW_MAX_PARTS;
    iw_label(&d, b, 40);
    CHECK(strlen(b) < 40, "a long line is cut to the room it has");
    iw_confirm(&d, "acscsi.device", 1 << 20, t, 64);
    CHECK(strlen(t) < 64, "a long question is cut to the room it has");

    printf("%s: %d of %d\n", fails ? "FAILED" : "PASSED", checks - fails, checks);
    return fails != 0;
}
