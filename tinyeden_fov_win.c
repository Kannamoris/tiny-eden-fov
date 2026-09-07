/*
 * tinyeden_fov_win.c - field of view control for Tiny Eden (Windows build)
 *
 * Built as winmm.dll and dropped next to CGH-Win64-Shipping.exe. The game
 * statically imports winmm, so this loads before the engine starts and the
 * four forwarded functions are handed straight back to the real winmm.
 *
 * Tiny Eden never sets a field of view anywhere in its content, so every
 * camera inherits the UCameraComponent constructor default of 90 degrees.
 * The Linux build keeps that default in a single .rodata constant; MSVC
 * instead emits it as immediate operands, so this patches the immediates.
 *
 * The constructor is found by the shape of its defaults rather than by a
 * fixed byte string: a store of 90.0f to +0x250 followed within 96 bytes by
 * a store of 1.7777778f (the default aspect ratio) to +0x274. Nothing else
 * in the binary looks like that, and it survives a recompile of the game.
 *
 * Set the value with -fov=105 on the command line, the TINY_EDEN_FOV
 * environment variable, or tiny_eden_fov.txt next to this DLL. The file is
 * re-read once a second, so it also retunes cameras that already exist.
 */

/* Keeps mmsystem.h out, which would declare the four functions below as
   dllimport and then complain when we export them ourselves. */
#define WIN32_LEAN_AND_MEAN

#include <windows.h>
#include <stdarg.h>
#include <stdio.h>

#define FOV_MIN 40.0f
#define FOV_MAX 170.0f

#define OFF_FOV        0x250   /* UCameraComponent::FieldOfView            */
#define OFF_FOV1P      0x254   /* FirstPersonFieldOfView                   */
#define OFF_SCALE1P    0x258   /* FirstPersonScale, 1.0f                   */
#define OFF_ASPECT     0x274   /* AspectRatio, 1.7777778f                  */

#define FOV_DEFAULT_BITS   0x42b40000u   /* 90.0f      */
#define ASPECT_BITS        0x3fe38e3bu   /* 1.7777778f */
#define ONE_BITS           0x3f800000u   /* 1.0f       */

#define MAX_SITES 16
#define WINDOW    0x60         /* how far after the FOV store to look      */
#define BACKSCAN  0x600        /* how far back to look for the vtable load */

/* ------------------------------------------------------------------ state */

static unsigned char *g_sites[MAX_SITES];  /* instruction starts to patch   */
static int            g_actor[MAX_SITES];  /* set on the camera actor sites */
static int            g_nsites;
static ULONGLONG      g_vtable;            /* UCameraComponent vtable       */
static float          g_current;
static float          g_last_file;
static int            g_debug;
static int            g_live = 1;
static int            g_cameraactors;
static wchar_t        g_cfg[MAX_PATH];
static wchar_t        g_log[MAX_PATH];

/* -------------------------------------------------------------- logging */

static void logf_(const char *fmt, ...)
{
    char buf[512];
    va_list ap;
    int n;
    HANDLE h;
    DWORD wrote;

    va_start(ap, fmt);
    n = vsnprintf(buf, sizeof buf - 2, fmt, ap);
    va_end(ap);
    if (n < 0) return;

    OutputDebugStringA(buf);
    fprintf(stderr, "%s", buf);

    h = CreateFileW(g_log, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE,
                    NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h != INVALID_HANDLE_VALUE) {
        WriteFile(h, buf, (DWORD)strlen(buf), &wrote, NULL);
        CloseHandle(h);
    }
}

/* ------------------------------------------------------- number parsing */

/* Deliberately hand rolled. The CRT is not necessarily initialised when
   DllMain runs, and locale can turn the decimal point into a comma. */
static float parse_fov(const char *s)
{
    float v = 0.0f, frac = 0.1f;
    int seen = 0;

    while (*s == ' ' || *s == '\t') s++;
    while (*s >= '0' && *s <= '9') { v = v * 10.0f + (float)(*s++ - '0'); seen = 1; }
    if (*s == '.' || *s == ',') {
        s++;
        while (*s >= '0' && *s <= '9') { v += (float)(*s++ - '0') * frac; frac *= 0.1f; seen = 1; }
    }
    if (!seen) return 0.0f;
    if (v < FOV_MIN) v = FOV_MIN;
    if (v > FOV_MAX) v = FOV_MAX;
    return v;
}

static float parse_fov_w(const wchar_t *s)
{
    char buf[32];
    int i = 0;
    while (i < (int)sizeof buf - 1 && *s && *s > 0 && *s < 128) buf[i++] = (char)*s++;
    buf[i] = 0;
    return parse_fov(buf);
}

/* ------------------------------------------------------------ PE walking */

static unsigned char *text_of(HMODULE mod, size_t *len)
{
    unsigned char *base = (unsigned char *)mod;
    IMAGE_DOS_HEADER *dos = (IMAGE_DOS_HEADER *)base;
    IMAGE_NT_HEADERS64 *nt;
    IMAGE_SECTION_HEADER *sec;
    WORD i;

    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return NULL;
    nt = (IMAGE_NT_HEADERS64 *)(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) return NULL;

    sec = IMAGE_FIRST_SECTION(nt);
    for (i = 0; i < nt->FileHeader.NumberOfSections; i++, sec++) {
        if (memcmp(sec->Name, ".text", 5) == 0) {
            *len = sec->Misc.VirtualSize;
            return base + sec->VirtualAddress;
        }
    }
    return NULL;
}

/* ------------------------------------------------------------- matching */

static unsigned u32at(const unsigned char *p)
{
    unsigned v;
    memcpy(&v, p, 4);
    return v;
}

/* mov dword ptr [reg+disp32], imm32  ->  C7 /0 disp32 imm32 */
static int is_store(const unsigned char *p, unsigned disp, unsigned imm)
{
    return p[0] == 0xc7 && (p[1] & 0xf8) == 0x80 &&
           u32at(p + 2) == disp && u32at(p + 6) == imm;
}

static int find_store(const unsigned char *from, size_t span, unsigned disp, unsigned imm)
{
    size_t i;
    for (i = 0; i + 10 <= span; i++)
        if (is_store(from + i, disp, imm)) return 1;
    return 0;
}

/* lea rax,[rip+disp32] ; mov [rbx],rax  -- the constructor storing a vtable */
static ULONGLONG vtable_before(const unsigned char *site, const unsigned char *text)
{
    const unsigned char *start = site - BACKSCAN;
    const unsigned char *p;
    ULONGLONG found = 0;
    int disp;

    if (start < text) start = text;
    for (p = start; p + 10 <= site; p++) {
        if (p[0] == 0x48 && p[1] == 0x8d && p[2] == 0x05 &&
            p[7] == 0x48 && p[8] == 0x89 && p[9] == 0x03) {
            memcpy(&disp, p + 3, 4);
            found = (ULONGLONG)(p + 7 + disp);
        }
    }
    return found;
}

static int locate(void)
{
    HMODULE self = GetModuleHandleW(NULL);
    unsigned char *text;
    size_t len, i;
    ULONGLONG vt_a = 0, vt_b = 0;

    text = text_of(self, &len);
    if (!text) { logf_("[tinyeden-fov] no .text section\n"); return 0; }

    for (i = 0; i + 10 <= len; i++) {
        unsigned char *p;

        if (!is_store(text + i, OFF_FOV, FOV_DEFAULT_BITS)) continue;
        p = text + i;

        /* The aspect ratio store nails it down as a camera. */
        if (!find_store(p + 10, WINDOW, OFF_ASPECT, ASPECT_BITS)) continue;

        /* Only the component constructor itself sets FirstPersonScale. The
           other sites are camera actors writing 90 back over a component
           they just built, and only in the constructor is the vtable in rbx
           the one we want for finding live cameras. */
        {
            int is_ctor = find_store(p + 10, WINDOW, OFF_SCALE1P, ONE_BITS);

            if (g_nsites < MAX_SITES) {
                g_actor[g_nsites] = !is_ctor;
                g_sites[g_nsites++] = p;
            }
            if (is_ctor) {
                ULONGLONG vt = vtable_before(p, text);
                if (!vt_a) vt_a = vt; else if (!vt_b) vt_b = vt;
            }
        }
    }

    if (!g_nsites) { logf_("[tinyeden-fov] constructor not found\n"); return 0; }

    /* UCameraComponent has two constructors, the ordinary one and the
       vtable helper. Both store the same pointer, so agreement is proof. */
    if (vt_a && vt_a == vt_b) g_vtable = vt_a;
    else if (vt_a && !vt_b)   g_vtable = vt_a;
    else if (vt_a != vt_b)    logf_("[tinyeden-fov] vtable candidates disagree "
                                    "(0x%llx vs 0x%llx), live retune off\n", vt_a, vt_b);

    {
        int actors = 0, i;
        for (i = 0; i < g_nsites; i++) actors += g_actor[i];
        logf_("[tinyeden-fov] %d constructor site%s, %d camera actor site%s%s, "
              "vtable 0x%llx\n",
              g_nsites - actors, g_nsites - actors == 1 ? "" : "s",
              actors, actors == 1 ? "" : "s",
              g_cameraactors ? "" : " (skipped)", g_vtable);
    }
    return 1;
}

/* -------------------------------------------------------------- patching */

static void patch_sites(float fov)
{
    int i;
    for (i = 0; i < g_nsites; i++) {
        unsigned char *at = g_sites[i] + 6;   /* the imm32 operand */
        DWORD old;

        /* Cutscene cameras are framed deliberately, so leave them alone
           unless asked. Same default as the Linux build. */
        if (g_actor[i] && !g_cameraactors) continue;

        if (!VirtualProtect(at, 4, PAGE_EXECUTE_READWRITE, &old)) continue;
        memcpy(at, &fov, 4);
        /* Restoring without EXECUTE would kill the page the game runs from. */
        VirtualProtect(at, 4, old, &old);
        FlushInstructionCache(GetCurrentProcess(), at, 4);
    }
}

/* ----------------------------------------------------------- live retune */

static int retune(float fov)
{
    MEMORY_BASIC_INFORMATION mbi;
    unsigned char *addr = 0;
    unsigned char *buf;
    HANDLE self = GetCurrentProcess();
    const size_t chunk = 8u << 20;
    unsigned char *img_lo, *img_hi;
    int hits = 0;

    if (!g_vtable || !g_live) return 0;

    /* The image's own .data holds the vtable address in places that are not
       objects, and it is writable, so a match there is always a false one.
       Cameras are heap allocated, so skipping the image costs nothing. */
    {
        HMODULE m = GetModuleHandleW(NULL);
        IMAGE_DOS_HEADER *dos = (IMAGE_DOS_HEADER *)m;
        IMAGE_NT_HEADERS64 *nt = (IMAGE_NT_HEADERS64 *)((unsigned char *)m + dos->e_lfanew);
        img_lo = (unsigned char *)m;
        img_hi = img_lo + nt->OptionalHeader.SizeOfImage;
    }

    buf = (unsigned char *)VirtualAlloc(NULL, chunk, MEM_COMMIT, PAGE_READWRITE);
    if (!buf) return 0;

    while (VirtualQuery(addr, &mbi, sizeof mbi) == sizeof mbi) {
        DWORD w = mbi.Protect & (PAGE_READWRITE | PAGE_WRITECOPY |
                                 PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY);
        addr = (unsigned char *)mbi.BaseAddress + mbi.RegionSize;

        if (mbi.State != MEM_COMMIT || !w) continue;
        if (mbi.Protect & PAGE_GUARD) continue;
        if (mbi.RegionSize > (256u << 20)) continue;
        if ((unsigned char *)mbi.BaseAddress < img_hi && addr > img_lo) continue;

        {
            unsigned char *p = (unsigned char *)mbi.BaseAddress;
            size_t left = mbi.RegionSize;

            while (left >= OFF_FOV + 4) {
                SIZE_T got = 0, k;
                size_t want = left < chunk ? left : chunk;

                /* Reading through the API rather than dereferencing: the
                   game frees regions underneath us and this returns FALSE
                   where a plain read would take the process down. */
                if (!ReadProcessMemory(self, p, buf, want, &got) || got < OFF_FOV + 4) {
                    if (got == 0) break;
                }

                for (k = 0; k + OFF_FOV + 4 <= got; k += 8) {
                    ULONGLONG vt;
                    float cur;
                    memcpy(&vt, buf + k, 8);
                    if (vt != g_vtable) continue;
                    memcpy(&cur, buf + k + OFF_FOV, 4);
                    /* Written as a positive test on purpose. A NaN compares
                       false against everything, so the obvious rejection of
                       out of range values lets NaN straight through. */
                    if (!(cur >= 5.0f && cur <= 175.0f)) continue;
                    if (WriteProcessMemory(self, p + k + OFF_FOV, &fov, 4, NULL)) {
                        hits++;
                        if (g_debug) logf_("[tinyeden-fov]   camera at %p was %.1f\n",
                                           (void *)(p + k), cur);
                    }
                }

                if (got <= 4096) break;
                p    += got - 4096;   /* overlap so a match cannot straddle */
                left -= got - 4096;
            }
        }
    }

    VirtualFree(buf, 0, MEM_RELEASE);
    return hits;
}

static void apply(float fov)
{
    int live;
    patch_sites(fov);
    live = retune(fov);
    g_current = fov;
    logf_("[tinyeden-fov] FOV %.1f (%d live camera%s retuned)\n",
          fov, live, live == 1 ? "" : "s");
}

/* ---------------------------------------------------------- input paths */

static float read_cmdline(void)
{
    const wchar_t *c = GetCommandLineW();
    const wchar_t *p;

    for (p = c; *p; p++) {
        if ((p[0] != L'-') || (p == c)) continue;
        if (p[-1] != L' ' && p[-1] != L'"') continue;
        {
            const wchar_t *q = p + 1;
            if (*q == L'-') q++;
            if (_wcsnicmp(q, L"fov", 3) != 0) continue;
            if (q[3] == L'=' || q[3] == L':') return parse_fov_w(q + 4);
            logf_("[tinyeden-fov] -fov needs a value, e.g. -fov=105\n");
        }
    }
    return 0.0f;
}

static float read_config(void)
{
    HANDLE h;
    char buf[64];
    DWORD got = 0;

    h = CreateFileW(g_cfg, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                    NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return 0.0f;
    ReadFile(h, buf, sizeof buf - 1, &got, NULL);
    CloseHandle(h);
    buf[got] = 0;
    return parse_fov(buf);
}

static DWORD WINAPI watcher(LPVOID unused)
{
    (void)unused;
    for (;;) {
        float v;
        Sleep(1000);
        v = read_config();
        /* Only an actual edit wins, otherwise the file would immediately
           overwrite whatever came in on the command line. */
        if (v != 0.0f && v != g_last_file) {
            g_last_file = v;
            apply(v);
        }
    }
    return 0;
}

/* ------------------------------------------------------------- winmm ABI */

static HMODULE  g_real;
static UINT  (WINAPI *p_timeBeginPeriod)(UINT);
static UINT  (WINAPI *p_timeEndPeriod)(UINT);
static DWORD (WINAPI *p_timeGetTime)(void);
static UINT  (WINAPI *p_waveOutGetNumDevs)(void);

/* Loaded on first use, never from DllMain: calling the loader while it
   holds its own lock is how proxy DLLs deadlock. */
static void need_real(void)
{
    wchar_t path[MAX_PATH];
    UINT n;

    if (g_real) return;
    n = GetSystemDirectoryW(path, MAX_PATH - 16);
    if (!n) return;
    wcscpy(path + n, L"\\winmm.dll");
    g_real = LoadLibraryW(path);
    if (!g_real) return;

    p_timeBeginPeriod   = (UINT (WINAPI *)(UINT))(void *)GetProcAddress(g_real, "timeBeginPeriod");
    p_timeEndPeriod     = (UINT (WINAPI *)(UINT))(void *)GetProcAddress(g_real, "timeEndPeriod");
    p_timeGetTime       = (DWORD (WINAPI *)(void))(void *)GetProcAddress(g_real, "timeGetTime");
    p_waveOutGetNumDevs = (UINT (WINAPI *)(void))(void *)GetProcAddress(g_real, "waveOutGetNumDevs");
}

__declspec(dllexport) UINT WINAPI timeBeginPeriod(UINT p)
{
    need_real();
    return p_timeBeginPeriod ? p_timeBeginPeriod(p) : 0;
}

__declspec(dllexport) UINT WINAPI timeEndPeriod(UINT p)
{
    need_real();
    return p_timeEndPeriod ? p_timeEndPeriod(p) : 0;
}

__declspec(dllexport) DWORD WINAPI timeGetTime(void)
{
    need_real();
    return p_timeGetTime ? p_timeGetTime() : GetTickCount();
}

__declspec(dllexport) UINT WINAPI waveOutGetNumDevs(void)
{
    need_real();
    return p_waveOutGetNumDevs ? p_waveOutGetNumDevs() : 0;
}

/* ----------------------------------------------------------------- entry */

static int is_the_game(void)
{
    wchar_t path[MAX_PATH];
    const wchar_t *base;
    DWORD n = GetModuleFileNameW(NULL, path, MAX_PATH);

    if (!n || n >= MAX_PATH) return 0;
    base = wcsrchr(path, L'\\');
    base = base ? base + 1 : path;
    return _wcsicmp(base, L"CGH-Win64-Shipping.exe") == 0;
}

static void beside_dll(HMODULE self, const wchar_t *name, wchar_t *out)
{
    wchar_t path[MAX_PATH];
    wchar_t *slash;

    out[0] = 0;
    if (!GetModuleFileNameW(self, path, MAX_PATH)) return;
    slash = wcsrchr(path, L'\\');
    if (!slash) return;
    slash[1] = 0;
    wcscpy(out, path);
    wcscat(out, name);
}

BOOL WINAPI DllMain(HINSTANCE self, DWORD reason, LPVOID reserved)
{
    float fov;
    char env[32];

    (void)reserved;
    if (reason != DLL_PROCESS_ATTACH) return TRUE;
    DisableThreadLibraryCalls(self);

    if (!is_the_game()) return TRUE;

    beside_dll(self, L"tiny_eden_fov.txt", g_cfg);
    beside_dll(self, L"tiny_eden_fov.log", g_log);
    g_debug = GetEnvironmentVariableA("TINY_EDEN_FOV_DEBUG", env, sizeof env) > 0 && env[0] != '0';
    g_cameraactors = GetEnvironmentVariableA("TINY_EDEN_FOV_CAMERAACTORS", env, sizeof env) > 0 && env[0] != '0';
    if (GetEnvironmentVariableA("TINY_EDEN_FOV_LIVE", env, sizeof env) > 0 && env[0] == '0') g_live = 0;

    if (!locate()) return TRUE;

    g_last_file = read_config();
    fov = read_cmdline();
    if (fov == 0.0f && GetEnvironmentVariableA("TINY_EDEN_FOV", env, sizeof env) > 0)
        fov = parse_fov(env);
    if (fov == 0.0f) fov = g_last_file;

    if (fov != 0.0f) apply(fov);

    /* Runs once the loader lock is released. */
    if (g_live) CloseHandle(CreateThread(NULL, 0, watcher, NULL, 0, NULL));
    return TRUE;
}
