/*
 * Tiny Eden FOV mod  (Unreal Engine 5.8 project "CGH", native Linux build)
 *
 * Why this works
 * --------------
 * Tiny Eden has no FOV setting because nothing in the game ever sets a FOV.
 * Not one cooked asset in the shipped IoStore container references FieldOfView
 * or SetFieldOfView, so every camera in the game runs at UCameraComponent's
 * C++ constructor default of 90 degrees. That default is a single 16-byte
 * .rodata constant the constructor copies into the object:
 *
 *   mov    dword [rbx+0x264], 1.777778    ; AspectRatio
 *   movaps xmm0, [rip+disp32]             ; -> {90.0, 90.0, 1.0, 1536.0}
 *   movaps [rbx+0x240], xmm0              ; FieldOfView is the first float
 *
 * The shipping binary is non-PIE, so that constant sits at a fixed address.
 * Rewriting its first float before the engine builds anything changes the FOV
 * for the whole game. Nothing on disk is modified, so Steam file validation
 * stays happy, and a game update just makes this library re-locate itself: it
 * pattern-scans its own .text at startup rather than hardcoding addresses.
 *
 * Live changes
 * ------------
 * Components already constructed keep the FOV they were born with, so when the
 * value in the config file changes, a background thread walks the process heap
 * for live UCameraComponent objects (identified by their vtable pointer) and
 * writes the new FieldOfView into each. That makes the config file behave like
 * a slider: change it while the game runs and the view follows within a second.
 *
 * Usage
 * -----
 *   Steam launch options:
 *     LD_PRELOAD=$HOME/tiny-eden-fov/libtinyeden_fov.so %command%
 *   Then, any time:
 *     ~/tiny-eden-fov/fov 100
 *
 *   TINY_EDEN_FOV=100     set the FOV for one launch, ignoring the file
 *   TINY_EDEN_FOV_LIVE=0  disable the live-update thread
 */

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

#define FOV_MIN 40.0f
#define FOV_MAX 170.0f
#define FIELDOFVIEW_OFFSET 0x240

static float   *g_fov_const;
static uint64_t g_vtable;
static float    g_applied;
static float    g_seen[16];
static int      g_nseen;

/* ACameraActor's constructor writes 90.0f into its camera component directly,
 * so camera actors ignore the UCameraComponent default. Patch that immediate
 * too when TINY_EDEN_FOV_CAMERAACTORS is set. */
static unsigned char *g_cameraactor_imm;

/*
 * The /proc parsing below is hand rolled rather than sscanf'd, and the number
 * parsing avoids strtof. Under _GNU_SOURCE, glibc redirects both to __isoc23_*
 * symbols that need GLIBC_2.38, which would refuse to load inside the Steam
 * Linux Runtime (sniper is glibc 2.31). Keeping to plain calls, plus the
 * .symver lines below, lets one prebuilt .so run everywhere.
 */
__asm__(".symver pthread_create,pthread_create@GLIBC_2.2.5");
__asm__(".symver pthread_detach,pthread_detach@GLIBC_2.2.5");

/* Parse "<hex>-<hex> <perms>" from a /proc maps or smaps header line.
   Returns 1 on success. */
static int parse_map_header(const char *line, uintptr_t *lo, uintptr_t *hi, char perms[5])
{
    const char *p = line;
    uintptr_t a = 0, b = 0;
    int digits = 0;

    for (; *p; p++, digits++) {
        int v;
        if (*p >= '0' && *p <= '9') v = *p - '0';
        else if (*p >= 'a' && *p <= 'f') v = *p - 'a' + 10;
        else break;
        a = a * 16 + (uintptr_t)v;
    }
    if (!digits || *p != '-')
        return 0;
    p++;
    for (digits = 0; *p; p++, digits++) {
        int v;
        if (*p >= '0' && *p <= '9') v = *p - '0';
        else if (*p >= 'a' && *p <= 'f') v = *p - 'a' + 10;
        else break;
        b = b * 16 + (uintptr_t)v;
    }
    if (!digits || *p != ' ')
        return 0;
    p++;
    for (int i = 0; i < 4; i++) {
        if (!*p || *p == ' ' || *p == '\n')
            return 0;
        perms[i] = *p++;
    }
    perms[4] = '\0';
    *lo = a;
    *hi = b;
    return 1;
}

/* Decimal, optionally with a fractional part. Returns -1.0f when unparseable. */
static float parse_number(const char *s)
{
    while (*s == ' ' || *s == '\t')
        s++;

    double whole = 0.0, frac = 0.0, scale = 1.0;
    int digits = 0;

    for (; *s >= '0' && *s <= '9'; s++, digits++)
        whole = whole * 10.0 + (*s - '0');
    if (*s == '.' || *s == ',') {
        s++;
        for (; *s >= '0' && *s <= '9'; s++, digits++) {
            scale /= 10.0;
            frac += (*s - '0') * scale;
        }
    }
    if (!digits)
        return -1.0f;
    return (float)(whole + frac);
}

static unsigned long parse_ulong(const char *s)
{
    while (*s == ' ' || *s == '\t')
        s++;
    unsigned long v = 0;
    for (; *s >= '0' && *s <= '9'; s++)
        v = v * 10 + (unsigned long)(*s - '0');
    return v;
}

static void note(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    fputs("[tinyeden-fov] ", stderr);
    vfprintf(stderr, fmt, ap);
    fputc('\n', stderr);
    fflush(stderr);
    va_end(ap);
}

/* ------------------------------------------------------------- process maps */

struct range { uintptr_t lo, hi; };

static int exec_ranges(struct range *out, int max)
{
    char self[4096];
    ssize_t n = readlink("/proc/self/exe", self, sizeof(self) - 1);
    if (n <= 0)
        return 0;
    self[n] = '\0';

    FILE *f = fopen("/proc/self/maps", "r");
    if (!f)
        return 0;

    char line[4096];
    int count = 0;
    while (count < max && fgets(line, sizeof(line), f)) {
        uintptr_t lo, hi;
        char perms[5];
        if (!parse_map_header(line, &lo, &hi, perms))
            continue;
        if (perms[2] != 'x')
            continue;

        /* the path is the last field; maps has no spaces before it */
        char *p = strchr(line, '/');
        if (!p)
            continue;
        size_t len = strlen(p);
        while (len && (p[len - 1] == '\n' || p[len - 1] == ' '))
            p[--len] = '\0';
        if (strcmp(p, self) != 0)
            continue;
        out[count].lo = lo;
        out[count].hi = hi;
        count++;
    }
    fclose(f);
    return count;
}

/* ------------------------------------------------------------- pattern scan */

static const unsigned char k_pat[] = {
    0xc7, 0x83, 0x64, 0x02, 0x00, 0x00, 0x3b, 0x8e, 0xe3, 0x3f,
    0x0f, 0x28, 0x05, 0x00, 0x00, 0x00, 0x00,
    0x0f, 0x29, 0x83, 0x40, 0x02, 0x00, 0x00
};
static const char k_mask[] = "xxxxxxxxxx" "xxx????" "xxxxxxx";
static const float k_expect[4] = { 90.0f, 90.0f, 1.0f, 1536.0f };

static const unsigned char *scan(const unsigned char *lo, const unsigned char *hi)
{
    size_t len = sizeof(k_pat);
    if ((size_t)(hi - lo) < len)
        return NULL;
    for (const unsigned char *p = lo; p <= hi - len; p++) {
        size_t i = 0;
        while (i < len && (k_mask[i] != 'x' || p[i] == k_pat[i]))
            i++;
        if (i == len)
            return p;
    }
    return NULL;
}

static int locate(void)
{
    struct range r[64];
    int n = exec_ranges(r, 64);
    if (n == 0) {
        note("cannot read /proc/self/maps");
        return -1;
    }

    for (int i = 0; i < n; i++) {
        const unsigned char *site = scan((const unsigned char *)r[i].lo,
                                         (const unsigned char *)r[i].hi);
        if (!site)
            continue;

        int32_t disp;
        memcpy(&disp, site + 13, 4);
        float *cst = (float *)(site + 17 + disp);
        if (memcmp(cst, k_expect, sizeof(k_expect)) != 0)
            continue;

        /* The same constructor stores the vtable: 48 c7 03 <imm32> */
        uint64_t vt = 0;
        for (const unsigned char *p = site; p > site - 512 && p > (const unsigned char *)r[i].lo; p--) {
            if (p[0] == 0x48 && p[1] == 0xc7 && p[2] == 0x03) {
                uint32_t imm;
                memcpy(&imm, p + 3, 4);
                vt = imm;
                break;
            }
        }

        /* ACameraActor ctor: mov dword [r14+0x240], 90.0f */
        static const unsigned char aa[] = {
            0xc7, 0x86, 0x40, 0x02, 0x00, 0x00, 0x00, 0x00, 0xb4, 0x42
        };
        for (const unsigned char *p = (const unsigned char *)r[i].lo;
             p <= (const unsigned char *)r[i].hi - sizeof(aa); p++) {
            if (memcmp(p, aa, sizeof(aa)) == 0) {
                g_cameraactor_imm = (unsigned char *)p + 6;
                break;
            }
        }

        g_fov_const = cst;
        g_vtable = vt;
        note("located FOV default at %p (UCameraComponent vtable 0x%lx, ACameraActor imm %p)",
             (void *)cst, (unsigned long)vt, (void *)g_cameraactor_imm);
        return 0;
    }
    note("could not locate the camera FOV default; game updated? mod disabled.");
    return -1;
}

/* ------------------------------------------------------------- patching */

static int write_ro(void *addr, const void *src, size_t len, int restore)
{
    long pagesz = sysconf(_SC_PAGESIZE);
    uintptr_t start = (uintptr_t)addr & ~(uintptr_t)(pagesz - 1);
    uintptr_t end = ((uintptr_t)addr + len + pagesz - 1) & ~(uintptr_t)(pagesz - 1);

    if (mprotect((void *)start, end - start, PROT_READ | PROT_WRITE) != 0) {
        note("mprotect failed: %s", strerror(errno));
        return -1;
    }
    memcpy(addr, src, len);
    mprotect((void *)start, end - start, restore);
    return 0;
}

/*
 * Write fov into every live UCameraComponent.
 *
 * The game keeps allocating and freeing while this runs, so a region listed in
 * smaps can be gone by the time we look at it. Going through /proc/self/mem
 * means a stale address returns EIO instead of taking the process down with it.
 */
#define SCAN_CHUNK (8UL << 20)
#define SCAN_OVERLAP 4096UL

static int retune_live(float fov)
{
    if (!g_vtable)
        return 0;

    int fd = open("/proc/self/mem", O_RDWR);
    if (fd < 0)
        return 0;

    FILE *sm = fopen("/proc/self/smaps", "r");
    if (!sm) {
        close(fd);
        return 0;
    }

    unsigned char *buf = malloc(SCAN_CHUNK);
    if (!buf) {
        fclose(sm);
        close(fd);
        return 0;
    }

    char line[512], perms[5] = { 0 };
    uintptr_t lo = 0, hi = 0;
    unsigned long rss = 0;
    int have = 0, hits = 0;

    for (;;) {
        char *got = fgets(line, sizeof(line), sm);
        uintptr_t a, b;
        char p[5];

        if (!got || parse_map_header(line, &a, &b, p)) {
            if (have && rss > 0 && perms[0] == 'r' && perms[1] == 'w' &&
                (hi - lo) <= (256UL << 20)) {
                uintptr_t step = SCAN_CHUNK - SCAN_OVERLAP;
                for (uintptr_t off = 0; lo + off < hi; off += step) {
                    size_t want = hi - (lo + off);
                    if (want > SCAN_CHUNK)
                        want = SCAN_CHUNK;
                    ssize_t n = pread(fd, buf, want, (off_t)(lo + off));
                    if (n <= 0)
                        break;
                    for (ssize_t i = 0; i + FIELDOFVIEW_OFFSET + 4 <= n; i += 8) {
                        uint64_t vt;
                        memcpy(&vt, buf + i, sizeof(vt));
                        if (vt != g_vtable)
                            continue;
                        float f;
                        memcpy(&f, buf + i + FIELDOFVIEW_OFFSET, sizeof(f));
                        if (!(f > 1.0f && f < 400.0f))
                            continue;
                        int k;
                        for (k = 0; k < g_nseen; k++)
                            if (g_seen[k] == f) break;
                        if (k == g_nseen && g_nseen < 16)
                            g_seen[g_nseen++] = f;
                        if (f == fov)
                            continue;
                        off_t at = (off_t)(lo + off + (uintptr_t)i + FIELDOFVIEW_OFFSET);
                        if (pwrite(fd, &fov, sizeof(fov), at) == (ssize_t)sizeof(fov))
                            hits++;
                    }
                    if ((size_t)n < want)
                        break;
                }
            }
            if (!got)
                break;
            lo = a; hi = b; rss = 0; have = 1;
            memcpy(perms, p, 5);
        } else if (strncmp(line, "Rss:", 4) == 0) {
            rss = parse_ulong(line + 4);
        }
    }

    free(buf);
    fclose(sm);
    close(fd);
    return hits;
}

/* ------------------------------------------------------------- config */

static void config_path(char *buf, size_t n)
{
    const char *over = getenv("TINY_EDEN_FOV_FILE");
    if (over && *over) {
        snprintf(buf, n, "%s", over);
        return;
    }
    const char *home = getenv("HOME");
    snprintf(buf, n, "%s/.config/tiny-eden-fov", home ? home : ".");
}

static float parse_fov(const char *s)
{
    float v = parse_number(s);
    if (v < 0.0f || v < FOV_MIN || v > FOV_MAX)
        return 0.0f;
    return v;
}

static float read_config(void)
{
    char path[4096], buf[64];
    config_path(path, sizeof(path));
    FILE *f = fopen(path, "r");
    if (!f)
        return 0.0f;
    char *got = fgets(buf, sizeof(buf), f);
    fclose(f);
    return got ? parse_fov(buf) : 0.0f;
}

static void apply(float fov)
{
    if (fov == g_applied)
        return;
    write_ro(g_fov_const, &fov, sizeof(fov), PROT_READ);

    const char *ca = getenv("TINY_EDEN_FOV_CAMERAACTORS");
    if (ca && strcmp(ca, "0") != 0 && g_cameraactor_imm)
        write_ro(g_cameraactor_imm, &fov, sizeof(fov), PROT_READ | PROT_EXEC);

    g_nseen = 0;
    int live = retune_live(fov);
    g_applied = fov;
    note("FOV %.1f (%d live camera%s retuned)", (double)fov, live, live == 1 ? "" : "s");
    const char *dbg = getenv("TINY_EDEN_FOV_DEBUG");
    if (dbg && strcmp(dbg, "0") != 0)
        for (int k = 0; k < g_nseen; k++)
            note("  live camera was at %.1f", (double)g_seen[k]);
}

static void *watcher(void *unused)
{
    (void)unused;
    for (;;) {
        struct timespec ts = { .tv_sec = 1, .tv_nsec = 0 };
        nanosleep(&ts, NULL);
        float v = read_config();
        if (v != 0.0f)
            apply(v);
    }
    return NULL;
}

/* ------------------------------------------------------------- entry */

static int is_the_game(void)
{
    char path[4096];
    ssize_t n = readlink("/proc/self/exe", path, sizeof(path) - 1);
    if (n <= 0)
        return 0;
    path[n] = '\0';
    const char *base = strrchr(path, '/');
    return strcmp(base ? base + 1 : path, "CGH-Linux-Shipping") == 0;
}

__attribute__((constructor))
static void tinyeden_fov_init(void)
{
    if (!is_the_game())
        return;
    if (locate() != 0)
        return;

    float fov = 0.0f;
    const char *env = getenv("TINY_EDEN_FOV");
    if (env && *env) {
        fov = parse_fov(env);
        if (fov == 0.0f)
            note("TINY_EDEN_FOV=%s ignored (want %.0f-%.0f)", env, (double)FOV_MIN, (double)FOV_MAX);
    }
    if (fov == 0.0f)
        fov = read_config();

    if (fov != 0.0f)
        apply(fov);
    else
        note("no FOV set; game stays at 90 (write one to ~/.config/tiny-eden-fov)");

    const char *live = getenv("TINY_EDEN_FOV_LIVE");
    if (!live || strcmp(live, "0") != 0) {
        pthread_t t;
        if (pthread_create(&t, NULL, watcher, NULL) == 0)
            pthread_detach(t);
    }
}
