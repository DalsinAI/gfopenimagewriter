/* GFOpenImageWriter 1.0: a disk image written to a whole drive, block for block, and a drive read into an image
 * (4 November 2026), its window in GFOpenLayout UI. For SD and CF cards, USB sticks and hard drives: an .img,
 * .iso, .hdf or any raw image.
 *
 * Choose the image, the device and one of its drives. Write image to drive asks first, naming the volumes on
 * the drive that are lost, and refuses a drive that holds the volume the system started from or one smaller than
 * the image. The drive's mounted partitions are inhibited while it is written (no handler touches it), and given
 * back after. With "Read it back" on, each piece is read again and compared. Read drive to image saves the whole
 * drive to a file. Stop stops either between pieces.
 *
 * Offsets past 4 GB go through the New Style 64-bit commands (NSCMD_TD_READ64/WRITE64) when the device has
 * them, else TD64's, as GFOpenFormat does.
 *
 * Tool types (in its icon, or as KEY=VALUE on the command line): DEVICE=scsi.device, the device chosen first.
 *
 * MIT, Copyright (c) 2026 AmiCradle Team. */

#include <exec/types.h>
#include <exec/memory.h>
#include <exec/io.h>
#include <devices/trackdisk.h>
#include <devices/newstyle.h>
#include <devices/scsidisk.h>
#include <dos/dos.h>
#include <dos/dosextens.h>
#include <dos/filehandler.h>
#include <intuition/intuition.h>
#include <libraries/asl.h>
#include <workbench/workbench.h>
#include <workbench/startup.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <proto/asl.h>
#include <proto/icon.h>
#include <string.h>
#include <stdio.h>
#include <stdint.h>

#include "gfopenlayout_ui.h"
#include "gfopenlayout_ui_amiga.h"

#define NAME "GFOpenImageWriter"
static const char version[] __attribute__((used)) = "$VER: GFOpenImageWriter 1.0 (4.11.2026) MIT, Copyright (c) 2026 AmiCradle Team";
unsigned long __stack = 65536;

#ifndef TD_READ64
#define TD_READ64 24
#define TD_WRITE64 25
#endif

struct Library *IconBase;

#define MAX_DEVICES 16
#define MAX_DRIVES 16
#define MAX_PARTS 16
#define PIECE (256UL * 1024UL)

static void scopy(char *to, const char *from, int size)
{
    int i;
    for (i = 0; i < size - 1 && from[i]; i++) to[i] = from[i];
    to[i] = 0;
}

static char devices[MAX_DEVICES][64];
static int ndevices;

struct drive {
    ULONG unit;
    ULONG block_bytes;
    uint64_t blocks;
    char label[160];
    char parts[MAX_PARTS][32];      /* the DOS devices mounted on it ("DH0") */
    int nparts;
    int system;                     /* SYS: is on it */
};

static struct drive drives[MAX_DRIVES];
static int ndrives, cur_dev = -1, cur_drive = -1;

static olui *ui;
static olui_amiga *glue;
static struct Window *win;
static int quit, busy, stop;

/* ---- the device ------------------------------------------------------------------------------------------- */

struct dio {
    struct MsgPort *mp;
    struct IOStdReq *io;
    int open, nsd64;
};

static int dio_open(struct dio *x, const char *dev, ULONG unit)
{
    struct NSDeviceQueryResult nsq;
    memset(x, 0, sizeof *x);
    if (!(x->mp = CreateMsgPort())) return 0;
    if (!(x->io = (struct IOStdReq *)CreateIORequest(x->mp, sizeof(struct IOExtTD)))) return 0;
    if (OpenDevice((STRPTR)dev, unit, (struct IORequest *)x->io, 0)) return 0;
    x->open = 1;
    memset(&nsq, 0, sizeof nsq);
    x->io->io_Command = NSCMD_DEVICEQUERY;
    x->io->io_Data = &nsq;
    x->io->io_Length = sizeof nsq;
    if (!DoIO((struct IORequest *)x->io) && nsq.nsdqr_SizeAvailable >= 16 && nsq.nsdqr_SupportedCommands) {
        UWORD *c = nsq.nsdqr_SupportedCommands;
        while (*c) { if (*c == NSCMD_TD_WRITE64) x->nsd64 = 1; c++; }
    }
    return 1;
}

static void dio_close(struct dio *x)
{
    if (x->open) CloseDevice((struct IORequest *)x->io);
    if (x->io) DeleteIORequest((struct IORequest *)x->io);
    if (x->mp) DeleteMsgPort(x->mp);
    memset(x, 0, sizeof *x);
}

static int dio_do(struct dio *x, UWORD cmd, uint64_t off, APTR buf, ULONG len)
{
    struct IOStdReq *io = x->io;
    io->io_Data = buf;
    io->io_Length = len;
    io->io_Offset = (ULONG)off;
    io->io_Actual = 0;
    io->io_Command = cmd;
    if (off + len > 0xFFFFFFFFULL) {
        io->io_Actual = (ULONG)(off >> 32);
        io->io_Command = x->nsd64 ? (cmd == CMD_READ ? NSCMD_TD_READ64 : NSCMD_TD_WRITE64) : (cmd == CMD_READ ? TD_READ64 : TD_WRITE64);
    }
    return DoIO((struct IORequest *)io) ? (io->io_Error ? io->io_Error : -1) : 0;
}

static void size_text(uint64_t bytes, char *out)
{
    ULONG mb = (ULONG)(bytes >> 20);
    if (bytes >= (10ULL << 30)) sprintf(out, "%lu GB", (unsigned long)(bytes >> 30));
    else if (mb >= 1) sprintf(out, "%lu.%lu MB", (unsigned long)mb, (unsigned long)(((bytes >> 10) & 1023) * 10 / 1024));
    else sprintf(out, "%lu KB", (unsigned long)(bytes >> 10));
}

static int fssm_of(struct DosList *dl, struct FileSysStartupMsg **f, char *devname)
{
    BPTR st = dl->dol_misc.dol_handler.dol_Startup;
    struct FileSysStartupMsg *fssm = (struct FileSysStartupMsg *)BADDR(st);
    UBYTE *bs;
    if (st <= 64 || !TypeOfMem(fssm) || !fssm->fssm_Device) return 0;
    bs = (UBYTE *)BADDR(fssm->fssm_Device);
    if (!TypeOfMem(bs) || bs[0] == 0 || bs[0] > 62) return 0;
    memcpy(devname, bs + 1, bs[0]);
    devname[bs[0]] = 0;
    *f = fssm;
    return 1;
}

/* The DOS device SYS: is on ("DH0"), or "" */
static void system_drive(char *name)
{
    struct DosList *dl;
    BPTR lock = Lock((STRPTR)"SYS:", SHARED_LOCK);
    struct MsgPort *task = NULL;
    name[0] = 0;
    if (lock) {
        task = ((struct FileLock *)BADDR(lock))->fl_Task;
        UnLock(lock);
    }
    if (!task) return;
    dl = LockDosList(LDF_DEVICES | LDF_READ);
    while ((dl = NextDosEntry(dl, LDF_DEVICES)))
        if (dl->dol_Task == task) {
            const UBYTE *s = (const UBYTE *)BADDR(dl->dol_Name);
            int n = s[0] < 31 ? s[0] : 31;
            memcpy(name, s + 1, n);
            name[n] = 0;
            break;
        }
    UnLockDosList(LDF_DEVICES | LDF_READ);
}

static void probe(int devi)
{
    ULONG units[MAX_DRIVES];
    int nunits = 0, i;
    struct DosList *dl;
    char sys[32], devname[64];
    for (i = 0; i < 8; i++) units[nunits++] = (ULONG)i;
    dl = LockDosList(LDF_DEVICES | LDF_READ);
    while ((dl = NextDosEntry(dl, LDF_DEVICES))) {
        struct FileSysStartupMsg *f;
        int have = 0;
        if (!fssm_of(dl, &f, devname) || strcmp(devname, devices[devi])) continue;
        for (i = 0; i < nunits; i++) if (units[i] == f->fssm_Unit) have = 1;
        if (!have && nunits < MAX_DRIVES) units[nunits++] = f->fssm_Unit;
    }
    UnLockDosList(LDF_DEVICES | LDF_READ);
    system_drive(sys);

    ndrives = 0;
    SetWindowPointer(win, WA_BusyPointer, TRUE, TAG_DONE);
    for (i = 0; i < nunits; i++) {
        struct dio x;
        struct drive *d = &drives[ndrives];
        struct DriveGeometry g;
        memset(d, 0, sizeof *d);
        d->unit = units[i];
        if (dio_open(&x, devices[devi], d->unit)) {
            memset(&g, 0, sizeof g);
            x.io->io_Command = TD_GETGEOMETRY;
            x.io->io_Data = &g;
            x.io->io_Length = sizeof g;
            if (!DoIO((struct IORequest *)x.io) && g.dg_SectorSize >= 256 && g.dg_TotalSectors) {
                char size[24], vols[96];
                int k, o = 0;
                d->block_bytes = g.dg_SectorSize;
                d->blocks = g.dg_TotalSectors;
                /* what is mounted on it */
                dl = LockDosList(LDF_DEVICES | LDF_READ);
                while ((dl = NextDosEntry(dl, LDF_DEVICES)) && d->nparts < MAX_PARTS) {
                    struct FileSysStartupMsg *f;
                    const UBYTE *s;
                    int n;
                    if (!fssm_of(dl, &f, devname) || strcmp(devname, devices[devi]) || f->fssm_Unit != d->unit) continue;
                    s = (const UBYTE *)BADDR(dl->dol_Name);
                    n = s[0] < 31 ? s[0] : 31;
                    memcpy(d->parts[d->nparts], s + 1, n);
                    d->parts[d->nparts][n] = 0;
                    if (sys[0] && !strcmp(d->parts[d->nparts], sys)) d->system = 1;
                    d->nparts++;
                }
                UnLockDosList(LDF_DEVICES | LDF_READ);
                vols[0] = 0;
                for (k = 0; k < d->nparts && o < (int)sizeof vols - 40; k++)
                    o += sprintf(vols + o, "%s%s:", k ? ", " : "", d->parts[k]);
                size_text(d->blocks * d->block_bytes, size);
                snprintf(d->label, sizeof d->label, "Unit %lu: %s%s%s%s", (unsigned long)d->unit, size,
                         d->nparts ? ", " : "", vols, d->system ? " (the system's)" : "");
                ndrives++;
            }
        }
        dio_close(&x);
    }
    SetWindowPointer(win, WA_BusyPointer, FALSE, TAG_DONE);
}

static void find_devices(const char *first)
{
    struct DosList *dl;
    char name[64];
    ndevices = 0;
    scopy(devices[ndevices++], first, 64);
    dl = LockDosList(LDF_DEVICES | LDF_READ);
    while ((dl = NextDosEntry(dl, LDF_DEVICES)) && ndevices < MAX_DEVICES) {
        struct FileSysStartupMsg *f;
        int i, have = 0;
        if (!fssm_of(dl, &f, name)) continue;
        if (!strcmp(name, "trackdisk.device")) continue;
        for (i = 0; i < ndevices; i++) if (!strcmp(devices[i], name)) have = 1;
        if (!have) strcpy(devices[ndevices++], name);
    }
    UnLockDosList(LDF_DEVICES | LDF_READ);
}

/* ---- the window -------------------------------------------------------------------------------------------- */

static const char markup[] =
    "<row><label for=file class=k>Image</label><input id=file style='flex:1' placeholder='The image file: .img, .iso, .hdf'>"
    "  <button id=browse>Choose...</button></row>"
    "<row><label for=device class=k>Device</label><select id=device style='flex:1'></select>"
    "  <button id=rescan title='Looks for its drives again'>Look again</button></row>"
    "<list id=drives style='flex:1;height:auto' title='The drives on the device'></list>"
    "<row><checkbox id=verify checked>Read it back to check</checkbox><spacer></spacer></row>"
    "<progress id=bar max=1000></progress>"
    "<row><label id=status style='flex:1'>Choose an image and a drive.</label>"
    "  <button id=read title='Saves the whole drive to a file'>Read drive to image...</button>"
    "  <button id=write>Write image to drive</button><button id=stop>Stop</button></row>";

static const char css[] = "row{gap:6px} .k{width:5em}";

static olui_el *el(const char *id) { return olui_find(ui, id); }
static void status(const char *s) { olui_set_text(ui, el("status"), s); }

static int ask(const char *text, const char *buttons)
{
    struct EasyStruct es = { sizeof(struct EasyStruct), 0, (STRPTR)NAME, (STRPTR)text, (STRPTR)buttons };
    return EasyRequestArgs(win, &es, NULL, NULL);
}

static void controls(void)
{
    olui_set_disabled(ui, el("stop"), !busy);
    olui_set_disabled(ui, el("write"), busy || cur_drive < 0);
    olui_set_disabled(ui, el("read"), busy || cur_drive < 0);
    olui_set_disabled(ui, el("browse"), busy);
    olui_set_disabled(ui, el("rescan"), busy);
    olui_set_disabled(ui, el("device"), busy);
    olui_set_disabled(ui, el("drives"), busy);
}

static void fill_drives(void)
{
    olui_el *list = el("drives");
    int i;
    olui_list_clear(ui, list);
    cur_drive = -1;
    for (i = 0; i < ndrives; i++) {
        olui_el *it = olui_list_add(ui, list, NULL, drives[i].label, NULL);
        if (it) olui_set_user(it, (void *)(long)(i + 1));
    }
    status(ndrives ? "Choose a drive." : "No drive answered on this device.");
    controls();
}

/* The window's messages while a drive is read or written: Stop is all that works */
static void pump(void)
{
    struct IntuiMessage *m;
    while ((m = (struct IntuiMessage *)GetMsg(win->UserPort))) {
        int r = olui_amiga_event(glue, m);
        ReplyMsg(&m->ExecMessage);
        if (r == OLUI_AMIGA_CLOSE) stop = 1;
    }
    olui_amiga_draw(glue);
}

static void progress(uint64_t done, uint64_t total, const char *what)
{
    char t[96], a[24], b[24];
    olui_set_progress(ui, el("bar"), total ? (int)(done * 1000 / total) : 0);
    size_text(done, a);
    size_text(total, b);
    snprintf(t, sizeof t, "%s: %s of %s", what, a, b);
    status(t);
    pump();
}

static void inhibit(const struct drive *d, int on)
{
    int i;
    for (i = 0; i < d->nparts; i++) {
        char n[34];
        snprintf(n, sizeof n, "%s:", d->parts[i]);
        Inhibit((STRPTR)n, on ? DOSTRUE : DOSFALSE);
    }
}

static void write_image(void)
{
    const struct drive *d = &drives[cur_drive];
    const char *path = olui_get_text(el("file"));
    struct dio x;
    BPTR fh;
    struct FileInfoBlock *fib;
    uint64_t size = 0, total, at = 0;
    UBYTE *buf = NULL, *back = NULL;
    int verify = olui_get_checked(el("verify")), e = 0, i, o;
    char text[512], s1[24], s2[24];
    const char *why = NULL;

    if (!path[0]) { ask("Choose the image first.", "OK"); return; }
    if (d->system) { ask("That drive holds the volume the system started from: it can't be written while it runs.", "OK"); return; }
    if (!(fh = Open((STRPTR)path, MODE_OLDFILE))) { ask("Can't open the image.", "OK"); return; }
    if ((fib = AllocDosObject(DOS_FIB, NULL))) {
        if (ExamineFH(fh, fib)) size = (ULONG)fib->fib_Size;
        FreeDosObject(DOS_FIB, fib);
    }
    total = d->blocks * d->block_bytes;
    if (!size) { Close(fh); ask("The image is empty, or its size can't be read.", "OK"); return; }
    if (size > total) {
        size_text(size, s1);
        size_text(total, s2);
        snprintf(text, sizeof text, "The image (%s) is bigger than the drive (%s).", s1, s2);
        Close(fh);
        ask(text, "OK");
        return;
    }
    size_text(size, s1);
    o = snprintf(text, sizeof text, "Write %s to %s unit %lu?\n\nEverything on the drive is lost", s1, devices[cur_dev],
                 (unsigned long)d->unit);
    for (i = 0; i < d->nparts && o < (int)sizeof text - 80; i++)
        o += snprintf(text + o, sizeof text - o, "%s%s:", i ? ", " : ": ", d->parts[i]);
    snprintf(text + o, sizeof text - o, ".");
    if (!ask(text, "Write|Cancel")) { Close(fh); return; }

    buf = AllocVec(PIECE, MEMF_PUBLIC);
    back = verify ? AllocVec(PIECE, MEMF_PUBLIC) : NULL;
    if (!buf || (verify && !back) || !dio_open(&x, devices[cur_dev], d->unit)) {
        if (buf) FreeVec(buf);
        if (back) FreeVec(back);
        dio_close(&x);
        Close(fh);
        ask("Not enough memory, or the drive can't be opened.", "OK");
        return;
    }
    busy = 1;
    stop = 0;
    controls();
    inhibit(d, 1);
    while (at < size && !stop) {
        ULONG n = size - at < PIECE ? (ULONG)(size - at) : PIECE, w = n;
        if (Read(fh, buf, n) != (LONG)n) { why = "the image couldn't be read"; break; }
        if (w % d->block_bytes) {      /* the last piece, made whole blocks */
            ULONG pad = d->block_bytes - w % d->block_bytes;
            memset(buf + w, 0, pad);
            w += pad;
        }
        if ((e = dio_do(&x, CMD_WRITE, at, buf, w))) { why = "the drive refused a write"; break; }
        if (verify) {
            if ((e = dio_do(&x, CMD_READ, at, back, w))) { why = "the drive refused a read"; break; }
            if (memcmp(buf, back, w)) { why = "what was read back isn't what was written"; break; }
        }
        at += n;
        progress(at, size, verify ? "Written and checked" : "Written");
    }
    x.io->io_Command = CMD_UPDATE;
    x.io->io_Length = 0;
    DoIO((struct IORequest *)x.io);
    dio_close(&x);
    inhibit(d, 0);
    Close(fh);
    FreeVec(buf);
    if (back) FreeVec(back);
    busy = 0;
    controls();
    if (why) {
        size_text(at, s1);
        snprintf(text, sizeof text, "Stopped after %s: %s.", s1, why);
        status(text);
        ask(text, "OK");
    } else if (stop)
        status("Stopped. The drive holds part of the image.");
    else {
        status(verify ? "Written and checked." : "Written.");
        ask("The image is written.\nTake the card or stick out and put it back (or reboot) for its partitions to be read.", "OK");
    }
}

static void read_image(void)
{
    const struct drive *d = &drives[cur_drive];
    struct FileRequester *fr;
    struct dio x;
    char path[512], text[320], s1[24];
    BPTR fh;
    UBYTE *buf;
    uint64_t total = d->blocks * d->block_bytes, at = 0;
    const char *why = NULL;
    if (!(fr = AllocAslRequestTags(ASL_FileRequest, ASLFR_Window, (ULONG)win, ASLFR_TitleText, (ULONG)"Save the drive as",
                                   ASLFR_DoSaveMode, TRUE, ASLFR_InitialFile, (ULONG)"drive.img", TAG_DONE)))
        return;
    if (!AslRequest(fr, NULL)) { FreeAslRequest(fr); return; }
    scopy(path, (const char *)fr->fr_Drawer, sizeof path);
    AddPart((STRPTR)path, fr->fr_File, sizeof path);
    FreeAslRequest(fr);
    if (!(fh = Open((STRPTR)path, MODE_NEWFILE))) { ask("Can't make that file.", "OK"); return; }
    if (!(buf = AllocVec(PIECE, MEMF_PUBLIC)) || !dio_open(&x, devices[cur_dev], d->unit)) {
        if (buf) FreeVec(buf);
        dio_close(&x);
        Close(fh);
        ask("Not enough memory, or the drive can't be opened.", "OK");
        return;
    }
    busy = 1;
    stop = 0;
    controls();
    while (at < total && !stop) {
        ULONG n = total - at < PIECE ? (ULONG)(total - at) : PIECE;
        if (dio_do(&x, CMD_READ, at, buf, n)) { why = "the drive refused a read"; break; }
        if (Write(fh, buf, n) != (LONG)n) { why = "the file couldn't be written (is there room?)"; break; }
        at += n;
        progress(at, total, "Read");
    }
    dio_close(&x);
    Close(fh);
    FreeVec(buf);
    busy = 0;
    controls();
    if (why || stop) {
        size_text(at, s1);
        snprintf(text, sizeof text, "Stopped after %s%s%s.", s1, why ? ": " : "", why ? why : "");
        status(text);
        if (why) ask(text, "OK");
    } else {
        olui_set_text(ui, el("file"), path);
        status("The drive is saved.");
    }
}

static void on_browse(olui *u, const olui_event *ev, void *data)
{
    struct FileRequester *fr;
    char path[512];
    (void)u; (void)ev; (void)data;
    if (busy) return;
    if (!(fr = AllocAslRequestTags(ASL_FileRequest, ASLFR_Window, (ULONG)win, ASLFR_TitleText, (ULONG)"The image to write",
                                   TAG_DONE)))
        return;
    if (AslRequest(fr, NULL)) {
        scopy(path, (const char *)fr->fr_Drawer, sizeof path);
        AddPart((STRPTR)path, fr->fr_File, sizeof path);
        olui_set_text(ui, el("file"), path);
    }
    FreeAslRequest(fr);
}

static void on_device(olui *u, const olui_event *ev, void *data)
{
    (void)u; (void)data;
    if (busy || ev->index < 0 || ev->index >= ndevices) return;
    cur_dev = ev->index;
    probe(cur_dev);
    fill_drives();
}

static void on_rescan(olui *u, const olui_event *ev, void *data)
{
    (void)u; (void)ev; (void)data;
    if (busy) return;
    probe(cur_dev);
    fill_drives();
}

static void on_drive(olui *u, const olui_event *ev, void *data)
{
    (void)u; (void)data;
    if (busy) return;
    cur_drive = ev->item ? (int)(long)olui_user(ev->item) - 1 : -1;
    status(cur_drive >= 0 ? drives[cur_drive].label : "Choose a drive.");
    controls();
}

static void on_write(olui *u, const olui_event *ev, void *data) { (void)u; (void)ev; (void)data; if (!busy && cur_drive >= 0) write_image(); }
static void on_read(olui *u, const olui_event *ev, void *data) { (void)u; (void)ev; (void)data; if (!busy && cur_drive >= 0) read_image(); }
static void on_stop(olui *u, const olui_event *ev, void *data) { (void)u; (void)ev; (void)data; stop = 1; }

static void settings(int argc, char **argv, char *dev, int size)
{
    if (argc == 0) {
        struct WBStartup *wbs = (struct WBStartup *)argv;
        if (IconBase && wbs && wbs->sm_NumArgs > 0) {
            BPTR old = CurrentDir(wbs->sm_ArgList[0].wa_Lock);
            struct DiskObject *d = GetDiskObject((STRPTR)wbs->sm_ArgList[0].wa_Name);
            if (d && d->do_ToolTypes) {
                const char *v = (const char *)FindToolType((CONST_STRPTR *)d->do_ToolTypes, (STRPTR)"DEVICE");
                if (v && v[0]) scopy(dev, v, size);
            }
            if (d) FreeDiskObject(d);
            CurrentDir(old);
        }
    } else {
        LONG a[1] = { 0 };
        struct RDArgs *rd = ReadArgs((STRPTR)"DEVICE/K", a, NULL);
        if (rd) {
            if (a[0]) scopy(dev, (const char *)a[0], size);
            FreeArgs(rd);
        }
    }
}

int main(int argc, char **argv)
{
    struct Screen *scr;
    char first[64] = "scsi.device", err[128];
    int fh, w, h, i, rc = RETURN_FAIL;
    (void)version;
    IconBase = OpenLibrary((STRPTR)"icon.library", 37);
    settings(argc, argv, first, sizeof first);
    find_devices(first);
    if (!(scr = LockPubScreen(NULL))) goto out;
    fh = scr->Font ? scr->Font->ta_YSize : 8;
    w = fh * 56;
    h = fh * 26;
    if (w > scr->Width) w = scr->Width;
    if (h > scr->Height - scr->BarHeight - 1) h = scr->Height - scr->BarHeight - 1;
    win = OpenWindowTags(NULL, WA_PubScreen, (ULONG)scr, WA_Title, (ULONG)"Image Writer", WA_ScreenTitle, (ULONG)NAME " 1.0",
                         WA_Left, (scr->Width - w) / 2, WA_Top, (scr->Height - h) / 2, WA_Width, w, WA_Height, h,
                         WA_MinWidth, fh * 40, WA_MinHeight, fh * 18, WA_MaxWidth, ~0UL, WA_MaxHeight, ~0UL,
                         WA_DragBar, TRUE, WA_DepthGadget, TRUE, WA_CloseGadget, TRUE, WA_SizeGadget, TRUE,
                         WA_Activate, TRUE, WA_SimpleRefresh, TRUE, WA_ReportMouse, TRUE,
                         WA_IDCMP, olui_amiga_idcmp(), TAG_DONE);
    UnlockPubScreen(NULL, scr);
    if (!win) goto out;
    if (!(ui = olui_new()) || !(glue = olui_amiga_new(ui, win))) goto out;
    olui_amiga_theme(glue, NULL);
    if (!olui_load(ui, markup, css, err, sizeof err)) { if (argc) Printf((STRPTR)NAME ": %s\n", (ULONG)err); goto out; }
    for (i = 0; i < ndevices; i++) olui_select_add(ui, el("device"), devices[i], NULL);
    olui_on(ui, "browse", OLUI_CLICK, on_browse, NULL);
    olui_on(ui, "device", OLUI_CHANGE, on_device, NULL);
    olui_on(ui, "rescan", OLUI_CLICK, on_rescan, NULL);
    olui_on(ui, "drives", OLUI_SELECT, on_drive, NULL);
    olui_on(ui, "write", OLUI_CLICK, on_write, NULL);
    olui_on(ui, "read", OLUI_CLICK, on_read, NULL);
    olui_on(ui, "stop", OLUI_CLICK, on_stop, NULL);
    olui_select_set(ui, el("device"), 0);
    cur_dev = 0;
    controls();
    if (!olui_amiga_layout(glue)) goto out;
    probe(cur_dev);
    fill_drives();
    rc = RETURN_OK;
    while (!quit) {
        struct IntuiMessage *m;
        ULONG sig = Wait((1UL << win->UserPort->mp_SigBit) | SIGBREAKF_CTRL_C);
        if (sig & SIGBREAKF_CTRL_C) break;
        while ((m = (struct IntuiMessage *)GetMsg(win->UserPort))) {
            int r = olui_amiga_event(glue, m);
            ReplyMsg(&m->ExecMessage);
            if (r == OLUI_AMIGA_CLOSE) quit = 1;
        }
        olui_amiga_draw(glue);
    }
out:
    if (glue) olui_amiga_free(glue);
    if (ui) olui_free(ui);
    if (win) CloseWindow(win);
    if (IconBase) CloseLibrary(IconBase);
    return rc;
}
