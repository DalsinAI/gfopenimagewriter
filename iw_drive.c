/* GFOpenImageWriter: which drives it offers, how it names them and what it refuses (see iw_drive.h).
 * MIT, Copyright (c) 2026 AmiCradle Team. */
#include "iw_drive.h"
#include <stdio.h>
#include <string.h>

static int lower(int c) { return c >= 'A' && c <= 'Z' ? c + 32 : c; }

int iw_is_acscsi(const char *dev)
{
    const char *want = "acscsi.device";
    int i;
    for (i = 0; want[i]; i++) if (lower((unsigned char)dev[i]) != want[i]) return 0;
    return dev[i] == 0;
}

static int listed(const uint32_t *u, int n, uint32_t x)
{
    int i;
    for (i = 0; i < n; i++) if (u[i] == x) return 1;
    return 0;
}

int iw_units(const char *dev, const uint32_t *mounted, int nmounted, uint32_t *out, int max)
{
    int acs = iw_is_acscsi(dev), n = 0, i;
    uint32_t last = acs ? IW_MAX_UNITS - 1 : 7, x;
    for (x = 0; x <= last && n < max; x++)
        if (!acs || x != IW_ACSCSI_HOST_ID) out[n++] = x;
    for (i = 0; i < nmounted && n < max; i++) {
        x = mounted[i];
        if (acs && (x == IW_ACSCSI_HOST_ID || x >= IW_MAX_UNITS)) continue;
        if (!listed(out, n, x)) out[n++] = x;
    }
    return n;
}

/* Appends len bytes of fixed-width SCSI text to out, trimmed, after a space if out has something */
static void put_field(char *out, int size, const uint8_t *s, int len)
{
    int a = 0, b = len, o = (int)strlen(out);
    while (a < b && (s[a] == ' ' || s[a] == 0)) a++;
    while (b > a && (s[b - 1] == ' ' || s[b - 1] == 0)) b--;
    if (a == b) return;
    if (o && o < size - 1) out[o++] = ' ';
    while (a < b && o < size - 1) { uint8_t c = s[a++]; out[o++] = c >= 32 && c < 127 ? (char)c : '?'; }
    out[o] = 0;
}

void iw_inquiry_model(const uint8_t *inq, int n, char *out, int size)
{
    out[0] = 0;
    if (n < 36 || (inq[0] & 0xe0) == 0x60) return;
    put_field(out, size, inq + 8, 8);
    put_field(out, size, inq + 16, 16);
    put_field(out, size, inq + 32, 4);
}

void iw_vpd_serial(const uint8_t *vpd, int n, char *out, int size)
{
    int len;
    out[0] = 0;
    if (n < 4 || vpd[1] != 0x80) return;
    len = vpd[3];
    if (len > n - 4) len = n - 4;
    put_field(out, size, vpd + 4, len);
}

void iw_size_text(uint64_t bytes, char *out, int size)
{
    unsigned long mb = (unsigned long)(bytes >> 20);
    if (bytes >= (10ULL << 30)) snprintf(out, size, "%lu GB", (unsigned long)(bytes >> 30));
    else if (mb >= 1) snprintf(out, size, "%lu.%lu MB", mb, (unsigned long)(((bytes >> 10) & 1023) * 10 / 1024));
    else snprintf(out, size, "%lu KB", (unsigned long)(bytes >> 10));
}

const char *iw_refuse(const iw_drive *d, const char *dev, uint64_t image_bytes, char *buf, int size)
{
    uint64_t total = d->blocks * d->block_bytes;
    char a[24], b[24];
    if (!image_bytes) return "The image is empty, or its size can't be read.";
    if (iw_is_acscsi(dev) && d->unit == IW_ACSCSI_BOOT_UNIT)
        return "That is the boot drive (unit 0): Image Writer never writes it.";
    if (d->system) return "That drive holds the volume the system started from: it can't be written while it runs.";
    if (image_bytes > total) {
        iw_size_text(image_bytes, a, sizeof a);
        iw_size_text(total, b, sizeof b);
        snprintf(buf, size, "The image (%s) is bigger than the drive (%s).", a, b);
        return buf;
    }
    return NULL;
}

/* "Work:, Games:" into out from o on; returns the new length */
static int put_volumes(const iw_drive *d, char *out, int size, int o)
{
    int i;
    for (i = 0; i < d->nparts && o < size - 1; i++) {
        int w = snprintf(out + o, size - o, "%s%s:", i ? ", " : "", d->parts[i]);
        o = w < 0 || o + w >= size ? size - 1 : o + w;
    }
    return o;
}

static int put(char *out, int size, int o, const char *fmt, const char *a)
{
    int w;
    if (o >= size - 1) return o;
    w = snprintf(out + o, size - o, fmt, a);
    return w < 0 || o + w >= size ? size - 1 : o + w;
}

void iw_label(const iw_drive *d, char *out, int size)
{
    char s[24];
    int o = snprintf(out, size, "Unit %lu: ", (unsigned long)d->unit);
    if (o >= size) o = size - 1;
    if (d->model[0]) o = put(out, size, o, "%s, ", d->model);
    if (d->serial[0]) o = put(out, size, o, "serial %s, ", d->serial);
    iw_size_text(d->blocks * d->block_bytes, s, sizeof s);
    o = put(out, size, o, "%s.", s);
    if (d->nparts) { o = put(out, size, o, "%s", " Volumes: "); o = put_volumes(d, out, size, o); }
    else o = put(out, size, o, "%s", " No volume mounted");
    if (d->system) put(out, size, o, "%s", " (the system's)");
}

void iw_confirm(const iw_drive *d, const char *dev, uint64_t image_bytes, char *out, int size)
{
    char s[24], u[16];
    int o;
    iw_size_text(image_bytes, s, sizeof s);
    snprintf(u, sizeof u, "%lu", (unsigned long)d->unit);
    o = snprintf(out, size, "Write %s to the drive at %s unit %s", s, dev, u);
    if (o >= size) o = size - 1;
    if (d->model[0] || d->serial[0]) {
        o = put(out, size, o, "%s", " (");
        if (d->model[0]) o = put(out, size, o, "%s", d->model);
        if (d->serial[0]) o = put(out, size, o, d->model[0] ? ", serial %s" : "serial %s", d->serial);
        o = put(out, size, o, "%s", ")");
    }
    o = put(out, size, o, "%s", "?\n\nEverything on the drive is lost.");
    if (d->nparts) { o = put(out, size, o, "%s", " The volumes on it: "); o = put_volumes(d, out, size, o); o = put(out, size, o, "%s", "."); }
    else put(out, size, o, "%s", " No volume on it is mounted.");
}
