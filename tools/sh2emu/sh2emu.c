/* sh2emu.c - run a Sonic CD 32X build's SH2 side on the PC before burning (tools/sh2emu/README.md).
 * SH7604 (master SH2) interpreter + the 32X's SH2-side memory map + the SH2's 4 KB cache. Boots the disc's own SH2
 * program (SCDSH2.BIN: Kobo crt0 + cd32x/sh2_engboot.c), loads the disc files through command 38 / 46 as the
 * Sub-CPU does, starts the engine (SONICCD.PAK) and runs it with the 60 Hz vertical-blank interrupt. The Sub-CPU and
 * 68000 are emulated at the level of the COMM registers (cd32x/sub_main.c, cd32x/kobo/hw_md.s).
 * Reports: accesses where nothing answers on the 32X, writes into the engine image / loader / vector table, reads of
 * never-written memory, stale cache reads, stack bounds, exceptions, execution outside code, line 10 freezing.
 *   gcc -O2 -o tools/sh2emu/sh2emu tools/sh2emu/sh2emu.c
 *   tools/sh2emu/sh2emu cd32x/build_engine > sh2emu_log.txt; tail -25 sh2emu_log.txt                       */
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef uint8_t u8;
typedef int8_t s8;
typedef uint16_t u16;
typedef int16_t s16;
typedef uint32_t u32;
typedef int32_t s32;
typedef uint64_t u64;
typedef int64_t s64;

/* ------------------------------------------------------------------ options */
static int opt_frames      = 1500;  /* emulated frames to run (60 per second) */
static int opt_fill        = 1;     /* power-on garbage: 0 zeros, 1 pseudo-random */
static u32 opt_seed        = 12345;
static int opt_cache       = 1;     /* model the SH7604 cache */
static int opt_verbose     = 0;
static int opt_maxlog      = 4;     /* log lines per (event, PC) */
static u64 opt_trace_from  = ~0ull; /* instruction trace from this instruction count */
static u64 opt_trace_count = 0;
static int opt_cpf         = 383500; /* SH2 cycles per frame (23.01 MHz / 60) */
/* bus timing, in SH2 cycles beyond the instruction's own (32X hardware manual 1.12, "Access Timing of each CPU to
   32X Block": a bus cycle is 2 clocks plus the wait states) */
static int opt_cart_wait   = 15;     /* cart: "6 wait (min) ~ 15 wait (max)" per 16-bit access (-cartwait) */
static int opt_cart_rd     = -1;     /* uncached cart longword read, 2 accesses (-cartrd; default from the waits) */
static int opt_cart_fill   = 87;     /* cache line fill from the cart, 8 accesses in a row (-cartfill). Measured against
                                        the console (R1S, 16 Oct 2026): with 15 waits for single reads and 87 for a line,
                                        the emulator loads the stage in the console's 2.5-2.8 s and stays within 0.1 s
                                        of the console's video through the loop and the climb (20 s of play) */
static int opt_sdram_fill  = 11;     /* SDRAM "Read: 12 Clock / 8 Word" = one cache line */
static int opt_sdram_unc   = 11;     /* "the SDRAM is read using cache-through ... 8-word-burst-read-fixed" */
static int opt_fb_rd       = 8;      /* frame buffer read: "5 wait (min) ~ 12 wait (max)" (-fbread) */
static int opt_68k_word    = 75;     /* the 68000 copying one record word out of the frame buffer (sgb_poll: move.w
                                        (a0)+,(a1)+ / dbra = 24-26 68000 clocks = 3 SH2 cycles each) (-k68word) */
static int opt_68k_poll    = 150;    /* the 68000's command loop seeing a new COMM4 (half its ~100-clock loop) */
static int opt_stop_on_vbi_loss = 1;
static const char *opt_dir = "cd32x/build_engine";
static u32 opt_hooks[16];
static int opt_nhooks, opt_hook_max = 50, hook_count[16];
static int opt_dump_frame = -1;
static const char *opt_dump_file = 0;


/* ------------------------------------------------------------------ memories */
#define SDRAM_SIZE 0x40000
#define FB_SIZE 0x20000
#define CART_SIZE 0x400000
static u8 sdram[SDRAM_SIZE], sdram_def[SDRAM_SIZE];
static u32 sdram_wpc[SDRAM_SIZE];
static u8 fb[2][FB_SIZE], fb_def[2][FB_SIZE];
static u8 cart[CART_SIZE], cart_def[CART_SIZE];
static u32 cart_wpc[CART_SIZE / 2];
static u8 sysreg[0x100]; /* 0x4000-0x40FF, big-endian bytes */
static u8 vdpreg[0x100]; /* 0x4100-0x41FF */
static u8 cram[0x200];   /* 0x4200-0x43FF */
static u8 onchip[0x200]; /* 0xFFFFFE00-0xFFFFFFFF */

/* ------------------------------------------------------------------ CPU */
static u32 R[16], PC, PR, SR, GBR, VBR, MACH, MACL;
#define F_T 0x001u
#define F_S 0x002u
#define F_Q 0x100u
#define F_M 0x200u
static u64 cycles, insns;
static int in_slot;
static u32 cur_pc; /* address of the instruction being executed */
static int no_int_next;
static int stopped;
static int exc_taken;
static const char *stop_reason = "";

/* ------------------------------------------------------------------ symbols */
typedef struct {
    u32 a;
    char *name;
    char t; /* nm's type letter: T/t/W/w code */
} Sym;
static Sym *syms;
static int nsyms;
static int sym_cmp(const void *x, const void *y)
{
    u32 a = ((const Sym *)x)->a, b = ((const Sym *)y)->a;
    return a < b ? -1 : a > b;
}
static void add_sym(u32 a, const char *name, char t)
{
    syms            = realloc(syms, sizeof(Sym) * (nsyms + 1));
    syms[nsyms].a   = a;
    syms[nsyms].name = strdup(name);
    syms[nsyms].t   = t;
    nsyms++;
}
static int sym_code(int i) /* a function (not a static variable such as "ProcessScript(...)::told") */
{
    char t = syms[i].t;
    return t == 'T' || t == 't' || t == 'W' || t == 'w';
}
static void load_engine_syms(const char *path)
{
    FILE *f = fopen(path, "r");
    char line[1024];
    if (!f) {
        fprintf(stderr, "no %s\n", path);
        return;
    }
    while (fgets(line, sizeof line, f)) {
        unsigned a;
        char t;
        int pos = 0;
        if (sscanf(line, "%x %c %n", &a, &t, &pos) >= 2 && pos > 0) {
            char *nm = line + pos;
            nm[strcspn(nm, "\r\n")] = 0;
            /* the linker script's section marks (_scd_sdtext_start ...) share an address with a function: not names */
            size_t L = strlen(nm);
            if (!strncmp(nm, "scd_", 4) && ((L > 6 && !strcmp(nm + L - 6, "_start")) || (L > 4 && !strcmp(nm + L - 4, "_end")) ||
                                            (L > 5 && !strcmp(nm + L - 5, "_load"))))
                continue;
            add_sym(a, nm, t);
        }
    }
    fclose(f);
}
static void load_map_syms(const char *path) /* loader: "0x0000000006000820                _scd_vbi_sample" */
{
    FILE *f = fopen(path, "r");
    char line[1024];
    if (!f)
        return;
    while (fgets(line, sizeof line, f)) {
        unsigned long long a;
        char nm[512];
        if (sscanf(line, " 0x%llx %511s", &a, nm) == 2 && nm[0] == '_' && a >= 0x06000000ull && a < 0x06002000ull &&
            !strchr(line, '=') && !strstr(line, ".o"))
            add_sym((u32)a, nm + 1, 'T');
    }
    fclose(f);
}
static u32 sym_addr(const char *name) /* 0 if not there */
{
    for (int i = 0; i < nsyms; ++i)
        if (!strcmp(syms[i].name, name))
            return syms[i].a;
    return 0;
}
static int sym_index(u32 a) /* the symbol at or below a, -1 if none near */
{
    int lo = 0, hi = nsyms - 1, best = -1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        if (syms[mid].a <= a) {
            best = mid;
            lo   = mid + 1;
        }
        else
            hi = mid - 1;
    }
    if (best < 0 || a - syms[best].a > 0x40000)
        return -1;
    return best;
}
static const char *symname(u32 a, char *buf)
{
    int lo = 0, hi = nsyms - 1, best = -1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        if (syms[mid].a <= a) {
            best = mid;
            lo   = mid + 1;
        }
        else
            hi = mid - 1;
    }
    if (best < 0 || a - syms[best].a > 0x40000) {
        sprintf(buf, "%08X", a);
        return buf;
    }
    sprintf(buf, "%08X <%.60s+0x%X>", a, syms[best].name, a - syms[best].a);
    return buf;
}

/* ------------------------------------------------------------------ logging */
#define LOGH 4096
static u32 last_pcs[LOGH];
static int last_pos;
typedef struct {
    const char *kind;
    u32 pc;
    int count;
} LogKey;
static LogKey *logkeys;
static int nlogkeys;
static int frame_no(void) { return (int)(cycles / (u64)opt_cpf); }
static int log_allow(const char *kind, u32 pc)
{
    for (int i = 0; i < nlogkeys; ++i)
        if (logkeys[i].pc == pc && strcmp(logkeys[i].kind, kind) == 0)
            return ++logkeys[i].count <= opt_maxlog;
    logkeys = realloc(logkeys, sizeof(LogKey) * (nlogkeys + 1));
    logkeys[nlogkeys].kind  = kind;
    logkeys[nlogkeys].pc    = pc;
    logkeys[nlogkeys].count = 1;
    nlogkeys++;
    return 1;
}
static struct {
    const char *kind;
    u64 count;
    u32 first_pc;
    int first_frame;
} kinds[64];
static int nkinds;
static void count_kind(const char *kind)
{
    for (int i = 0; i < nkinds; ++i)
        if (!strcmp(kinds[i].kind, kind)) {
            kinds[i].count++;
            return;
        }
    if (nkinds < 64) {
        kinds[nkinds].kind        = kind;
        kinds[nkinds].count       = 1;
        kinds[nkinds].first_pc    = cur_pc;
        kinds[nkinds].first_frame = frame_no();
        nkinds++;
    }
}
/* 2 = a problem (on hardware this can hang or corrupt), 1 = a warning, 0 = information */
static int kind_severity(const char *k)
{
    static const char *info[] = { "W-COMM", "W-SYSREG", "W-VDP", "W-SAMPLING", "CCR", "VBR", "W-VECTOR", "W-BOOTREC", 0 };
    static const char *warn[] = { "R-UNDEF", 0 };
    for (int i = 0; info[i]; ++i)
        if (!strcmp(k, info[i]))
            return 0;
    for (int i = 0; warn[i]; ++i)
        if (!strcmp(k, warn[i]))
            return 1;
    return 2;
}
static void evlog(const char *kind, const char *fmt, ...)
{
    char b1[128], b2[128];
    count_kind(kind);
    if (!log_allow(kind, cur_pc))
        return;
    printf("[f%5d c%llu] %-10s PC=%s PR=%s R15=%08X ", frame_no(), (unsigned long long)cycles, kind, symname(cur_pc, b1),
           symname(PR, b2), R[15]);
    va_list ap;
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
    printf("\n");
}

/* shadow call stack */
#define CSD 256
static struct {
    u32 target, ret, sp;
} cstack[CSD];
static int csp;
static const char *opt_callers; /* -callers NAME: who calls NAME, while profiling */
/* REV R4: -set SYMBOL=VALUE: the engine variable SYMBOL (32 bits) set to VALUE when the engine reaches its first stage
   set-up (5C16: its data and zeroed arrays are ready) - e.g. -set scdDemoPad=1, the dev menu's DEMO PAD switch */
#define MAX_SETS 16
static const char *opt_set_name[MAX_SETS];
static u32 opt_set_value[MAX_SETS];
static int opt_nsets;
/* REV R4B: -pokeg FRAME:INDEX=VALUE - the game's global variable INDEX (globalVariables[INDEX], GameConfig.bin's order:
   25 Warp.Destination, 26 Warp.Timer ...) = VALUE from frame FRAME on, once */
#define MAX_POKES 256
static int opt_poke_frame[MAX_POKES], opt_poke_index[MAX_POKES], opt_npokes;
static u32 opt_poke_value[MAX_POKES];
static const char *opt_xsyms[8]; /* REV R4: -syms: more names to load */
static int opt_nxsyms;
static const char *opt_peek[8]; /* REV R4: -peek NAME[:size]: an SDRAM variable's value at each stage load and the end */
static int opt_peek_size[8], opt_npeek, opt_peek_every; /* -peekevery N: also every N frames */
static void print_peeks(const char *when);
static u32 callers_addr;
static u64 *callers_count;
static int prof_active_c; /* prof_active, for the call stack (declared before it) */
static int sym_index(u32 a);
/* -profile: the script cycles by object type and event (ProcessScript and everything it calls) */
static u32 scr_fn, scr_loop_addr, scr_ents_addr;
static int scr_active, scr_ctx;
static u32 scr_ret;
static u8 sdram_byte(u32 a);
static u32 sdram_read32(u32 a);
static void call_push(u32 target, u32 ret)
{
    if (scr_fn && target == scr_fn && !scr_active) {
        u32 loop = sdram_read32(scr_loop_addr);
        u8 type  = loop < 0x4A0 ? sdram_byte(scr_ents_addr + loop * 68 + 56) : 0;
        scr_active = 1;
        scr_ctx    = type * 4 + (int)(R[6] & 3);
        scr_ret    = ret;
    }
    if (callers_addr && target == callers_addr && prof_active_c) {
        int k = sym_index(cur_pc);
        callers_count[k < 0 ? nsyms : k]++;
    }
    if (csp < CSD) {
        cstack[csp].target = target;
        cstack[csp].ret    = ret;
        cstack[csp].sp     = R[15];
    }
    csp++;
}
static void call_ret(u32 to)
{
    if (scr_active && to == scr_ret)
        scr_active = 0;
    for (int i = (csp < CSD ? csp : CSD) - 1; i >= 0 && i >= csp - 8; --i)
        if (cstack[i].ret == to) {
            csp = i;
            return;
        }
}
static void dump_calls(void)
{
    char b[128];
    int n = csp < CSD ? csp : CSD;
    printf("  call stack (%d):\n", csp);
    for (int i = n - 1; i >= 0 && i >= n - 24; --i)
        printf("    %s  (ret %08X sp %08X)\n", symname(cstack[i].target, b), cstack[i].ret, cstack[i].sp);
}
/* ---- profiler (-profile FROM TO): every PROF_STEP cycles, the function the SH2 is in (self) and every function on
   the shadow call stack (inclusive), between two frames */
static int prof_from = -1, prof_to = -1;
#define PROF_STEP 64
static u64 *prof_self, *prof_incl, prof_samples, prof_next;
static void prof_sample(u32 pc)
{
    if (!prof_self) {
        prof_self = calloc((size_t)nsyms + 1, sizeof(u64));
        prof_incl = calloc((size_t)nsyms + 1, sizeof(u64));
    }
    prof_samples++;
    int k = sym_index(pc);
    prof_self[k < 0 ? nsyms : k]++;
    /* inclusive: each distinct function on the call stack once */
    static int seen[CSD + 1];
    int ns = 0, n = csp < CSD ? csp : CSD;
    if (k >= 0)
        seen[ns++] = k;
    for (int i = n - 1; i >= 0; --i) {
        int f = sym_index(cstack[i].target), dup = 0;
        if (f < 0)
            continue;
        for (int j = 0; j < ns; ++j)
            if (seen[j] == f)
                dup = 1;
        if (!dup && ns < CSD)
            seen[ns++] = f;
    }
    for (int j = 0; j < ns; ++j) prof_incl[seen[j]]++;
}
static int prof_cmp_self(const void *a, const void *b)
{
    u64 x = prof_self[*(const int *)a], y = prof_self[*(const int *)b];
    return x < y ? 1 : x > y ? -1 : 0;
}
static int prof_cmp_incl(const void *a, const void *b)
{
    u64 x = prof_incl[*(const int *)a], y = prof_incl[*(const int *)b];
    return x < y ? 1 : x > y ? -1 : 0;
}
static void prof_report(void)
{
    if (!prof_samples)
        return;
    int frames = prof_to - prof_from;
    double cyc_per_frame = (double)prof_samples * PROF_STEP / (frames > 0 ? frames : 1);
    printf("\n==================== PROFILE frames %d-%d (%.0f SH2 cycles per vertical blank sampled) ====================\n",
           prof_from, prof_to, cyc_per_frame);
    int *idx = malloc(sizeof(int) * (size_t)nsyms);
    for (int i = 0; i < nsyms; ++i) idx[i] = i;
    printf("self time (the function itself):\n");
    qsort(idx, (size_t)nsyms, sizeof(int), prof_cmp_self);
    for (int i = 0; i < 30 && prof_self[idx[i]]; ++i)
        printf("  %5.1f%%  %.60s\n", 100.0 * (double)prof_self[idx[i]] / (double)prof_samples, syms[idx[i]].name);
    if (prof_self[nsyms])
        printf("  %5.1f%%  (no symbol)\n", 100.0 * (double)prof_self[nsyms] / (double)prof_samples);
    printf("inclusive time (the function and everything it calls):\n");
    qsort(idx, (size_t)nsyms, sizeof(int), prof_cmp_incl);
    for (int i = 0; i < 30 && prof_incl[idx[i]]; ++i)
        printf("  %5.1f%%  %.60s\n", 100.0 * (double)prof_incl[idx[i]] / (double)prof_samples, syms[idx[i]].name);
    free(idx);
}
static u32 eng_text_hi; /* defined with the engine layout below */
static u32 sdt_lo, sdt_hi; /* (tentative) the engine's code in SDRAM - defined with the engine layout below */
/* the profiler's view of an instruction's stalls (-profile): by kind, and line fills by kind */
enum { PK_IF_CART, PK_D_CART, PK_IF_SD, PK_D_SD, PK_FB, PK_OTHER, PK_N };
static const char *pk_name[PK_N] = { "cart code", "cart data", "SDRAM code", "SDRAM data", "frame buffer", "other" };
static int pen_kind[PK_N], fill_kind[PK_N], pen_quiet;
static u64 *dpage_stall; /* -profile: cart data stalls per 4 KB of the cart */
static int prof_active;
/* ---- exact cost (-profile): every instruction's cycles, with its stalls by kind, to its function */
typedef struct {
    u64 cyc, insn, kind[PK_N], fills[PK_N];
} Cost;
static Cost *cost;
static int cost_k = -1;
static u32 cost_lo = 1, cost_hi;
static int cost_frames;
/* per engine frame: the cycles outside plat_present's wait for the vertical blank */
static int idle_k = -2;
static u32 presented_addr, presented_at_loaded, last_presented;
static u64 eng_busy, *eng_busy_list;
static int *eng_frame_at; /* the console frame (vertical blank count) each engine frame ended in */
static int opt_fl_from = -1, opt_fl_to = -1; /* -frames A B: each engine frame ending in console frames A..B */
static int eng_n, eng_cap;
static u32 *eng_fn_cur, *eng_fn_all; /* per engine frame: cycles by function (nsyms+1 each) */
static u32 scr_cur[1024], *scr_all;    /* per engine frame: script cycles by type * 4 + event */
static u32 sdram_read32(u32 a);
/* -pcprof NAME (up to 8): every instruction of these functions - count, cycles and stalls by kind per address */
#define PCP_MAX 8
static const char *pcp_name[PCP_MAX];
static int pcp_n;
static struct {
    u32 lo, hi;
    u64 *cnt, *cyc, *kind; /* per halfword of the function; kind: PK_N per halfword */
} pcp[PCP_MAX];
static void prof_account(u32 pc, u64 c)
{
    for (int i = 0; i < pcp_n; ++i)
        if (pc >= pcp[i].lo && pc < pcp[i].hi) {
            u32 h = (pc - pcp[i].lo) >> 1;
            pcp[i].cnt[h]++;
            pcp[i].cyc[h] += c;
            for (int k = 0; k < PK_N; ++k) pcp[i].kind[(size_t)h * PK_N + k] += (u64)pen_kind[k];
        }
    if (!cost)
        cost = calloc((size_t)nsyms + 1, sizeof(Cost));
    if (!(pc >= cost_lo && pc < cost_hi)) {
        int k = sym_index(pc);
        if (k < 0) {
            cost_k  = nsyms;
            cost_lo = 1;
            cost_hi = 0;
        }
        else {
            cost_k  = k;
            cost_lo = syms[k].a;
            cost_hi = k + 1 < nsyms ? syms[k + 1].a : syms[k].a + 0x1000;
        }
    }
    Cost *e = &cost[cost_k];
    e->cyc += c;
    e->insn++;
    if (idle_k == -2) {
        idle_k = -1;
        for (int i = 0; i < nsyms; ++i)
            if (!strncmp(syms[i].name, "plat_present(", 13) && sym_code(i))
                idle_k = i;
    }
    if (scr_active)
        scr_cur[scr_ctx] += (u32)c;
    if (cost_k != idle_k) {
        eng_busy += c;
        if (!eng_fn_cur)
            eng_fn_cur = calloc((size_t)nsyms + 1, sizeof(u32));
        eng_fn_cur[cost_k] += (u32)c;
    }
    if (presented_addr) {
        u32 n = sdram_read32(presented_addr);
        if (n != last_presented) { /* an engine frame ended */
            if (last_presented && eng_busy) {
                if (eng_n == eng_cap) {
                    eng_cap       = eng_cap ? eng_cap * 2 : 1024;
                    eng_busy_list = realloc(eng_busy_list, sizeof(u64) * (size_t)eng_cap);
                    eng_frame_at  = realloc(eng_frame_at, sizeof(int) * (size_t)eng_cap);
                    eng_fn_all    = realloc(eng_fn_all, sizeof(u32) * ((size_t)nsyms + 1) * (size_t)eng_cap);
                    scr_all       = realloc(scr_all, sizeof(u32) * 1024 * (size_t)eng_cap);
                }
                memcpy(scr_all + (size_t)eng_n * 1024, scr_cur, sizeof scr_cur);
                if (eng_fn_cur)
                    memcpy(eng_fn_all + (size_t)eng_n * ((size_t)nsyms + 1), eng_fn_cur, sizeof(u32) * ((size_t)nsyms + 1));
                else
                    memset(eng_fn_all + (size_t)eng_n * ((size_t)nsyms + 1), 0, sizeof(u32) * ((size_t)nsyms + 1));
                eng_frame_at[eng_n]    = frame_no();
                eng_busy_list[eng_n++] = eng_busy;
            }
            if (eng_fn_cur)
                memset(eng_fn_cur, 0, sizeof(u32) * ((size_t)nsyms + 1));
            memset(scr_cur, 0, sizeof scr_cur);
            eng_busy       = 0;
            last_presented = n;
        }
    }
    for (int i = 0; i < PK_N; ++i) {
        e->kind[i] += (u64)pen_kind[i];
        e->fills[i] += (u64)fill_kind[i];
    }
}
static int cost_cmp(const void *a, const void *b)
{
    u64 x = cost[*(const int *)a].cyc, y = cost[*(const int *)b].cyc;
    return x < y ? 1 : x > y ? -1 : 0;
}
static const char *cart_area(u32 off, char *buf) /* what lies at this cart offset (cd32x/engine/plat_mars.cpp layout) */
{
    static const char *part[19] = { "scriptcode", "jumptable", "chunks", "collision", "scriptframes", "animframes",
                                    "animlist", "arctan", "stagelist", "typenames", "globalnames", "animfiles", "font",
                                    "trig", "gfxdata", "tilegfx", "blend", "datapack", "native scripts" };
    if (off < 0x080000) {
        u32 a = 0x02000000u + off;
        if (a < eng_text_hi) {
            char b[128];
            sprintf(buf, "engine code (%s)", symname(a, b));
        }
        else
            sprintf(buf, "engine constants / data image");
        return buf;
    }
    if (off < 0x300000) { /* the scene slot (cd32x/engine/plat_mars.cpp CART_STAGE - CART_PACK) */
        const u8 *h = cart + 0x080000;
        u32 n = (u32)h[8] << 24 | (u32)h[9] << 16 | (u32)h[10] << 8 | h[11];
        if (h[0] == 'S' && h[1] == 'C' && h[2] == 'C' && h[3] == 'B')
            for (u32 k = 0; k < n && k < 19; ++k) {
                const u8 *e = h + 12 + 20 * k;
                u32 o = (u32)e[0] << 24 | (u32)e[1] << 16 | (u32)e[2] << 8 | e[3];
                u32 l = (u32)e[4] << 24 | (u32)e[5] << 16 | (u32)e[6] << 8 | e[7];
                if (off - 0x080000 >= o && off - 0x080000 < o + l) {
                    sprintf(buf, "stage image: %s +%X", part[k], off - 0x080000 - o);
                    return buf;
                }
            }
        sprintf(buf, "stage image");
        return buf;
    }
    if (off < 0x380000)
        sprintf(buf, "data pack (SCDDATA.PAK)");
    else
        sprintf(buf, "Genesis plane data (MDG)");
    return buf;
}
static void pcp_report(void)
{
    double F = cost_frames > 0 ? cost_frames : 1;
    for (int i = 0; i < pcp_n; ++i) {
        if (!pcp[i].cnt)
            continue;
        printf("\n==================== PC PROFILE %s (%08X-%08X), per frame: count cycles | cart code, cart data, SDRAM "
               "code, SDRAM data, fb, other ====================\n", pcp_name[i], pcp[i].lo, pcp[i].hi);
        for (u32 h = 0; h < (pcp[i].hi - pcp[i].lo) >> 1; ++h) {
            if (!pcp[i].cnt[h])
                continue;
            printf("PCP %08X %10.2f %10.1f |", pcp[i].lo + h * 2, pcp[i].cnt[h] / F, pcp[i].cyc[h] / F);
            for (int k = 0; k < PK_N; ++k) printf(" %8.1f", pcp[i].kind[(size_t)h * PK_N + k] / F);
            printf("\n");
        }
    }
}
static void cost_report(void)
{
    if (!cost || cost_frames <= 0)
        return;
    double F = cost_frames, vb = opt_cpf;
    Cost all;
    memset(&all, 0, sizeof all);
    int idle = -1; /* plat_present: the engine's wait for the vertical blank */
    for (int i = 0; i < nsyms; ++i)
        if (!strncmp(syms[i].name, "plat_present(", 13) && sym_code(i))
            idle = i;
    for (int i = 0; i <= nsyms; ++i) {
        all.cyc += cost[i].cyc;
        all.insn += cost[i].insn;
        for (int k = 0; k < PK_N; ++k) {
            all.kind[k] += cost[i].kind[k];
            all.fills[k] += cost[i].fills[k];
        }
    }
    u64 idle_cyc = idle >= 0 ? cost[idle].cyc : 0;
    printf("\n==================== COST frames %d-%d: exact SH2 cycles per frame (a vertical blank = %d) ====================\n",
           prof_from, prof_to, opt_cpf);
    printf("bus timing: cart word read +%d, cart line fill +%d, SDRAM line fill +%d, frame buffer read +%d, 68000 copy "
           "%d per record word\n", opt_cart_rd / 2, opt_cart_fill, opt_sdram_fill, opt_fb_rd, opt_68k_word);
    printf("busy (all but the vertical-blank wait in plat_present): %.0f cycles per vertical blank (%.0f%%)\n",
           (all.cyc - idle_cyc) / F, 100.0 * (all.cyc - idle_cyc) / F / vb);
    if (eng_n > 2) {
        u64 *v = malloc(sizeof(u64) * (size_t)eng_n);
        memcpy(v, eng_busy_list, sizeof(u64) * (size_t)eng_n);
        for (int a = 1; a < eng_n; ++a) { /* insertion sort: a few thousand at most */
            u64 x = v[a];
            int b = a - 1;
            while (b >= 0 && v[b] > x) {
                v[b + 1] = v[b];
                --b;
            }
            v[b + 1] = x;
        }
        double sum = 0;
        int under1 = 0, under08 = 0;
        for (int a = 0; a < eng_n; ++a) {
            sum += (double)v[a];
            under1 += v[a] < (u64)opt_cpf;
            under08 += v[a] < (u64)(opt_cpf * 8 / 10);
        }
        printf("ENGINE FRAMES     %d: busy cycles each - mean %.0f (%.2f vertical blanks), median %.2f, 90%% %.2f, max %.2f; "
               "%d%% fit in one vertical blank, %d%% in 0.8 (60 frames per second needs them all in one)\n",
               eng_n, sum / eng_n, sum / eng_n / vb, v[eng_n / 2] / vb, v[eng_n * 9 / 10] / vb, v[eng_n - 1] / vb,
               100 * under1 / eng_n, 100 * under08 / eng_n);
        /* what the slowest tenth of the frames spend more on (per engine frame, in vertical blanks) */
        if (eng_fn_all) {
            u64 cut = v[eng_n * 9 / 10];
            size_t W = (size_t)nsyms + 1;
            double *slow = calloc(W, sizeof(double)), *mean = calloc(W, sizeof(double));
            int ns = 0;
            for (int f = 0; f < eng_n; ++f) {
                const u32 *row = eng_fn_all + (size_t)f * W;
                int isslow = eng_busy_list[f] >= cut;
                ns += isslow;
                for (size_t k = 0; k < W; ++k) {
                    mean[k] += row[k];
                    if (isslow)
                        slow[k] += row[k];
                }
            }
            printf("  per engine frame, all frames vs the slowest tenth (%d frames), in vertical blanks:\n", ns);
            for (int r = 0; r < 14; ++r) {
                size_t best = W;
                for (size_t k = 0; k < W; ++k)
                    if (slow[k] > 0 && (best == W || slow[k] > slow[best]))
                        best = k;
                if (best == W)
                    break;
                printf("    %5.2f  %5.2f  %.60s\n", mean[best] / eng_n / vb, slow[best] / (ns ? ns : 1) / vb,
                       best < (size_t)nsyms ? syms[best].name : "(no symbol)");
                slow[best] = 0;
            }
            free(slow);
            free(mean);
            /* the slowest engine frames, each with the functions that took most of it */
            printf("  the slowest engine frames (busy cycles in vertical blanks, the console frame it ended in):\n");
            int *ord = malloc(sizeof(int) * (size_t)eng_n);
            for (int f = 0; f < eng_n; ++f) ord[f] = f;
            for (int a = 0; a < 6 && a < eng_n; ++a) {
                int b = a;
                for (int c = a + 1; c < eng_n; ++c)
                    if (eng_busy_list[ord[c]] > eng_busy_list[ord[b]])
                        b = c;
                int t = ord[a];
                ord[a] = ord[b];
                ord[b] = t;
                int f = ord[a];
                const u32 *row = eng_fn_all + (size_t)f * W;
                printf("    %.2f  frame %d:", eng_busy_list[f] / vb, eng_frame_at[f]);
                u32 shown[8];
                int ns2 = 0;
                for (int r = 0; r < 8; ++r) {
                    size_t best = W;
                    for (size_t k = 0; k < W; ++k) {
                        int dup = 0;
                        for (int q = 0; q < ns2; ++q) dup |= shown[q] == (u32)k;
                        if (!dup && row[k] && (best == W || row[k] > row[best]))
                            best = k;
                    }
                    if (best == W)
                        break;
                    shown[ns2++] = (u32)best;
                    printf(" %.30s %.3f%s", best < (size_t)nsyms ? syms[best].name : "?", row[best] / vb, r < 7 ? "," : "");
                }
                printf("\n");
            }
            free(ord);
            if (opt_fl_from >= 0) { /* -frames A B */
                printf("  engine frames ending in console frames %d-%d (busy, then the functions that took most of it):\n",
                       opt_fl_from, opt_fl_to);
                for (int f = 0; f < eng_n; ++f) {
                    if (eng_frame_at[f] < opt_fl_from || eng_frame_at[f] > opt_fl_to)
                        continue;
                    const u32 *row = eng_fn_all + (size_t)f * W;
                    printf("    %.3f  frame %d:", eng_busy_list[f] / vb, eng_frame_at[f]);
                    u32 shown[8];
                    int ns2 = 0;
                    for (int r = 0; r < 8; ++r) {
                        size_t best = W;
                        for (size_t k = 0; k < W; ++k) {
                            int dup = 0;
                            for (int q = 0; q < ns2; ++q) dup |= shown[q] == (u32)k;
                            if (!dup && row[k] && (best == W || row[k] > row[best]))
                                best = k;
                        }
                        if (best == W)
                            break;
                        shown[ns2++] = (u32)best;
                        printf(" %.30s %.3f%s", best < (size_t)nsyms ? syms[best].name : "?", row[best] / vb, r < 7 ? "," : "");
                    }
                    printf("\n");
                }
            }
        }
        if (scr_all) { /* the scripts: by object type and event */
            static const char *ev[4] = { "main", "player", "draw", "setup" };
            u64 cut = v[eng_n * 9 / 10];
            double slow[1024] = { 0 }, mean[1024] = { 0 };
            int ns = 0;
            for (int f = 0; f < eng_n; ++f) {
                const u32 *row = scr_all + (size_t)f * 1024;
                int isslow = eng_busy_list[f] >= cut;
                ns += isslow;
                for (int k = 0; k < 1024; ++k) {
                    mean[k] += row[k];
                    if (isslow)
                        slow[k] += row[k];
                }
            }
            const u8 *h = cart + 0x080000; /* the stage image's type names (part 9) */
            u32 tn = (u32)h[12 + 20 * 9] << 24 | (u32)h[13 + 20 * 9] << 16 | (u32)h[14 + 20 * 9] << 8 | h[15 + 20 * 9];
            printf("  scripts per engine frame by object type and event, all frames vs the slowest tenth, in vertical blanks:\n");
            for (int r = 0; r < 16; ++r) {
                int best = -1;
                for (int k = 0; k < 1024; ++k)
                    if (slow[k] > 0 && (best < 0 || slow[k] > slow[best]))
                        best = k;
                if (best < 0)
                    break;
                char nm[0x41];
                memcpy(nm, h + tn + (u32)(best / 4) * 0x40, 0x40);
                nm[0x40] = 0;
                printf("    %5.3f  %5.3f  type %3d %-24.24s %s\n", mean[best] / eng_n / vb, slow[best] / (ns ? ns : 1) / vb,
                       best / 4, nm, ev[best & 3]);
                slow[best] = 0;
            }
        }
        free(v);
    }
    printf("  instructions %.0f per frame; stalls inside that: ", all.insn / F);
    for (int k = 0; k < PK_N; ++k)
        printf("%s %.0f%s", pk_name[k], all.kind[k] / F, k + 1 < PK_N ? ", " : "\n");
    printf("  cache line fills per frame: ");
    for (int k = 0; k < PK_N; ++k)
        printf("%s %.0f%s", pk_name[k], all.fills[k] / F, k + 1 < PK_N ? ", " : "\n");
    int *idx = malloc(sizeof(int) * ((size_t)nsyms + 1));
    for (int i = 0; i <= nsyms; ++i) idx[i] = i;
    qsort(idx, (size_t)nsyms + 1, sizeof(int), cost_cmp);
    printf("by function, per frame (the stalls are part of the cycles); 'in SDRAM' = the cycles if the function's code "
           "were in SDRAM:\n");
    printf("  %9s %6s %8s %9s %9s %8s %8s %7s %6s %9s  %s\n", "cycles", "%blank", "insns", "cart code", "cart data",
           "SDRAM", "fb", "other", "size", "in SDRAM", "function");
    for (int i = 0; i < 40 && cost[idx[i]].cyc; ++i) {
        const Cost *e = &cost[idx[i]];
        int k = idx[i];
        u32 size = k < nsyms ? (k + 1 < nsyms ? syms[k + 1].a - syms[k].a : 0) : 0;
        double insd = (double)e->cyc - (double)e->kind[PK_IF_CART] + (double)e->fills[PK_IF_CART] * opt_sdram_fill;
        printf("  %9.0f %5.1f%% %8.0f %9.0f %9.0f %8.0f %8.0f %7.0f %6u %9.0f  %.70s\n", e->cyc / F, 100.0 * e->cyc / F / vb,
               e->insn / F, e->kind[PK_IF_CART] / F, e->kind[PK_D_CART] / F, (e->kind[PK_IF_SD] + e->kind[PK_D_SD]) / F,
               e->kind[PK_FB] / F, e->kind[PK_OTHER] / F, size, insd / F, k < nsyms ? syms[k].name : "(no symbol)");
    }
    /* the code to move: functions by cart-code stall, with their sizes */
    printf("cart code stalls by function, cumulative (what moving code to SDRAM would save):\n");
    {
        int *j = malloc(sizeof(int) * ((size_t)nsyms + 1));
        int n = 0;
        for (int i = 0; i < nsyms; ++i)
            if (cost[i].kind[PK_IF_CART])
                j[n++] = i;
        for (int a = 0; a < n; ++a) /* by stall, largest first */
            for (int b = a + 1; b < n; ++b)
                if (cost[j[b]].kind[PK_IF_CART] > cost[j[a]].kind[PK_IF_CART]) {
                    int t = j[a];
                    j[a]  = j[b];
                    j[b]  = t;
                }
        double cum = 0, tot = (double)all.kind[PK_IF_CART];
        u32 bytes = 0;
        for (int a = 0; a < n && a < 40; ++a) {
            int k = j[a];
            u32 size = k + 1 < nsyms ? syms[k + 1].a - syms[k].a : 0;
            bytes += size;
            double save = (double)cost[k].kind[PK_IF_CART] - (double)cost[k].fills[PK_IF_CART] * opt_sdram_fill;
            cum += save;
            printf("  %8.0f saved/frame, %5.1f%% of all cart code stalls so far, %6u bytes so far  %.60s\n", save / F,
                   100.0 * cum / (tot > 0 ? tot : 1), bytes, syms[k].name);
        }
        free(j);
    }
    if (sdt_hi) { /* R1X: what each function in SDRAM saves against the cart (its code line fills x (cart - SDRAM)) */
        printf("code in SDRAM (.sdtext): what each function saves against the cart, per frame:\n");
        int *j = malloc(sizeof(int) * ((size_t)nsyms + 1));
        int n = 0;
        for (int i = 0; i < nsyms; ++i)
            if (syms[i].a >= sdt_lo && syms[i].a < sdt_hi && sym_code(i))
                j[n++] = i;
        for (int a = 0; a < n; ++a)
            for (int b = a + 1; b < n; ++b)
                if (cost[j[b]].fills[PK_IF_SD] > cost[j[a]].fills[PK_IF_SD]) {
                    int t = j[a];
                    j[a]  = j[b];
                    j[b]  = t;
                }
        for (int a = 0; a < n; ++a) {
            int k = j[a];
            u32 size = k + 1 < nsyms ? syms[k + 1].a - syms[k].a : 0;
            if (syms[k + 1].a > sdt_hi)
                size = sdt_hi - syms[k].a;
            double save = (double)cost[k].fills[PK_IF_SD] * (opt_cart_fill - opt_sdram_fill) / F;
            printf("  %8.0f saved/frame %6u bytes %6.2f a byte  %.60s\n", save, size, size ? save / size : 0, syms[k].name);
        }
        free(j);
    }
    if (callers_count) {
        printf("calls to %s per frame, by caller:\n", opt_callers);
        for (int r = 0; r < 12; ++r) {
            int best = -1;
            for (int k = 0; k <= nsyms; ++k)
                if (callers_count[k] && (best < 0 || callers_count[k] > callers_count[best]))
                    best = k;
            if (best < 0)
                break;
            printf("  %8.1f  %.70s\n", callers_count[best] / F, best < nsyms ? syms[best].name : "(no symbol)");
            callers_count[best] = 0;
        }
    }
    if (dpage_stall) {
        printf("cart data stalls by 4 KB of the cart (per frame):\n");
        for (int r = 0; r < 16; ++r) {
            int best = -1;
            for (int pg = 0; pg < 1024; ++pg)
                if (dpage_stall[pg] && (best < 0 || dpage_stall[pg] > dpage_stall[best]))
                    best = pg;
            if (best < 0)
                break;
            char b[160];
            printf("  %8.0f  cart %06X  %s\n", dpage_stall[best] / F, best << 12, cart_area((u32)best << 12, b));
            dpage_stall[best] = 0;
        }
    }
    free(idx);
}
static void dump_state(void)
{
    char b[128];
    printf("  PC=%s PR=%s SR=%08X GBR=%08X VBR=%08X MACH=%08X MACL=%08X\n", symname(PC, b), symname(PR, b + 64), SR, GBR,
           VBR, MACH, MACL);
    for (int i = 0; i < 16; ++i) printf("  R%-2d=%08X%s", i, R[i], (i & 3) == 3 ? "\n" : "");
    dump_calls();
    printf("  last PCs:\n");
    for (int k = 32; k >= 1; --k) {
        u32 p = last_pcs[(last_pos - k) & (LOGH - 1)];
        printf("    %s\n", symname(p, b));
    }
}
static void stop(const char *why)
{
    stopped     = 1;
    stop_reason = why;
}

/* ------------------------------------------------------------------ the 32X side registers */
static u16 rd_be16(const u8 *p) { return (u16)(p[0] << 8 | p[1]); }
static void wr_be16(u8 *p, u16 v)
{
    p[0] = (u8)(v >> 8);
    p[1] = (u8)v;
}
#define COMM(n) rd_be16(&sysreg[0x20 + (n)])
#define SET_COMM(n, v) wr_be16(&sysreg[0x20 + (n)], (u16)(v))
static int sgb_on;      /* the 68000 takes plane records (SGB_FLAG) - the model further down */
static u64 rec_sent;
static u16 intmsk_lo;   /* interrupt enables (byte 0x4001) */
static int fm = 0;      /* FM: 1 = the SH2 owns the VDP */
static int vint_pending, cmdint_pending;
static int fs_bit;      /* frame buffer select: the buffer the SH2 and the 68000 reach (the other one is shown) */
static int fs_req = -1; /* REV R4: a swap asked for, done at the next vertical blank */
static int rv_active;   /* the 68000 holds the cart (Kobo CMD 46 copy) */
static u16 last_state = 0xFFFF;
static u64 vbi_count;
static int frame_in_vblank(void)
{
    u64 pos = cycles % (u64)opt_cpf;
    return pos >= (u64)opt_cpf * 224 / 262;
}

/* ------------------------------------------------------------------ cache (SH7604: 4 ways x 64 x 16 bytes) */
static struct {
    u32 tag;
    int valid;
    u8 d[16];
    u64 used;
} cl[64][4];
static u8 ccr;
static u64 lru_clock;
static void cache_purge(void)
{
    memset(cl, 0, sizeof cl);
}

/* ------------------------------------------------------------------ physical bus */
enum { RG_NONE, RG_ROM, RG_SYS, RG_VDP, RG_CRAM, RG_CART, RG_FB, RG_FBOV, RG_SDRAM };
static int region(u32 p, u32 *off)
{
    p &= 0x1FFFFFFF;
    if (p < 0x4000) {
        *off = p;
        return RG_ROM;
    }
    if (p < 0x4100) {
        *off = p - 0x4000;
        return RG_SYS;
    }
    if (p < 0x4200) {
        *off = p - 0x4100;
        return RG_VDP;
    }
    if (p < 0x4400) {
        *off = p - 0x4200;
        return RG_CRAM;
    }
    if (p >= 0x02000000 && p < 0x02400000) {
        *off = p - 0x02000000;
        return RG_CART;
    }
    if (p >= 0x04000000 && p < 0x04020000) {
        *off = p - 0x04000000;
        return RG_FB;
    }
    if (p >= 0x04020000 && p < 0x04040000) {
        *off = p - 0x04020000;
        return RG_FBOV;
    }
    if (p >= 0x06000000 && p < 0x06040000) {
        *off = p - 0x06000000;
        return RG_SDRAM;
    }
    *off = p;
    return RG_NONE;
}
static u8 sys_read8(u32 off)
{
    if (off == 0x00) { /* FM, cart / adapter status bits */
        return (u8)((fm ? 0x80 : 0x00) | 0x02); /* bit 9 ADEN = 1 (adapter on); bit 8 CART: 0 = cart inserted */
    }
    if (off == 0x01)
        return (u8)intmsk_lo;
    return sysreg[off];
}
static u8 vdp_read8(u32 off)
{
    if (off == 0x0A || off == 0x0B) { /* frame buffer control: VBLK, HBLK, PEN, FEN, FS */
        u16 v = (u16)((frame_in_vblank() ? 0x8000 : 0) | 0x2000 | (fs_bit & 1));
        return off == 0x0A ? (u8)(v >> 8) : (u8)v;
    }
    return vdpreg[off];
}
static int bus_penalty;
static void pen_note(int rg, int ifetch, int add, u32 p)
{
    if (pen_quiet)
        return;
    int k = rg == RG_CART ? (ifetch ? PK_IF_CART : PK_D_CART)
          : rg == RG_SDRAM ? (ifetch ? PK_IF_SD : PK_D_SD)
          : (rg == RG_FB || rg == RG_FBOV) ? PK_FB : PK_OTHER;
    pen_kind[k] += add;
    if (k == PK_D_CART && prof_active && dpage_stall)
        dpage_stall[(p & 0x3FFFFF) >> 12] += (u64)add;
}
/* ---- the frame buffer's 4-word write FIFO (32X manual 1.12: "a 4 word component of FIFO is held for frame buffer
   writing ... 5 clock if FIFO is FULL"). SH2 writes wait in it; a read waits until it is empty. When the SH2 gives
   the frame buffer to the 68000 (FM 1 -> 0) with words still in it, the manual guarantees nothing ("access from SH2
   is interrupted by force and the operation that follows is not guaranteed"): they are dropped here and logged. */
static int opt_fb_word = 5;     /* SH2 cycles per frame-buffer word leaving the FIFO */
static int opt_fifo = 1;        /* model the FIFO */
static struct {
    int bank, ov, n;
    u32 off;
    u8 b[2];
    u64 t;
} fbq[8];
static int fbq_n;
static u64 fbq_last_t, fifo_lost_words, fifo_lost_events;
static void fbq_commit_one(void)
{
    for (int i = 0; i < fbq[0].n; ++i) {
        u32 o = (fbq[0].off + i) & (FB_SIZE - 1);
        if (fbq[0].ov && !fbq[0].b[i])
            continue; /* the overwrite image ignores 0 bytes */
        fb[fbq[0].bank][o]     = fbq[0].b[i];
        fb_def[fbq[0].bank][o] = 1;
    }
    for (int i = 1; i < fbq_n; ++i) fbq[i - 1] = fbq[i];
    fbq_n--;
}
static u64 now_cycles(void) { return cycles + (u64)bus_penalty; }
static void fbq_drain(u64 now)
{
    while (fbq_n && fbq[0].t <= now) fbq_commit_one();
}
static void fbq_flush_wait(void) /* an SH2 read of the frame buffer waits until the FIFO is empty */
{
    if (!fbq_n)
        return;
    u64 now = now_cycles();
    if (fbq[fbq_n - 1].t > now) {
        bus_penalty += (int)(fbq[fbq_n - 1].t - now);
        if (!pen_quiet)
            pen_kind[PK_FB] += (int)(fbq[fbq_n - 1].t - now);
    }
    while (fbq_n) fbq_commit_one();
}
static void fb_queue_word(int bank, u32 off, const u8 *b, int n, int ov)
{
    u64 now = now_cycles();
    fbq_drain(now);
    if (fbq_n >= 4) { /* full: the SH2 waits for the oldest word */
        bus_penalty += (int)(fbq[0].t - now);
        if (!pen_quiet)
            pen_kind[PK_FB] += (int)(fbq[0].t - now);
        fbq_commit_one();
        now = now_cycles();
    }
    u64 t = (fbq_last_t > now ? fbq_last_t : now) + (u64)opt_fb_word;
    fbq_last_t      = t;
    fbq[fbq_n].bank = bank;
    fbq[fbq_n].ov   = ov;
    fbq[fbq_n].n    = n;
    fbq[fbq_n].off  = off;
    fbq[fbq_n].b[0] = b[0];
    fbq[fbq_n].b[1] = n > 1 ? b[1] : 0;
    fbq[fbq_n].t    = t;
    fbq_n++;
}
static void fm_to_68000(void) /* FM 1 -> 0: what is still in the FIFO is not guaranteed - dropped */
{
    if (!opt_fifo)
        return;
    fbq_drain(now_cycles());
    if (fbq_n) {
        fifo_lost_words += (u64)fbq_n;
        fifo_lost_events++;
        evlog("FB-FIFO-LOST", "%d frame-buffer word(s) still in the write FIFO when the SH2 gave the frame buffer to "
                              "the 68000 (FM 0) - first at FB+%05X: dropped (not guaranteed by the 32X)", fbq_n, fbq[0].off);
        fbq_n = 0;
    }
}
static u32 bus_read(u32 p, int size, int ifetch)
{
    u32 off;
    int rg = region(p, &off);
    u32 v = 0;
    if (opt_fifo && (rg == RG_FB || rg == RG_FBOV))
        fbq_flush_wait();
    for (int i = 0; i < size; ++i) {
        u32 o = off + i;
        u8 b  = 0;
        switch (rg) {
            case RG_ROM:
                b = 0;
                break;
            case RG_SYS:
                b = sys_read8(o & 0xFF);
                break;
            case RG_VDP:
                b = vdp_read8(o & 0xFF);
                break;
            case RG_CRAM:
                b = cram[o & 0x1FF];
                break;
            case RG_CART:
                b = cart[o & (CART_SIZE - 1)];
                break;
            case RG_FB:
            case RG_FBOV:
                b = fb[fs_bit][o & (FB_SIZE - 1)];
                break;
            case RG_SDRAM:
                b = sdram[o & (SDRAM_SIZE - 1)];
                break;
            default:
                b = 0;
        }
        v = (v << 8) | b;
    }
    int add;
    switch (rg) {
        case RG_CART:
            add = size == 4 ? opt_cart_rd : opt_cart_rd / 2;
            break;
        case RG_SDRAM:
            add = opt_sdram_unc;
            break;
        case RG_FB:
        case RG_FBOV:
            add = (size == 4 ? 2 : 1) * opt_fb_rd;
            break;
        default:
            add = 3;
    }
    bus_penalty += add;
    pen_note(rg, ifetch, add, p);
    return v;
}
static void onchip_write(u32 off, u32 v, int size);
static void bus_write(u32 p, u32 v, int size)
{
    u32 off;
    int rg = region(p, &off);
    if (opt_fifo && (rg == RG_FB || rg == RG_FBOV)) { /* into the FIFO, a word at a time */
        u8 bytes[4];
        for (int i = 0; i < size; ++i) bytes[i] = (u8)(v >> (8 * (size - 1 - i)));
        if (size == 1)
            fb_queue_word(fs_bit, off & (FB_SIZE - 1), bytes, 1, rg == RG_FBOV);
        else
            for (int i = 0; i < size; i += 2) fb_queue_word(fs_bit, (off + i) & (FB_SIZE - 1), bytes + i, 2, rg == RG_FBOV);
        return;
    }
    for (int i = 0; i < size; ++i) {
        u32 o = off + i;
        u8 b  = (u8)(v >> (8 * (size - 1 - i)));
        switch (rg) {
            case RG_ROM:
                break;
            case RG_SYS:
                o &= 0xFF;
                if (o == 0x00) {
                    int nf = (b & 0x80) != 0;
                    if (fm && !nf)
                        fm_to_68000();
                    fm = nf;
                }
                else if (o == 0x01)
                    intmsk_lo = b;
                else {
                    sysreg[o] = b;
                    if (o == 0x16 || o == 0x17)
                        vint_pending = 0;
                    if (o == 0x1A || o == 0x1B)
                        cmdint_pending = 0;
                }
                break;
            case RG_VDP:
                o &= 0xFF;
                vdpreg[o] = b;
                if (o == 0x0B)
                    fs_req = b & 1; /* REV R4: the swap happens at the next vertical blank (FS reads the old value until then) */
                break;
            case RG_CRAM:
                cram[o & 0x1FF] = b;
                break;
            case RG_CART:
                o &= CART_SIZE - 1;
                cart[o]          = b;
                cart_def[o]      = 1;
                cart_wpc[o >> 1] = cur_pc;
                break;
            case RG_FB:
                o &= FB_SIZE - 1;
                fb[fs_bit][o]     = b;
                fb_def[fs_bit][o] = 1;
                break;
            case RG_FBOV:
                o &= FB_SIZE - 1;
                if (b) {
                    fb[fs_bit][o]     = b;
                    fb_def[fs_bit][o] = 1;
                }
                break;
            case RG_SDRAM:
                o &= SDRAM_SIZE - 1;
                sdram[o]     = b;
                sdram_def[o] = 1;
                sdram_wpc[o] = cur_pc;
                break;
            default:
                break;
        }
    }
    int add;
    switch (rg) {
        case RG_CART:
            add = (size == 4 ? 2 : 1) * (2 + opt_cart_wait) - 1;
            break;
        case RG_SDRAM:
            add = size == 4 ? 3 : 1; /* "Write: 2 Clock / 1 Word" */
            break;
        default:
            add = 3;
    }
    bus_penalty += add;
    pen_note(rg, 0, add, p);
}

/* ------------------------------------------------------------------ the CPU's view: areas, cache, checks */
static const u32 ENG_TEXT_LO = 0x02000000;
static u32 ENG_IMG_HI = 0x0203A1F0, eng_img_end_off = 0x3A1F0; /* the engine image in the cart (code, rodata, data LMA) */
static u32 eng_text_hi = 0x020358B0;
static u32 eng_bss_end_off = 0x382E8;  /* the engine's RAM ends here (SDRAM offset) */
static u32 sdt_lo, sdt_hi;             /* R1V: the engine's code in SDRAM (.sdtext, copied from the cart at start-up) */
#define STACK_TOP 0x0603F400u          /* the master stack (Kobo's crt0 / the boot loader) */
#define STACK_RES 0x0603D400u          /* R1W: its reserved 8 KB (engine_cart.ld: the engine's RAM ends below) */
static int sdt_ran;                    /* code in .sdtext has run: from then on nothing may write there */
static u32 blob_lo, blob_hi;           /* REV R4: the scene image's native scripts (part 18) in the cart, 0 if none */
static int in_eng_code(u32 a)          /* the engine's code: in the cart or in SDRAM (either address alias) */
{
    u32 p = a & 0x1FFFFFFF;
    return (p >= ENG_TEXT_LO && p < eng_text_hi) || (sdt_hi && p >= sdt_lo && p < sdt_hi) || (p >= blob_lo && p < blob_hi);
}
static const u32 LDR_LO = 0x06000000;
static u32 ldr_text_hi = 0x06000DFC;
static u32 ldr_bss_lo = 0x0DFC, ldr_bss_hi = 0x0E1C, ldr_sampling_off = 0x0E10;
static int engine_started;

static int ms_engine = -1, ms_setup = -1, ms_loaded = -1, ms_planes = -1;
static u16 ms_loaded_code;
static u8 sdram_byte(u32 a) { return sdram[(a - 0x06000000u) & (SDRAM_SIZE - 1)]; }
static u32 sdram_read32(u32 a)
{
    u32 o = (a - 0x06000000u) & (SDRAM_SIZE - 1);
    return (u32)sdram[o] << 24 | (u32)sdram[o + 1] << 16 | (u32)sdram[o + 2] << 8 | sdram[o + 3];
}
static void sdram_write32(u32 a, u32 v)
{
    u32 o = (a - 0x06000000u) & (SDRAM_SIZE - 4);
    sdram[o] = (u8)(v >> 24), sdram[o + 1] = (u8)(v >> 16), sdram[o + 2] = (u8)(v >> 8), sdram[o + 3] = (u8)v;
    for (int k = 0; k < 4; ++k) sdram_def[o + k] = 1;
}
static void note_state(u16 v)
{
    if (v == 0x5C16 && ms_setup < 0) {
        ms_setup = frame_no();
        for (int i = 0; i < opt_nsets; ++i) { /* -set */
            u32 a = sym_addr(opt_set_name[i]), o = (a - 0x06000000u) & (SDRAM_SIZE - 1);
            if (a < 0x06000000u || a >= 0x06040000u) {
                printf("[f%5d] -set %s: no such engine variable in SDRAM\n", frame_no(), opt_set_name[i]);
                continue;
            }
            sdram[o] = (u8)(opt_set_value[i] >> 24), sdram[o + 1] = (u8)(opt_set_value[i] >> 16);
            sdram[o + 2] = (u8)(opt_set_value[i] >> 8), sdram[o + 3] = (u8)opt_set_value[i];
            printf("[f%5d] -set %s (%08X) = %u\n", frame_no(), opt_set_name[i], a, opt_set_value[i]);
        }
    }
    if ((v & 0xF000) == 0x5000 && (v & 0x0F00) < 0x0C00 && ms_loaded < 0 && ms_setup >= 0) {
        ms_loaded      = frame_no();
        ms_loaded_code = v;
        if (presented_addr)
            presented_at_loaded = sdram_read32(presented_addr);
    }
}
static void check_data_access(u32 a, int size, int write, u32 v)
{
    u32 p = a & 0x1FFFFFFF, off;
    int rg   = region(p, &off);
    u32 area = a >> 29;
    if (rg == RG_NONE && area <= 1) {
        evlog(write ? "W-UNMAPPED" : "R-UNMAPPED", "addr=%08X size=%d val=%08X", a, size, v);
        return;
    }
    if (rg == RG_ROM && area <= 1) {
        evlog(write ? "W-ROM" : "R-ROM", "addr=%08X size=%d val=%08X", a, size, v);
        return;
    }
    if ((rg == RG_SYS || rg == RG_VDP || rg == RG_CRAM) && area == 0)
        evlog("REG-CACHED", "addr=%08X size=%d write=%d val=%08X (system register through the CACHED area)", a, size, write, v);
    if (rg == RG_SYS && write) {
        u32 o = off & 0xFF;
        if (o == 0x2C && size == 2) {
            if (engine_started || 1) {
                if ((u16)v != last_state) {
                    if (opt_verbose || ((u16)v != 0 && ((u16)v & 0xFF00) != 0x5D00))
                        printf("[f%5d] STATE %04X  CKPT %04X  (PC %08X)\n", frame_no(), (u16)v, COMM(8), cur_pc);
                    last_state = (u16)v;
                    note_state((u16)v);
                    if (opt_npeek && ((u16)v >> 12) == 5 && (((u16)v >> 8) & 0xF) < 4) /* 5Lnn: a stage loaded */
                        print_peeks("stage loaded");
                }
            }
        }
        else if (o >= 0x20 && o < 0x30) {
            if (o == 0x24 && sgb_on && (u16)v)
                rec_sent++;
            if (opt_verbose || (o != 0x28 && o != 0x2A && o != 0x20 && o != 0x22))
                evlog("W-COMM", "COMM%d (addr %08X) size=%d val=%08X", (int)(o - 0x20), a, size, v);
        }
        else if (o == 0x14 || o == 0x16 || o == 0x18 || o == 0x1A || o == 0x1C) {
            /* interrupt clears: expected from Kobo's handlers */
        }
        else
            evlog("W-SYSREG", "reg 4%03X size=%d val=%08X", (unsigned)o, size, v);
    }
    if (rg == RG_VDP && write)
        evlog("W-VDP", "reg 41%02X size=%d val=%08X", (unsigned)(off & 0xFF), size, v);
    if ((rg == RG_FB || rg == RG_FBOV) && !fm)
        evlog("FB-NOFM", "addr=%08X write=%d (frame buffer access while FM=0)", a, write);
    if (rg == RG_CART && rv_active)
        evlog("CART-RV", "addr=%08X write=%d (cart access while the 68000 holds RV)", a, write);
    if (write && engine_started) {
        if (rg == RG_CART) {
            if (off < eng_img_end_off)
                evlog("W-CARTCODE", "addr=%08X size=%d val=%08X (into the engine's own code / data image)", a, size, v);
            else
                evlog("W-CART", "addr=%08X size=%d val=%08X", a, size, v);
        }
        if (rg == RG_SDRAM && off < 0x2000) {
            /* the loader's area: vectors, loader code/bss, boot record */
            int ok = 0;
            if (off >= ldr_bss_lo && off < ldr_bss_hi && cur_pc >= LDR_LO && cur_pc < ldr_text_hi)
                ok = 1; /* loader bss written by loader code (VBI handler) */
            if (off >= 0x1F10 && off < 0x1F14 && cur_pc >= LDR_LO && cur_pc < ldr_text_hi)
                ok = 1; /* VBLANK_COUNT by the VBI handler */
            if (off >= ldr_sampling_off && off < ldr_sampling_off + 4)
                ok = 2; /* scdSampling (the engine switches it) */
            if (off < 0x400 && cur_pc >= ENG_TEXT_LO && cur_pc < ENG_TEXT_LO + 0x100)
                ok = 3; /* scd_engine_entry puts its exception handlers in the vector table */
            if (off >= 0x1F14 && off < 0x1F18 && size == 4)
                ok = 4; /* REV R3: boot record [5], the scene image now in the cart - the engine sets it after it loaded
                           another scene (cd32x/engine/plat_mars.cpp scene_load) */
            if (!ok || ok >= 2)
                evlog(ok == 2 ? "W-SAMPLING" : ok == 3 ? "W-VECTOR" : ok == 4 ? "W-BOOTREC" : "W-LOADER", "addr=%08X size=%d val=%08X", a,
                      size, v);
        }
        if (rg == RG_SDRAM && sdt_ran && off >= sdt_lo - 0x06000000u && off < sdt_hi - 0x06000000u)
            evlog("W-SDCODE", "addr=%08X size=%d val=%08X (into the engine's code in SDRAM)", a, size, v);
        if (rg == RG_SDRAM && off >= eng_bss_end_off && off < STACK_RES - 0x06000000u)
            evlog("W-GAP", "addr=%08X size=%d val=%08X (between the engine's RAM and its stack's 8 KB)", a, size, v);
    }
    if (!write && engine_started) {
        int undef = 0;
        for (int i = 0; i < size; ++i) {
            u32 o = off + i;
            if (rg == RG_SDRAM && !sdram_def[o & (SDRAM_SIZE - 1)])
                undef = 1;
            if (rg == RG_CART && !cart_def[o & (CART_SIZE - 1)])
                undef = 1;
            if ((rg == RG_FB || rg == RG_FBOV) && !fb_def[fs_bit][o & (FB_SIZE - 1)])
                undef = 1;
        }
        if (undef)
            evlog("R-UNDEF", "addr=%08X size=%d", a, size);
    }
}

static u32 cache_read(u32 a, int size, int ifetch)
{
    u32 p = a & 0x1FFFFFFF;
    u32 set = (p >> 4) & 63, tag = (p >> 10) & 0x7FFFF;
    int w, hit = -1;
    for (w = 0; w < 4; ++w)
        if (cl[set][w].valid && cl[set][w].tag == tag) {
            hit = w;
            break;
        }
    if (hit < 0) {
        int victim = 0;
        u64 oldest = ~0ull;
        for (w = 0; w < 4; ++w) {
            if (!cl[set][w].valid) {
                victim = w;
                oldest = 0;
                break;
            }
            if (cl[set][w].used < oldest) {
                oldest = cl[set][w].used;
                victim = w;
            }
        }
        u32 base = p & ~15u;
        int save = bus_penalty;
        pen_quiet++; /* the line fill is charged as a whole below */
        for (int i = 0; i < 16; i += 4) {
            u32 x                  = bus_read(base + i, 4, ifetch);
            cl[set][victim].d[i]     = (u8)(x >> 24);
            cl[set][victim].d[i + 1] = (u8)(x >> 16);
            cl[set][victim].d[i + 2] = (u8)(x >> 8);
            cl[set][victim].d[i + 3] = (u8)x;
        }
        pen_quiet--;
        u32 off;
        int rg      = region(p, &off);
        int fillc   = rg == RG_CART ? opt_cart_fill : (rg == RG_FB || rg == RG_FBOV) ? 8 * opt_fb_rd : opt_sdram_fill;
        bus_penalty = save + fillc;
        pen_note(rg, ifetch, fillc, p);
        if (!pen_quiet)
            fill_kind[rg == RG_CART ? (ifetch ? PK_IF_CART : PK_D_CART) : rg == RG_SDRAM ? (ifetch ? PK_IF_SD : PK_D_SD) : PK_OTHER]++;
        cl[set][victim].valid = 1;
        cl[set][victim].tag   = tag;
        hit                   = victim;
    }
    cl[set][hit].used = ++lru_clock;
    u32 v = 0;
    for (int i = 0; i < size; ++i) v = (v << 8) | cl[set][hit].d[(p & 15) + i];
    /* stale check: the cached copy against memory */
    {
        int save = bus_penalty;
        pen_quiet++;
        u32 m    = bus_read(p, size, ifetch);
        pen_quiet--;
        bus_penalty = save;
        if (m != v) {
            if (ifetch)
                evlog("STALE-CODE", "addr=%08X cache=%08X mem=%08X", a, v, m);
            else
                evlog("STALE-READ", "addr=%08X size=%d cache=%08X mem=%08X", a, size, v, m);
        }
    }
    return v;
}
static void cache_write_update(u32 a, u32 v, int size)
{
    u32 p = a & 0x1FFFFFFF;
    u32 set = (p >> 4) & 63, tag = (p >> 10) & 0x7FFFF;
    for (int w = 0; w < 4; ++w)
        if (cl[set][w].valid && cl[set][w].tag == tag) {
            for (int i = 0; i < size; ++i) cl[set][w].d[(p & 15) + i] = (u8)(v >> (8 * (size - 1 - i)));
            cl[set][w].used = ++lru_clock;
        }
}
static void cache_assoc_purge(u32 a)
{
    u32 p = a & 0x1FFFFFFF;
    u32 set = (p >> 4) & 63, tag = (p >> 10) & 0x7FFFF;
    for (int w = 0; w < 4; ++w)
        if (cl[set][w].valid && cl[set][w].tag == tag)
            cl[set][w].valid = 0;
}

static void address_error(u32 a, int write);
static u32 onchip_read(u32 off, int size);
static u32 mem_read(u32 a, int size, int ifetch)
{
    if ((size == 2 && (a & 1)) || (size == 4 && (a & 3))) {
        address_error(a, 0);
        return 0;
    }
    u32 area = a >> 29;
    if (!ifetch)
        check_data_access(a, size, 0, 0);
    if (a >= 0xFFFFFE00u)
        return onchip_read(a - 0xFFFFFE00u, size);
    if (area == 7) {
        evlog("R-IO", "addr=%08X size=%d", a, size);
        return 0;
    }
    if (area == 0) {
        if (opt_cache && (ccr & 1))
            return cache_read(a, size, ifetch);
        return bus_read(a, size, ifetch);
    }
    if (area == 1)
        return bus_read(a, size, ifetch);
    evlog("R-AREA", "addr=%08X size=%d (cache arrays / purge area)", a, size);
    return 0;
}
static void mem_write(u32 a, u32 v, int size)
{
    if ((size == 2 && (a & 1)) || (size == 4 && (a & 3))) {
        address_error(a, 1);
        return;
    }
    if (size == 1)
        v &= 0xFF;
    else if (size == 2)
        v &= 0xFFFF;
    u32 area = a >> 29;
    check_data_access(a, size, 1, v);
    if (a >= 0xFFFFFE00u) {
        onchip_write(a - 0xFFFFFE00u, v, size);
        return;
    }
    if (area == 7) {
        evlog("W-IO", "addr=%08X size=%d val=%08X (SDRAM mode / reserved I/O)", a, size, v);
        return;
    }
    if (area == 0) {
        if (opt_cache && (ccr & 1))
            cache_write_update(a, v, size);
        bus_write(a, v, size);
        return;
    }
    if (area == 1) {
        bus_write(a, v, size);
        return;
    }
    if (area == 2) { /* associative purge */
        cache_assoc_purge(a);
        return;
    }
    evlog("W-AREA", "addr=%08X size=%d val=%08X (cache arrays)", a, size, v);
}

/* ------------------------------------------------------------------ on-chip modules */
static u32 divu_dvsr, divu_dvdntl, divu_dvdnth;
static u32 onchip_read(u32 off, int size)
{
    u32 v = 0;
    if (off >= 0x100) { /* 0xFFFFFF00+ : DIVU, DMAC, BSC */
        u32 o = off - 0x100;
        if (o == 0x00)
            v = divu_dvsr;
        else if (o == 0x04 || o == 0x14)
            v = divu_dvdntl;
        else if (o == 0x10)
            v = divu_dvdnth;
        else {
            v = 0;
            evlog("R-ONCHIP", "addr=FFFFFE%03X size=%d", (unsigned)off, size);
        }
        return v;
    }
    for (int i = 0; i < size; ++i) v = (v << 8) | onchip[(off + i) & 0x1FF];
    if (off == 0x92)
        v = ccr;
    return v;
}
static void onchip_write(u32 off, u32 v, int size)
{
    if (off == 0x92 && size == 1) {
        ccr = (u8)v;
        if (v & 0x10) {
            cache_purge();
            ccr &= (u8)~0x10;
        }
        if (in_eng_code(cur_pc))
            evlog("CCR", "cache control = %02X (from engine code)", v);
        return;
    }
    if (off >= 0x100) {
        u32 o = off - 0x100;
        if (o == 0x00 && size == 4)
            divu_dvsr = v;
        else if (o == 0x04 && size == 4) { /* 32/32 signed */
            s32 d = (s32)divu_dvsr, n = (s32)v;
            if (d)
                divu_dvdntl = (u32)(n / d), divu_dvdnth = (u32)(n % d);
            evlog("DIVU", "32/32 %d / %d", n, d);
        }
        else if (o == 0x10 && size == 4)
            divu_dvdnth = v;
        else if (o == 0x14 && size == 4) {
            s64 n = (s64)(((u64)divu_dvdnth << 32) | v);
            s32 d = (s32)divu_dvsr;
            if (d)
                divu_dvdntl = (u32)(n / d), divu_dvdnth = (u32)(n % d);
            evlog("DIVU", "64/32");
        }
        else
            evlog("W-ONCHIP", "addr=FFFFFF%02X size=%d val=%08X (DMAC / BSC / other)", (unsigned)o, size, v);
        return;
    }
    /* FRT (0x10-0x19), WDT (0x80-0x83), interrupt controller (0x60-0x6F, 0xE0-0xE5), SBYCR 0x91 */
    for (int i = 0; i < size; ++i) onchip[(off + i) & 0x1FF] = (u8)(v >> (8 * (size - 1 - i)));
    if (!(off >= 0x10 && off <= 0x19)) /* FRT TOCR writes by Kobo's handlers are routine */
        evlog("W-ONCHIP", "addr=FFFFFE%02X size=%d val=%08X", (unsigned)off, size, v);
}

/* ------------------------------------------------------------------ exceptions / interrupts */
static void push32(u32 v)
{
    R[15] -= 4;
    mem_write(R[15], v, 4);
}
static void exception(int vec, u32 ret_pc, const char *why)
{
    char b[128];
    printf("[f%5d c%llu] EXCEPTION %s vector %d at %s (ret %08X)\n", frame_no(), (unsigned long long)cycles, why, vec,
           symname(cur_pc, b), ret_pc);
    dump_state();
    push32(SR);
    push32(ret_pc);
    PC        = mem_read(VBR + vec * 4, 4, 0);
    exc_taken = 1;
}
static int pending_addr_err;
static u32 pending_addr;
static void address_error(u32 a, int write)
{
    char b[128];
    printf("[f%5d] ADDRESS ERROR %s addr=%08X at %s\n", frame_no(), write ? "write" : "read", a, symname(cur_pc, b));
    pending_addr_err = 1;
    pending_addr     = a;
}
static void take_interrupt(int level, int vec)
{
    push32(SR);
    push32(PC);
    SR = (SR & ~0xF0u) | ((u32)level << 4);
    PC = mem_read(VBR + vec * 4, 4, 0);
}

/* ------------------------------------------------------------------ the Sub-CPU / 68000 side */
static struct {
    const char *name;
    u8 *data;
    u32 len;
} files[64];
static int nfiles;
static u8 *load_file(const char *path, u32 *len)
{
    FILE *f = fopen(path, "rb");
    if (!f)
        return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    u8 *d = malloc(n + 0x8000);
    memset(d, 0, n + 0x8000);
    if (fread(d, 1, n, f) != (size_t)n) {
        fclose(f);
        return NULL;
    }
    fclose(f);
    *len = (u32)n;
    return d;
}
static int find_file(const char *name)
{
    for (int i = 0; i < nfiles; ++i)
        if (strcmp(files[i].name, name) == 0)
            return i;
    char path[512];
    sprintf(path, "%s/disc/%s", opt_dir, name);
    u32 len;
    u8 *d = load_file(path, &len);
    if (!d)
        return -1;
    files[nfiles].name = strdup(name);
    files[nfiles].data = d;
    files[nfiles].len  = len;
    return nfiles++;
}
static int sub_busy;
static u64 sub_done_at;
static u16 sub_cmd;
static int cur_file = -1, cur_img = 0;
static int planes_on;
static int md_display = 1; /* REV R4B: the Genesis display (VDP register 1 bit 6): CMD 52 sets it, CMD 68 op 1 / op 2 turn it on */
static u64 splash_cmds;      /* REV R4B: command 51s (file chunk -> frame buffer: the loading picture) */
/* ---- the 68000's plane records and the VDP (cd32x/kobo/hw_md.s sgb_poll / sgb_vblank, md/sgb.h) ---- */
static u16 vram[0x8000];      /* 64 KB, words */
static u16 vsram[2], vcram[64];
static int sgb_on, sgb_ready, sgb_bad;
static u16 sgb_last;
static u16 sgb_rec[6002];
static u64 rec_sent, rec_taken, rec_applied, rec_junk;
static int opt_records = 3;   /* print the first N records */
#define MAX_SHOTS 64
static int shot_frame[MAX_SHOTS];
static const char *shot_file[MAX_SHOTS];
static int nshots;
static int shots_from = -1, shots_to, shots_step = 1; /* -shots A B STEP PREFIX: a picture every STEP frames */
static const char *shots_prefix;
static u32 rd_be32(const u8 *p) { return (u32)p[0] << 24 | (u32)p[1] << 16 | (u32)p[2] << 8 | p[3]; }
static void vdp_planes_on(u32 mdg_off)
{
    const u8 *m = &cart[mdg_off & (CART_SIZE - 1)];
    u32 cells = rd_be16(m + 6), oCell = rd_be32(m + 12), oCram = rd_be32(m + 32);
    memset(vram, 0, sizeof vram); /* the planes are cleared by command 68; the rest is the font - not shown */
    for (u32 i = 0; i < cells * 16 && i < 0x6000; ++i) vram[i] = rd_be16(m + oCell + 2 * i);
    for (int i = 0; i < 64; ++i) vcram[i] = rd_be16(m + oCram + 2 * i);
    vsram[0] = vsram[1] = 0;
    sgb_on = 1;
    sgb_ready = sgb_bad = 0;
    sgb_last = 0;
}
static void vram_w(u32 byteaddr, u16 v) { vram[(byteaddr >> 1) & 0x7FFF] = v; }
/* the 68000's time (hw_md.s): sgb_poll copies a record word by word out of the frame buffer before it answers
   COMM6, and it does not poll while its vertical-blank handler (sgb_vblank) writes the previous record to the VDP */
static u64 k68_free_at; /* the 68000 is in sgb_vblank until this cycle */
static u64 k68_done_at; /* the record being copied is answered at this cycle */
static u16 k68_copy_id; /* that record's id (0: none) */
static void sgb_poll_model(void) /* the 68000 command loop: a new id in COMM4 -> copy the record, answer COMM6 */
{
    if (!sgb_on || sgb_ready)
        return;
    if (k68_copy_id) { /* copying */
        if (cycles < k68_done_at)
            return;
        const u8 *f = fb[fs_bit];
        u16 len = rd_be16(f);
        if (len > 6000)
            sgb_bad = 1;
        else {
            for (u32 i = 0; i <= len; ++i) sgb_rec[i] = rd_be16(f + 2 * i);
            sgb_ready = 1;
        }
        sgb_last = k68_copy_id;
        SET_COMM(6, k68_copy_id);
        rec_taken++;
        k68_copy_id = 0;
        return;
    }
    u16 id = COMM(4);
    if (!id || id == sgb_last)
        return;
    if (!fm) { /* the SH2 gave the frame buffer: the record at its start (68000 0x840000), copied word by word */
        u16 len   = rd_be16(fb[fs_bit]);
        u64 start = cycles > k68_free_at ? cycles : k68_free_at;
        k68_done_at = start + (u64)opt_68k_poll + (u64)(len > 6000 ? 1 : len + 1) * (u64)opt_68k_word;
        k68_copy_id = id;
        return;
    }
    else { /* the SH2 still holds the frame buffer: what the 68000 copied on the console (R1H rows 12-13) */
        static const u16 junk[5] = { 4, 4, 6, 4, 4 };
        for (int i = 0; i < 5; ++i) sgb_rec[i] = junk[i];
        sgb_ready = 1;
        rec_junk++;
        count_kind("JUNK-RECORD"); /* a problem: on the console a dot in every blank cell, or worse */
        printf("[f%5d] 68000: took id %04X while the SH2 held the frame buffer - a junk record (console R1H: REC 0004 "
               "0004 0006 ...): a dot into VRAM cell 0\n", frame_no(), id);
    }
    sgb_last = id;
    SET_COMM(6, id);
    rec_taken++;
}
/* the records' contents in order (CRC-32), for the first opt_rec_crc_n records: a speed change must not change them */
static int opt_rec_crc_n = 600;
static u32 rec_crc = 0xFFFFFFFFu, rec_crc_count;
static void rec_crc_add(const u16 *w, u32 n)
{
    for (u32 i = 0; i < n; ++i)
        for (int b = 1; b >= 0; --b) {
            rec_crc ^= (u8)(w[i] >> (8 * b));
            for (int k = 0; k < 8; ++k) rec_crc = (rec_crc >> 1) ^ (0xEDB88320u & (0u - (rec_crc & 1)));
        }
}
static void sgb_apply_model(void) /* sgb_vblank: the waiting record into the VDP */
{
    if (rec_crc_count < (u32)opt_rec_crc_n && sgb_rec[0] <= 6000) {
        rec_crc_add(sgb_rec, (u32)sgb_rec[0] + 1);
        rec_crc_count++;
    }
    const u16 *p = sgb_rec + 1, *end = sgb_rec + 6002;
#define NEXTW (p < end ? *p++ : 0)
    int G = NEXTW, wordsA = 0, wordsB = 0, wordsX = 0;
    for (int g = 0; g < G; ++g) {
        u16 addr = NEXTW;
        int n    = NEXTW;
        for (int i = 0; i < n; ++i) {
            u16 a = (u16)(addr + 2 * i);
            vram_w(a, NEXTW);
            if (a >= 0xC000 && a < 0xE000)
                wordsA++;
            else if (a >= 0xE000)
                wordsB++;
            else
                wordsX++;
        }
    }
    vsram[0] = NEXTW;
    vsram[1] = NEXTW;
    for (int pl = 0; pl < 2; ++pl) { /* h-scroll runs: plane A at 0xFC00, plane B at 0xFC02, 4 bytes a line */
        int R = NEXTW, line = 0;
        for (int r = 0; r < R; ++r) {
            int lines = NEXTW;
            u16 v     = NEXTW;
            for (int l = 0; l < lines; ++l, ++line) vram_w((u32)(0xFC00 + pl * 2 + line * 4), v);
        }
    }
    int P = NEXTW;
    for (int i = 0; i < P; ++i) {
        int idx = NEXTW & 63;
        vcram[idx] = NEXTW;
    }
#undef NEXTW
    /* sgb_vblank's 68000 clocks: name words 22 each (move.w (a0)+,(a3) / dbra), a group's command ~60, the h-scroll
       table 18 a line for both planes, a colour ~40; 3 SH2 cycles a 68000 clock */
    k68_free_at = cycles + 3u * (u64)(400 + 22 * (wordsA + wordsB + wordsX) + 60 * G + 18 * 448 + 40 * P);
    rec_applied++;
    if (rec_applied <= (u64)opt_records)
        printf("[f%5d] 68000: record %llu into the VDP: %u words, name words plane A %d / B %d / other %d, vscroll %04X "
               "%04X, h-scroll line 0 %04X %04X, %d colours\n",
               frame_no(), (unsigned long long)rec_applied, sgb_rec[0], wordsA, wordsB, wordsX, vsram[0], vsram[1],
               vram[0xFC00 / 2], vram[0xFC02 / 2], P);
}
static void sgb_vblank_model(void)
{
    if (!sgb_on)
        return;
    if (sgb_bad)
        vcram[0] = 0x000E; /* R1G: the backdrop red */
    if (sgb_ready) {
        sgb_apply_model();
        sgb_ready = 0;
    }
}
static int plane_pixel(u32 base, int hs, int vs, int x, int y, int *pri)
{
    int px = (x - hs) & 511, py = (y + vs) & 255;
    u16 e = vram[((base + (((py >> 3) * 64 + (px >> 3)) << 1)) >> 1) & 0x7FFF];
    int cx = px & 7, cy = py & 7;
    if (e & 0x0800)
        cx = 7 - cx;
    if (e & 0x1000)
        cy = 7 - cy;
    u32 ba = (u32)(e & 0x7FF) * 32 + (u32)cy * 4 + (u32)(cx >> 1);
    u16 w  = vram[(ba >> 1) & 0x7FFF];
    int b  = (ba & 1) ? (w & 0xFF) : (w >> 8);
    int nib = (cx & 1) ? (b & 15) : (b >> 4);
    *pri = e >> 15;
    return nib ? (((e >> 13) & 3) * 16 + nib) : -1;
}
static int vram_byte(u32 a) { u16 w = vram[(a >> 1) & 0x7FFF]; return (a & 1) ? (w & 0xFF) : (w >> 8); }
/* REV R3: the sprites on line y (H40: the table at 0xF800 - SH2 command 68 sets register 5 to 0x7C -, the list from
   entry 0 through the links, 80 at most, 20 sprites and 320 dots a line). pix[x]: -1 none, else priority << 8 |
   palette line << 4 | colour; the first sprite in the list is in front. As host/md_check.cpp sgb_sprite_line. */
static void sprite_line(int y, int *pix)
{
    for (int x = 0; x < 320; ++x) pix[x] = -1;
    int i = 0, onLine = 0, dots = 0;
    for (int n = 0; n < 80; ++n) {
        u32 at   = 0xF800 + 8 * (u32)i;
        int sy   = (vram_byte(at) << 8 | vram_byte(at + 1)) & 0x1FF;
        int size = vram_byte(at + 2) & 0xF, link = vram_byte(at + 3) & 0x7F;
        int attr = vram_byte(at + 4) << 8 | vram_byte(at + 5);
        int sx   = (vram_byte(at + 6) << 8 | vram_byte(at + 7)) & 0x1FF;
        int w = (size >> 2 & 3) + 1, h = (size & 3) + 1, ly = y + 128 - sy;
        if (ly >= 0 && ly < h * 8) {
            if (++onLine > 20)
                break;
            for (int px = 0; px < w * 8 && dots < 320; ++px, ++dots) {
                int x = sx - 128 + px;
                if (x < 0 || x >= 320 || pix[x] >= 0)
                    continue;
                int cx = px >> 3, cy = ly >> 3, fx = px & 7, fy = ly & 7;
                if (attr & 0x800)
                    cx = w - 1 - cx, fx = 7 - fx;
                if (attr & 0x1000)
                    cy = h - 1 - cy, fy = 7 - fy;
                int tile = (attr & 0x7FF) + cx * h + cy;
                int b    = vram_byte(((u32)tile * 32 + (u32)fy * 4 + (u32)(fx >> 1)) & 0xFFFF);
                int c    = fx & 1 ? b & 15 : b >> 4;
                if (c)
                    pix[x] = (attr >> 15) << 8 | ((attr >> 13) & 3) << 4 | c;
            }
        }
        if (!link)
            break;
        i = link;
    }
}
static void write_shot(const char *path)
{
    FILE *f = fopen(path, "wb");
    if (!f) {
        printf("cannot write %s\n", path);
        return;
    }
    const int W = 320, H = 224, row = W * 3;
    u8 hdr[54] = { 'B', 'M' };
    u32 size = 54 + (u32)row * H;
    hdr[2] = (u8)size, hdr[3] = (u8)(size >> 8), hdr[4] = (u8)(size >> 16), hdr[5] = (u8)(size >> 24);
    hdr[10] = 54, hdr[14] = 40, hdr[18] = (u8)W, hdr[19] = (u8)(W >> 8), hdr[22] = (u8)H, hdr[23] = (u8)(H >> 8);
    hdr[26] = 1, hdr[28] = 24;
    fwrite(hdr, 1, 54, f);
    static u8 line[320 * 3];
    static int spr[320];
    for (int y = H - 1; y >= 0; --y) {
        int hA = (s16)vram[(0xFC00 + y * 4) >> 1], hB = (s16)vram[(0xFC02 + y * 4) >> 1];
        sprite_line(y, spr);
        for (int x = 0; x < W; ++x) {
            int pa, pb;
            int ca = plane_pixel(0xC000, hA, (s16)vsram[0], x, y, &pa);
            int cb = plane_pixel(0xE000, hB, (s16)vsram[1], x, y, &pb);
            int cs = spr[x] >= 0 ? (((spr[x] >> 4) & 3) * 16 + (spr[x] & 15)) : -1, ps = spr[x] >= 0 && (spr[x] >> 8);
            int c  = 0; /* backdrop: CRAM 0; then low before high, and sprite before plane A before plane B */
            if (cb >= 0 && !pb) c = cb;
            if (ca >= 0 && !pa) c = ca;
            if (cs >= 0 && !ps) c = cs;
            if (cb >= 0 && pb) c = cb;
            if (ca >= 0 && pa) c = ca;
            if (cs >= 0 && ps) c = cs;
            if (!md_display) /* REV R4B: the Genesis display off (CMD 52): black, see-through for the 32X */
                c = 0;
            u16 g = md_display ? vcram[c & 63] : 0;
            line[x * 3 + 0] = (u8)(((g >> 9) & 7) * 36);
            line[x * 3 + 1] = (u8)(((g >> 5) & 7) * 36);
            line[x * 3 + 2] = (u8)(((g >> 1) & 7) * 36);
            /* REV R4: the 32X layer (the shown frame buffer, the other one): 256-colour or direct-colour mode, its line
               table; a colour 0x0000 is see-through; in front of the Genesis picture when the priority bit (0x4101 bit 7)
               and the colour's bit 15 differ, else behind it - seen where the Genesis shows its backdrop */
            int mode = vdpreg[1] & 3, pri = (vdpreg[1] >> 7) & 1;
            if (mode == 1 || mode == 2) {
                const u8 *sf = fb[fs_bit ^ 1];
                u32 lt = rd_be16(sf + 2 * y);
                u16 col;
                if (mode == 1)
                    col = rd_be16(cram + 2 * sf[(lt * 2 + (u32)x) & (FB_SIZE - 1)]);
                else
                    col = rd_be16(sf + (((lt + (u32)x) * 2) & (FB_SIZE - 1)));
                int md_opaque = c != 0;
                if ((col & 0x7FFF) != 0 && ((pri ^ (col >> 15)) || !md_opaque)) {
                    line[x * 3 + 0] = (u8)(((col >> 10) & 31) * 255 / 31); /* BGR file order: blue */
                    line[x * 3 + 1] = (u8)(((col >> 5) & 31) * 255 / 31);
                    line[x * 3 + 2] = (u8)((col & 31) * 255 / 31);
                }
            }
        }
        fwrite(line, 1, row, f);
    }
    fclose(f);
    printf("[f%5d] the Genesis picture (planes and sprites model) -> %s\n", frame_no(), path);
}
static int sub_boot_stage;
static u64 pad_requests; /* command 48s answered */
static u64 sfx_plays, music_cmds; /* REV R4 */
static int cdd_status;            /* REV R4: the CD drive's status nibble (COMM14) */
/* REV R4: the controller (port 1) the 68000 reads in its vertical blank and the Sub-CPU hands the SH2 in command 48's
   answer (SEGA_CTRL_* bits: up 1, down 2, left 4, right 8, B 0x10, C 0x20, A 0x40, start 0x80, Z 0x100, Y 0x200,
   X 0x400). -pad "FRAME:BUTTONS,...": from FRAME on these buttons are held (letters U D L R A B C S X Y Z, '-' none);
   "FRAME:demo" = from FRAME the PC check's pad (spin dash, run right, one jump, every 4 s). No -pad: nothing pressed. */
#define MAX_PAD 1024
static int pad_frame[MAX_PAD], npad;
static u16 pad_bits[MAX_PAD]; /* 0xFFFF = the demo pattern */
static void pad_parse(const char *s)
{
    while (*s && npad < MAX_PAD) {
        char *e;
        int f = (int)strtol(s, &e, 10);
        if (*e != ':')
            break;
        s       = e + 1;
        u16 b   = 0;
        if (!strncmp(s, "demo", 4)) {
            b = 0xFFFF;
            s += 4;
        }
        else
            for (; *s && *s != ','; ++s) {
                const char *k = strchr("UDLRBCASZYX", *s);
                if (k)
                    b |= (u16)(1u << (k - "UDLRBCASZYX"));
            }
        pad_frame[npad] = f;
        pad_bits[npad]  = b;
        npad++;
        while (*s && *s != ',') ++s;
        if (*s == ',')
            ++s;
    }
}
static u16 pad_now(void)
{
    int f = frame_no(), i, k = -1;
    for (i = 0; i < npad && pad_frame[i] <= f; ++i) k = i;
    if (k < 0)
        return 0;
    if (pad_bits[k] != 0xFFFF)
        return pad_bits[k];
    unsigned ms = (unsigned)(f - pad_frame[k]) * 1000u / 60u, t;
    if (ms < 4000)
        return 0;
    t = (ms - 4000) % 4000;
    return t < 150 ? 2 : t < 300 ? 2 | 0x20 : t < 900 ? 2 : t < 2400 ? 8 : t < 2700 ? 8 | 0x20 : t < 3900 ? 8 : 0;
}
static u32 comm32(int n) { return ((u32)COMM(n) << 16) | COMM(n + 2); }
static void set_comm32(int n, u32 v)
{
    SET_COMM(n, v >> 16);
    SET_COMM(n + 2, v & 0xFFFF);
}
static void blob_update(void) /* REV R4: where the scene image (cart 0x080000) has its native scripts - code from now on */
{
    const u8 *h = cart + 0x080000;
    u32 lo = 0, hi = 0;
    if (h[0] == 'S' && h[1] == 'C' && h[2] == 'C' && h[3] == 'B' && rd_be32(h + 8) > 18) {
        u32 o = rd_be32(h + 12 + 20 * 18), l = rd_be32(h + 16 + 20 * 18);
        if (l && o + l <= 0x280000) {
            lo = 0x02080000u + o;
            hi = lo + l;
        }
    }
    if (lo != blob_lo || hi != blob_hi) {
        blob_lo = lo, blob_hi = hi;
        if (lo)
            printf("[f%5d] scene image: native scripts at %08X-%08X\n", frame_no(), lo, hi);
    }
}
static void sub_finish(void)
{
    u16 cmd = sub_cmd;
    char nm[32];
    if (cmd == 38) {
        int idx = COMM(2);
        cur_file = -1;
        if (idx == 21) {
            strcpy(nm, "SONICCD.PAK");
            cur_img = 0;
        }
        else if (idx == 22) {
            strcpy(nm, "SCDDATA.PAK");
            cur_img = 24;
        }
        else if (idx >= 0x100 && idx < 0x500) {
            sprintf(nm, "L%dS%03d.IMG", (idx >> 8) - 1, idx & 0xFF);
            cur_img = 4;
        }
        else if (idx >= 0x1100 && idx < 0x1500) {
            sprintf(nm, "G%dS%03d.MDG", ((idx >> 8) & 0xF) - 1, idx & 0xFF);
            cur_img = 28;
        }
        else if (idx <= 0)
            strcpy(nm, "IMAGE.RAW"); /* REV R4B: as the Sub-CPU (sub_main.c: file 0 and below) */
        else
            sprintf(nm, "IMAGE%d.RAW", idx);
        cur_file = find_file(nm);
        printf("[f%5d] SUB: CMD 38 open %s -> %s %u\n", frame_no(), nm, cur_file < 0 ? "NOT FOUND" : "ok",
               cur_file < 0 ? 0 : files[cur_file].len);
        set_comm32(8, cur_file < 0 ? 0 : files[cur_file].len);
        SET_COMM(0, 0);
    }
    else if (cmd == 46) {
        int chunk  = COMM(2);
        u32 length = comm32(8);
        u32 off    = (u32)chunk * 32768;
        int n      = 0;
        if (off < length)
            n = (int)((length - off + 2047) / 2048);
        if (n > 16)
            n = 16;
        u32 coff = (u32)cur_img * 0x20000 + off;
        for (int i = 0; i < n * 2048; ++i) {
            u32 src = off + i;
            u8 b    = (cur_file >= 0 && src < files[cur_file].len) ? files[cur_file].data[src] : 0;
            u32 o   = (coff + i) & (CART_SIZE - 1);
            cart[o]          = b;
            cart_def[o]      = 1;
            cart_wpc[o >> 1] = 0xC0DE0046;
        }
        rv_active = 0;
        SET_COMM(12, 0);
        SET_COMM(0, 0);
        if (cur_img == 4)
            blob_update();
        if (opt_verbose)
            printf("[f%5d] SUB: CMD 46 chunk %d -> cart %06X (%d sectors)\n", frame_no(), chunk, coff, n);
    }
    else if (cmd == 48 || (cmd & 0xFF) == 48) {
        static u16 last = 0xFFFF;
        u16 pad = pad_now();
        if (cmd >> 8) { /* REV R4: a sound effect in the request's upper byte (the Sub-CPU's pcm_play) */
            sfx_plays++;
            if (sfx_plays <= 40)
                printf("[f%5d] SUB: sound effect %d (PCM chip)\n", frame_no(), cmd >> 8);
        }
        if (pad != last && npad)
            printf("[f%5d] PAD %04X (command 48's answer)\n", frame_no(), pad);
        last = pad;
        pad_requests++;
        set_comm32(8, ((u32)pad << 16) | (u32)(frame_no() & 0xFFFF));
        SET_COMM(0, 0);
    }
    else if (cmd == 45 || cmd == 54) { /* REV R4: CD audio: play a track (repeated) / stop - the drive's status nibble */
        cdd_status = cmd == 45 ? 1 : 0;
        music_cmds++;
        printf("[f%5d] SUB: CMD %d %s%d\n", frame_no(), cmd, cmd == 45 ? "play track " : "stop", cmd == 45 ? COMM(2) : 0);
        SET_COMM(0, 0);
    }
    else if (cmd == 53) { /* REV R4: KOBOSFX.BIN -> the PCM chip; COMM12 = the number of effects */
        int k = find_file("KOBOSFX.BIN"), n = 0;
        if (k >= 0 && files[k].len >= 8 && !memcmp(files[k].data, "KSFX", 4))
            n = files[k].data[4] << 8 | files[k].data[5];
        printf("[f%5d] SUB: CMD 53 load KOBOSFX.BIN -> %d effects\n", frame_no(), n);
        SET_COMM(12, (u16)n);
        SET_COMM(0, 0);
    }
    else if (cmd == 51) {
        /* REV R4B: as the Sub-CPU's CMD 51 (sub_main.c K15): chunk c of the open file (32 KB, 16 sectors, whole sectors)
           -> the frame buffer the 68000 sees (the one not shown) at byte 0x200 + c * 32 KB, written by the 68000: the SH2
           must have handed the frame buffer over (FM = 0) */
        int chunk  = COMM(2);
        u32 length = cur_file >= 0 ? files[cur_file].len : 0;
        u32 off    = (u32)chunk * 32768;
        int n      = 0;
        if (off < length)
            n = (int)((length - off + 2047) / 2048);
        if (n > 16)
            n = 16;
        if (fm)
            evlog("FB-CMD51", "command 51 while the SH2 holds the frame buffer (FM 1): the 68000's writes would not land");
        for (int i = 0; i < n * 2048; ++i) {
            u32 src = off + i, o = 0x200 + off + i;
            if (o >= FB_SIZE)
                break;
            fb[fs_bit][o]     = (cur_file >= 0 && src < length) ? files[cur_file].data[src] : 0;
            fb_def[fs_bit][o] = 1;
        }
        splash_cmds++;
        SET_COMM(12, (u16)(((chunk & 0xFF) << 8) | (n & 0xFF))); /* (the Sub-CPU's K11 reply, in COMM12 and COMM14) */
        if (opt_verbose)
            printf("[f%5d] SUB: CMD 51 chunk %d -> frame buffer %X (%d sectors)\n", frame_no(), chunk, 0x200 + off, n);
        SET_COMM(0, 0);
    }
    else if (cmd == 52) { /* REV R4B: the Genesis display on (COMM2 = 1) / off (0) - the text and the planes */
        int on = COMM(2) & 1;
        if (on != md_display)
            printf("[f%5d] SUB: CMD 52 Genesis display %s\n", frame_no(), on ? "on" : "off");
        md_display = on;
        SET_COMM(0, 0);
    }
    else if (cmd == 44) {
        SET_COMM(0, 0);
    }
    else if (cmd == 68) {
        int op   = COMM(2) & 0xFF;
        u32 off  = comm32(8);
        int ok   = 0;
        if (op == 1) {
            /* REV R4: the Sub-CPU's own checks (cd32x/sub_main.c, SGB_CMD): an act file it would refuse is refused here */
            u32 o = off & (CART_SIZE - 1);
            const u8 *m = &cart[o];
            u32 magic = rd_be32(m), size = rd_be32(m + 8), oCell = rd_be32(m + 12), oZero = rd_be32(m + 28), oCram = rd_be32(m + 32);
            int ver = rd_be16(m + 4), cells = rd_be16(m + 6);
            if (!(off & 1) && magic == 0x53434D47u && ver == 1 && cells > 0 && cells <= 1536 && (off & 0xFFFFF) + size <= 0x100000 &&
                oCell < size && oZero < size && oCram < size) {
                ok        = 1;
                planes_on = 1;
                if (ms_planes < 0)
                    ms_planes = frame_no();
                vdp_planes_on(o);
                md_display = 1; /* REV R4B: the Sub-CPU's register list ends with 0x8174 */
                SET_COMM(6, 0);
            }
        }
        else if (op == 2) {
            planes_on  = 0;
            sgb_on     = 0;
            md_display = 1; /* REV R4B: md_init_hw leaves the display on (the text screen) */
            ok         = 1;
        }
        printf("[f%5d] SUB: CMD 68 op %d off %06X -> %d\n", frame_no(), op, off, ok);
        SET_COMM(12, ok);
        SET_COMM(0, 0);
    }
    else {
        printf("[f%5d] SUB: CMD %d (0x%04X) - no handler: COMM0 stays\n", frame_no(), cmd, cmd);
        return; /* the real Sub-CPU leaves an unknown command in COMM0 */
    }
    sub_busy = 0;
}
static void sub_poll(void)
{
    sgb_poll_model();
    /* boot-time handshake (Kobo): the 68000 clears M_OK, the Sub-CPU writes 0x1234 to COMM4, answers 0x0AAA */
    if (sub_boot_stage == 0 && cycles > 200000) {
        SET_COMM(0, 0x1000);
        SET_COMM(2, 0xF000);
        sub_boot_stage = 1;
    }
    if (sub_boot_stage == 1 && cycles > 400000) {
        SET_COMM(4, 0x1234);
        sub_boot_stage = 2;
    }
    if (sub_boot_stage == 2 && COMM(10) == 0x0AAA) {
        SET_COMM(10, 0x0BBB);
        sub_boot_stage = 3;
        printf("[f%5d] SUB: COMM10 0AAA -> 0BBB\n", frame_no());
    }
    if (sub_boot_stage < 3)
        return;
    if (sub_busy) {
        if (cycles >= sub_done_at)
            sub_finish();
        return;
    }
    u16 c = COMM(0);
    if (c == 0)
        return;
    if (c == 0xA55A || c == 0x1000)
        return;
    sub_cmd  = c;
    sub_busy = 1;
    /* latency: a few 68000 round trips; CMD 46 ~ 1 s per 32 KB chunk on hardware, shortened here */
    u64 lat = 30000;
    if (c == 46) {
        lat       = 200000;
        rv_active = 1;
    }
    if (c == 38)
        lat = 400000;
    sub_done_at = cycles + lat;
}
static void sdram_write32(u32 a, u32 v);
static void frame_tick(void)
{
    for (int i = 0; i < opt_npokes; ++i) /* REV R4B: -pokeg */
        if (opt_poke_frame[i] == frame_no()) {
            u32 a = sym_addr("globalVariables");
            if (a < 0x06000000u || a >= 0x06040000u)
                printf("[f%5d] -pokeg: no globalVariables in SDRAM\n", frame_no());
            else {
                sdram_write32(a + 4u * (u32)opt_poke_index[i], opt_poke_value[i]);
                cache_write_update(a + 4u * (u32)opt_poke_index[i], opt_poke_value[i], 4); /* as an SH2 write */
                printf("[f%5d] -pokeg globalVariables[%d] (%08X) = %d\n", frame_no(), opt_poke_index[i],
                       a + 4u * (u32)opt_poke_index[i], (int)opt_poke_value[i]);
            }
        }
    if (fs_req >= 0) { /* REV R4: the frame buffer swap, in vertical blank */
        fs_bit = fs_req;
        fs_req = -1;
    }
    /* the 68000's vertical-blank heartbeat in COMM6 (Kobo HW7), off while the planes run */
    if (!sgb_on)
        SET_COMM(6, (u16)(COMM(6) + 1));
    sgb_vblank_model();
    for (int i = 0; i < nshots; ++i)
        if (shot_frame[i] == frame_no())
            write_shot(shot_file[i]);
    if (shots_prefix && frame_no() >= shots_from && frame_no() <= shots_to && (frame_no() - shots_from) % shots_step == 0) {
        char path[600];
        snprintf(path, sizeof path, "%s_%05d.bmp", shots_prefix, frame_no());
        write_shot(path);
    }
    /* the Sub-CPU heartbeat in COMM14: bits 12-15 the CD drive's status (REV R4: 1 playing after command 45, 0 stopped) */
    if ((frame_no() & 7) == 0)
        SET_COMM(14, (u16)(cdd_status << 12 | ((frame_no() >> 3) & 0xFFF)));
}

/* ------------------------------------------------------------------ the interpreter */
static void exec(u16 op, u32 pc);
static u32 plat_log_addr; /* REV R4: the engine's plat_log (a no-op on the console): its notes printed */
static u8 sdram_byte(u32 a);
static void note_plat_log(void)
{
    static int shown;
    char t[96];
    int n = 0;
    for (u32 p = R[4]; n < 95; ++p, ++n) {
        u32 q = p & 0x1FFFFFFF;
        u8 c  = q >= 0x06000000u && q < 0x06000000u + SDRAM_SIZE ? sdram[(q - 0x06000000u) & (SDRAM_SIZE - 1)]
                : q < CART_SIZE + 0x02000000u && q >= 0x02000000u ? cart[(q - 0x02000000u) & (CART_SIZE - 1)] : 0;
        if (!c)
            break;
        t[n] = (char)c;
    }
    t[n] = 0;
    if (++shown <= 200)
        printf("[f%5d] ENGINE LOG: %s\n", frame_no(), t);
}
static u16 fetch(u32 a)
{
    if (plat_log_addr && (a & 0x1FFFFFFF) == plat_log_addr)
        note_plat_log();
    if (a & 1) {
        printf("odd fetch %08X\n", a);
        stop("odd fetch");
        return 0x0009;
    }
    int known = in_eng_code(a) || ((a & 0x1FFFFFFF) >= LDR_LO && (a & 0x1FFFFFFF) < ldr_text_hi);
    if (sdt_hi && !sdt_ran && (a & 0x1FFFFFFF) >= sdt_lo && (a & 0x1FFFFFFF) < sdt_hi)
        sdt_ran = 1;
    if (!known) {
        char b[128];
        printf("[f%5d c%llu] EXEC OUTSIDE CODE at %08X (from %s)\n", frame_no(), (unsigned long long)cycles, a,
               symname(cur_pc, b));
        dump_state();
        stop("exec outside code");
    }
    return (u16)mem_read(a, 2, 1);
}
static int is_branch(u16 op)
{
    if ((op & 0xF000) == 0xA000 || (op & 0xF000) == 0xB000)
        return 1;
    if ((op & 0xFF00) == 0x8900 || (op & 0xFF00) == 0x8B00 || (op & 0xFF00) == 0x8D00 || (op & 0xFF00) == 0x8F00)
        return 1;
    if ((op & 0xF0FF) == 0x400B || (op & 0xF0FF) == 0x402B || (op & 0xF0FF) == 0x0003 || (op & 0xF0FF) == 0x0023)
        return 1;
    if (op == 0x000B || op == 0x002B || (op & 0xFF00) == 0xC300)
        return 1;
    return 0;
}
static void delay_slot(u32 slot_pc, u32 target)
{
    u16 op = fetch(slot_pc);
    if (is_branch(op)) {
        exception(6, slot_pc, "slot illegal");
        return;
    }
    u32 save   = cur_pc;
    cur_pc     = slot_pc;
    in_slot    = 1;
    PC         = slot_pc + 2;
    exc_taken  = 0;
    exec(op, slot_pc);
    in_slot    = 0;
    cur_pc     = save;
    if (!pending_addr_err && !exc_taken)
        PC = target;
    exc_taken = 0;
    cycles += 1;
}
static void illegal(u16 op, u32 pc)
{
    char b[128];
    printf("[f%5d] ILLEGAL op %04X at %s\n", frame_no(), op, symname(pc, b));
    exception(in_slot ? 6 : 4, pc, "illegal instruction");
}
#define N_ ((op >> 8) & 15)
#define M_ ((op >> 4) & 15)
#define RN R[N_]
#define RM R[M_]
#define SETT(x) (SR = (SR & ~F_T) | ((x) ? F_T : 0))
#define TBIT (SR & F_T)
static u32 rd8(u32 a) { return mem_read(a, 1, 0); }
static u32 rd16(u32 a) { return mem_read(a, 2, 0); }
static u32 rd32(u32 a) { return mem_read(a, 4, 0); }
static void wr8(u32 a, u32 v) { mem_write(a, v, 1); }
static void wr16(u32 a, u32 v) { mem_write(a, v, 2); }
static void wr32(u32 a, u32 v) { mem_write(a, v, 4); }
static void pcrel_check(void)
{
    if (in_slot)
        evlog("PCREL-SLOT", "PC-relative load in a delay slot");
}
static void exec(u16 op, u32 pc)
{
    u32 t, tmp0, tmp1;
    switch (op >> 12) {
        case 0x0:
            switch (op & 0xF) {
                case 0x2:
                    switch (M_) {
                        case 0: RN = SR; break;
                        case 1: RN = GBR; break;
                        case 2: RN = VBR; break;
                        default: illegal(op, pc);
                    }
                    no_int_next = 1;
                    break;
                case 0x3:
                    if (M_ == 0) { /* BSRF */
                        t  = pc + 4 + RN;
                        PR = pc + 4;
                        call_push(t, PR);
                        delay_slot(pc + 2, t);
                    }
                    else if (M_ == 2) { /* BRAF */
                        t = pc + 4 + RN;
                        delay_slot(pc + 2, t);
                    }
                    else
                        illegal(op, pc);
                    break;
                case 0x4: wr8(RN + R[0], RM); break;
                case 0x5: wr16(RN + R[0], RM); break;
                case 0x6: wr32(RN + R[0], RM); break;
                case 0x7:
                    MACL = RN * RM;
                    cycles += 1;
                    break;
                case 0x8:
                    if (op == 0x0008)
                        SR &= ~F_T;
                    else if (op == 0x0018)
                        SR |= F_T;
                    else if (op == 0x0028)
                        MACH = MACL = 0;
                    else
                        illegal(op, pc);
                    break;
                case 0x9:
                    if (op == 0x0009) {
                    }
                    else if (op == 0x0019)
                        SR &= ~(F_M | F_Q | F_T);
                    else if ((op & 0xF0FF) == 0x0029)
                        RN = TBIT;
                    else
                        illegal(op, pc);
                    break;
                case 0xA:
                    switch (M_) {
                        case 0: RN = MACH; break;
                        case 1: RN = MACL; break;
                        case 2: RN = PR; break;
                        default: illegal(op, pc);
                    }
                    no_int_next = 1;
                    break;
                case 0xB:
                    if (op == 0x000B) { /* RTS */
                        t = PR;
                        call_ret(t);
                        delay_slot(pc + 2, t);
                    }
                    else if (op == 0x001B) { /* SLEEP */
                        evlog("SLEEP", "sleep instruction");
                        cycles += 64;
                    }
                    else if (op == 0x002B) { /* RTE */
                        t = rd32(R[15]);
                        R[15] += 4;
                        SR = rd32(R[15]) & 0x3F3;
                        R[15] += 4;
                        call_ret(t);
                        delay_slot(pc + 2, t);
                    }
                    else
                        illegal(op, pc);
                    break;
                case 0xC: RN = (u32)(s32)(s8)rd8(R[0] + RM); break;
                case 0xD: RN = (u32)(s32)(s16)rd16(R[0] + RM); break;
                case 0xE: RN = rd32(R[0] + RM); break;
                case 0xF: { /* MAC.L @Rm+,@Rn+ */
                    s32 tn = (s32)rd32(RN);
                    RN += 4;
                    s32 tm = (s32)rd32(RM);
                    RM += 4;
                    s64 mac = (s64)(((u64)MACH << 32) | MACL);
                    mac += (s64)tn * (s64)tm;
                    if (SR & F_S) {
                        if (mac > 0x00007FFFFFFFFFFFll)
                            mac = 0x00007FFFFFFFFFFFll;
                        if (mac < (s64)0xFFFF800000000000ll)
                            mac = (s64)0xFFFF800000000000ll;
                    }
                    MACH = (u32)((u64)mac >> 32);
                    MACL = (u32)mac;
                    cycles += 2;
                    break;
                }
                default: illegal(op, pc);
            }
            break;
        case 0x1: wr32(RN + (op & 0xF) * 4, RM); break;
        case 0x2:
            switch (op & 0xF) {
                case 0x0: wr8(RN, RM); break;
                case 0x1: wr16(RN, RM); break;
                case 0x2: wr32(RN, RM); break;
                case 0x4:
                    t = RM;
                    wr8(RN - 1, t);
                    RN -= 1;
                    break;
                case 0x5:
                    t = RM;
                    wr16(RN - 2, t);
                    RN -= 2;
                    break;
                case 0x6:
                    t = RM;
                    wr32(RN - 4, t);
                    RN -= 4;
                    break;
                case 0x7: /* DIV0S */
                    SR &= ~(F_Q | F_M | F_T);
                    if (RN & 0x80000000u)
                        SR |= F_Q;
                    if (RM & 0x80000000u)
                        SR |= F_M;
                    if (((SR & F_Q) != 0) != ((SR & F_M) != 0))
                        SR |= F_T;
                    break;
                case 0x8: SETT((RN & RM) == 0); break;
                case 0x9: RN &= RM; break;
                case 0xA: RN ^= RM; break;
                case 0xB: RN |= RM; break;
                case 0xC:
                    t = RN ^ RM;
                    SETT(!(t & 0xFF000000u) || !(t & 0x00FF0000u) || !(t & 0x0000FF00u) || !(t & 0x000000FFu));
                    break;
                case 0xD: RN = (RM << 16) | (RN >> 16); break;
                case 0xE: MACL = (u32)(u16)RN * (u32)(u16)RM; break;
                case 0xF: MACL = (u32)((s32)(s16)RN * (s32)(s16)RM); break;
                default: illegal(op, pc);
            }
            break;
        case 0x3:
            switch (op & 0xF) {
                case 0x0: SETT(RN == RM); break;
                case 0x2: SETT(RN >= RM); break;
                case 0x3: SETT((s32)RN >= (s32)RM); break;
                case 0x4: { /* DIV1 (MAME) */
                    u32 n = N_, m = M_;
                    u32 old_q = SR & F_Q;
                    if (0x80000000u & R[n])
                        SR |= F_Q;
                    else
                        SR &= ~F_Q;
                    R[n] = (R[n] << 1) | (SR & F_T);
                    if (!old_q) {
                        if (!(SR & F_M)) {
                            tmp0 = R[n];
                            R[n] -= R[m];
                            if (!(SR & F_Q)) {
                                if (R[n] > tmp0) SR |= F_Q; else SR &= ~F_Q;
                            }
                            else {
                                if (R[n] > tmp0) SR &= ~F_Q; else SR |= F_Q;
                            }
                        }
                        else {
                            tmp0 = R[n];
                            R[n] += R[m];
                            if (!(SR & F_Q)) {
                                if (R[n] < tmp0) SR &= ~F_Q; else SR |= F_Q;
                            }
                            else {
                                if (R[n] < tmp0) SR |= F_Q; else SR &= ~F_Q;
                            }
                        }
                    }
                    else {
                        if (!(SR & F_M)) {
                            tmp0 = R[n];
                            R[n] += R[m];
                            if (!(SR & F_Q)) {
                                if (R[n] < tmp0) SR |= F_Q; else SR &= ~F_Q;
                            }
                            else {
                                if (R[n] < tmp0) SR &= ~F_Q; else SR |= F_Q;
                            }
                        }
                        else {
                            tmp0 = R[n];
                            R[n] -= R[m];
                            if (!(SR & F_Q)) {
                                if (R[n] > tmp0) SR &= ~F_Q; else SR |= F_Q;
                            }
                            else {
                                if (R[n] > tmp0) SR |= F_Q; else SR &= ~F_Q;
                            }
                        }
                    }
                    tmp0 = SR & (F_Q | F_M);
                    if (!tmp0 || tmp0 == (F_Q | F_M))
                        SR |= F_T;
                    else
                        SR &= ~F_T;
                    break;
                }
                case 0x5: {
                    u64 r = (u64)RN * (u64)RM;
                    MACH  = (u32)(r >> 32);
                    MACL  = (u32)r;
                    cycles += 2;
                    break;
                }
                case 0x6: SETT(RN > RM); break;
                case 0x7: SETT((s32)RN > (s32)RM); break;
                case 0x8: RN -= RM; break;
                case 0xA: /* SUBC */
                    tmp1 = RN - RM;
                    tmp0 = RN;
                    RN   = tmp1 - TBIT;
                    SETT(tmp0 < tmp1 || tmp1 < RN);
                    break;
                case 0xB: { /* SUBV */
                    s32 dest = (s32)RN >= 0 ? 0 : 1, src = (s32)RM >= 0 ? 0 : 1, ans;
                    src += dest;
                    RN -= RM;
                    ans = (s32)RN >= 0 ? 0 : 1;
                    ans += dest;
                    SETT(src == 1 && ans == 1);
                    break;
                }
                case 0xC: RN += RM; break;
                case 0xD: {
                    s64 r = (s64)(s32)RN * (s64)(s32)RM;
                    MACH  = (u32)((u64)r >> 32);
                    MACL  = (u32)r;
                    cycles += 2;
                    break;
                }
                case 0xE: /* ADDC */
                    tmp1 = RN + RM;
                    tmp0 = RN;
                    RN   = tmp1 + TBIT;
                    SETT(tmp0 > tmp1 || tmp1 > RN);
                    break;
                case 0xF: { /* ADDV */
                    s32 dest = (s32)RN >= 0 ? 0 : 1, src = (s32)RM >= 0 ? 0 : 1, ans;
                    src += dest;
                    RN += RM;
                    ans = (s32)RN >= 0 ? 0 : 1;
                    ans += dest;
                    SETT((src == 0 || src == 2) && ans == 1);
                    break;
                }
                default: illegal(op, pc);
            }
            break;
        case 0x4:
            switch (op & 0xFF) {
                case 0x00:
                case 0x20:
                    SETT(RN >> 31);
                    RN <<= 1;
                    break;
                case 0x10:
                    RN--;
                    SETT(RN == 0);
                    break;
                case 0x01:
                    SETT(RN & 1);
                    RN >>= 1;
                    break;
                case 0x11: SETT((s32)RN >= 0); break;
                case 0x21:
                    SETT(RN & 1);
                    RN = (u32)((s32)RN >> 1);
                    break;
                case 0x02: RN -= 4; wr32(RN, MACH); no_int_next = 1; break;
                case 0x12: RN -= 4; wr32(RN, MACL); no_int_next = 1; break;
                case 0x22: RN -= 4; wr32(RN, PR); no_int_next = 1; break;
                case 0x03: RN -= 4; wr32(RN, SR); no_int_next = 1; break;
                case 0x13: RN -= 4; wr32(RN, GBR); no_int_next = 1; break;
                case 0x23: RN -= 4; wr32(RN, VBR); no_int_next = 1; break;
                case 0x04:
                    SETT(RN >> 31);
                    RN = (RN << 1) | TBIT;
                    break;
                case 0x24:
                    t  = RN >> 31;
                    RN = (RN << 1) | TBIT;
                    SETT(t);
                    break;
                case 0x05:
                    SETT(RN & 1);
                    RN = (RN >> 1) | (TBIT << 31);
                    break;
                case 0x15: SETT((s32)RN > 0); break;
                case 0x25:
                    t  = RN & 1;
                    RN = (RN >> 1) | (TBIT << 31);
                    SETT(t);
                    break;
                case 0x06: MACH = rd32(RN); RN += 4; no_int_next = 1; break;
                case 0x16: MACL = rd32(RN); RN += 4; no_int_next = 1; break;
                case 0x26: PR = rd32(RN); RN += 4; no_int_next = 1; break;
                case 0x07:
                    t = rd32(RN);
                    RN += 4;
                    SR = t & 0x3F3;
                    no_int_next = 1;
                    if (((SR >> 4) & 15) >= 12 && engine_started && in_eng_code(pc))
                        evlog("SR-HIGH", "LDC.L sets SR=%03X (interrupt mask %d)", SR, (SR >> 4) & 15);
                    break;
                case 0x17: GBR = rd32(RN); RN += 4; no_int_next = 1; break;
                case 0x27:
                    VBR = rd32(RN);
                    RN += 4;
                    no_int_next = 1;
                    evlog("VBR", "LDC.L VBR=%08X", VBR);
                    break;
                case 0x08: RN <<= 2; break;
                case 0x18: RN <<= 8; break;
                case 0x28: RN <<= 16; break;
                case 0x09: RN >>= 2; break;
                case 0x19: RN >>= 8; break;
                case 0x29: RN >>= 16; break;
                case 0x0A: MACH = RN; no_int_next = 1; break;
                case 0x1A: MACL = RN; no_int_next = 1; break;
                case 0x2A: PR = RN; no_int_next = 1; break;
                case 0x0B: /* JSR */
                    t  = RN;
                    PR = pc + 4;
                    call_push(t, PR);
                    delay_slot(pc + 2, t);
                    break;
                case 0x1B: { /* TAS.B */
                    u32 a = RN;
                    u32 v = rd8(a);
                    SETT(v == 0);
                    wr8(a, v | 0x80);
                    cycles += 3;
                    break;
                }
                case 0x2B: /* JMP */
                    t = RN;
                    delay_slot(pc + 2, t);
                    break;
                case 0x0E:
                    SR = RN & 0x3F3;
                    no_int_next = 1;
                    if (((SR >> 4) & 15) >= 12 && engine_started && in_eng_code(pc))
                        evlog("SR-HIGH", "LDC sets SR=%03X (interrupt mask %d)", SR, (SR >> 4) & 15);
                    break;
                case 0x1E: GBR = RN; no_int_next = 1; break;
                case 0x2E:
                    VBR = RN;
                    no_int_next = 1;
                    evlog("VBR", "LDC VBR=%08X", VBR);
                    break;
                default:
                    if ((op & 0xF) == 0xF) { /* MAC.W @Rm+,@Rn+ */
                        s32 tn = (s16)rd16(RN);
                        RN += 2;
                        s32 tm = (s16)rd16(RM);
                        RM += 2;
                        if (SR & F_S) {
                            s64 r = (s64)(s32)MACL + (s64)tn * tm;
                            if (r > 0x7FFFFFFFll) {
                                r = 0x7FFFFFFFll;
                                MACH |= 1;
                            }
                            else if (r < -0x80000000ll) {
                                r = -0x80000000ll;
                                MACH |= 1;
                            }
                            MACL = (u32)r;
                        }
                        else {
                            s64 mac = (s64)(((u64)MACH << 32) | MACL);
                            mac += (s64)tn * tm;
                            MACH = (u32)((u64)mac >> 32);
                            MACL = (u32)mac;
                        }
                        cycles += 2;
                    }
                    else
                        illegal(op, pc);
            }
            break;
        case 0x5: RN = rd32(RM + (op & 0xF) * 4); break;
        case 0x6:
            switch (op & 0xF) {
                case 0x0: RN = (u32)(s32)(s8)rd8(RM); break;
                case 0x1: RN = (u32)(s32)(s16)rd16(RM); break;
                case 0x2: RN = rd32(RM); break;
                case 0x3: RN = RM; break;
                case 0x4: {
                    u32 a = RM;
                    t     = (u32)(s32)(s8)rd8(a);
                    if (N_ != M_)
                        RM += 1;
                    RN = t;
                    break;
                }
                case 0x5: {
                    u32 a = RM;
                    t     = (u32)(s32)(s16)rd16(a);
                    if (N_ != M_)
                        RM += 2;
                    RN = t;
                    break;
                }
                case 0x6: {
                    u32 a = RM;
                    t     = rd32(a);
                    if (N_ != M_)
                        RM += 4;
                    RN = t;
                    break;
                }
                case 0x7: RN = ~RM; break;
                case 0x8: RN = (RM & 0xFFFF0000u) | ((RM & 0xFF) << 8) | ((RM >> 8) & 0xFF); break;
                case 0x9: RN = (RM << 16) | (RM >> 16); break;
                case 0xA:
                    tmp0 = 0 - RM;
                    t    = tmp0 - TBIT;
                    RN   = t;
                    SETT(0 < tmp0 || tmp0 < t);
                    break;
                case 0xB: RN = 0 - RM; break;
                case 0xC: RN = RM & 0xFF; break;
                case 0xD: RN = RM & 0xFFFF; break;
                case 0xE: RN = (u32)(s32)(s8)RM; break;
                case 0xF: RN = (u32)(s32)(s16)RM; break;
            }
            break;
        case 0x7: RN += (u32)(s32)(s8)(op & 0xFF); break;
        case 0x8:
            switch ((op >> 8) & 0xF) {
                case 0x0: wr8(R[M_] + (op & 0xF), R[0]); break;
                case 0x1: wr16(R[M_] + (op & 0xF) * 2, R[0]); break;
                case 0x4: R[0] = (u32)(s32)(s8)rd8(R[M_] + (op & 0xF)); break;
                case 0x5: R[0] = (u32)(s32)(s16)rd16(R[M_] + (op & 0xF) * 2); break;
                case 0x8: SETT(R[0] == (u32)(s32)(s8)(op & 0xFF)); break;
                case 0x9:
                    if (TBIT) {
                        PC = pc + 4 + (u32)((s32)(s8)(op & 0xFF) * 2);
                        cycles += 2;
                    }
                    break;
                case 0xB:
                    if (!TBIT) {
                        PC = pc + 4 + (u32)((s32)(s8)(op & 0xFF) * 2);
                        cycles += 2;
                    }
                    break;
                case 0xD:
                    if (TBIT) {
                        t = pc + 4 + (u32)((s32)(s8)(op & 0xFF) * 2);
                        delay_slot(pc + 2, t);
                    }
                    break;
                case 0xF:
                    if (!TBIT) {
                        t = pc + 4 + (u32)((s32)(s8)(op & 0xFF) * 2);
                        delay_slot(pc + 2, t);
                    }
                    break;
                default: illegal(op, pc);
            }
            break;
        case 0x9:
            pcrel_check();
            RN = (u32)(s32)(s16)rd16(pc + 4 + (op & 0xFF) * 2);
            break;
        case 0xA: {
            s32 d = (s32)((op & 0xFFF) ^ 0x800) - 0x800;
            t     = pc + 4 + (u32)(d * 2);
            delay_slot(pc + 2, t);
            cycles += 1;
            break;
        }
        case 0xB: {
            s32 d = (s32)((op & 0xFFF) ^ 0x800) - 0x800;
            t     = pc + 4 + (u32)(d * 2);
            PR    = pc + 4;
            call_push(t, PR);
            delay_slot(pc + 2, t);
            cycles += 1;
            break;
        }
        case 0xC: {
            u32 d = op & 0xFF;
            switch ((op >> 8) & 0xF) {
                case 0x0: wr8(GBR + d, R[0]); break;
                case 0x1: wr16(GBR + d * 2, R[0]); break;
                case 0x2: wr32(GBR + d * 4, R[0]); break;
                case 0x3: /* TRAPA */
                    evlog("TRAPA", "trapa #%u", d);
                    push32(SR);
                    push32(pc + 2);
                    PC = rd32(VBR + d * 4);
                    break;
                case 0x4: R[0] = (u32)(s32)(s8)rd8(GBR + d); break;
                case 0x5: R[0] = (u32)(s32)(s16)rd16(GBR + d * 2); break;
                case 0x6: R[0] = rd32(GBR + d * 4); break;
                case 0x7:
                    pcrel_check();
                    R[0] = (pc & ~3u) + 4 + d * 4;
                    break;
                case 0x8: SETT((R[0] & d) == 0); break;
                case 0x9: R[0] &= d; break;
                case 0xA: R[0] ^= d; break;
                case 0xB: R[0] |= d; break;
                case 0xC: SETT((rd8(GBR + R[0]) & d) == 0); break;
                case 0xD: t = rd8(GBR + R[0]) & d; wr8(GBR + R[0], t); break;
                case 0xE: t = rd8(GBR + R[0]) ^ d; wr8(GBR + R[0], t); break;
                case 0xF: t = rd8(GBR + R[0]) | d; wr8(GBR + R[0], t); break;
            }
            break;
        }
        case 0xD:
            pcrel_check();
            RN = rd32((pc & ~3u) + 4 + (op & 0xFF) * 4);
            break;
        case 0xE: RN = (u32)(s32)(s8)(op & 0xFF); break;
        case 0xF: illegal(op, pc); break;
    }
}

/* ------------------------------------------------------------------ main loop */
static void parse_layout(void)
{
    char path[512], line[1024];
    sprintf(path, "%s/engine.map", opt_dir);
    FILE *f = fopen(path, "r");
    if (f) {
        while (fgets(line, sizeof line, f)) {
            unsigned long long a, n, la;
            if (sscanf(line, ".text %llx %llx", &a, &n) == 2 && a == 0x02000000ull)
                eng_text_hi = (u32)(a + n);
            else if (sscanf(line, ".data %llx %llx load address %llx", &a, &n, &la) == 3 && la >= 0x02000000ull &&
                     la < 0x02400000ull) {
                ENG_IMG_HI      = (u32)(la + n);
                eng_img_end_off = ENG_IMG_HI - 0x02000000u;
            }
            else if (sscanf(line, ".sdtext %llx %llx load address %llx", &a, &n, &la) == 3 && a >= 0x06000000ull &&
                     a < 0x06040000ull && la >= 0x02000000ull && la < 0x02400000ull) {
                sdt_lo = (u32)a;
                sdt_hi = (u32)(a + n);
                if ((u32)(la + n) > ENG_IMG_HI) {
                    ENG_IMG_HI      = (u32)(la + n);
                    eng_img_end_off = ENG_IMG_HI - 0x02000000u;
                }
            }
            else if (sscanf(line, ".bss %llx %llx", &a, &n) == 2 && a >= 0x06000000ull && a < 0x06040000ull)
                eng_bss_end_off = (u32)(a + n - 0x06000000ull);
        }
        fclose(f);
    }
    else
        printf("note: no %s - default engine layout limits\n", path);
    sprintf(path, "%s/sh2.map", opt_dir);
    f = fopen(path, "r");
    if (f) {
        while (fgets(line, sizeof line, f)) {
            unsigned long long a;
            char nm[256];
            if (sscanf(line, " 0x%llx %255s", &a, nm) == 2 && a >= 0x06000000ull && a < 0x06002000ull) {
                if (!strcmp(nm, "__bss_start"))
                    ldr_bss_lo = (u32)(a - 0x06000000ull);
                else if (!strcmp(nm, "__bss_end"))
                    ldr_bss_hi = (u32)(a - 0x06000000ull);
                else if (!strcmp(nm, "_scdSampling"))
                    ldr_sampling_off = (u32)(a - 0x06000000ull);
            }
        }
        fclose(f);
    }
    printf("layout: engine code 02000000-%08X, image to %08X, RAM to %08X; loader bss %04X-%04X, sampler flag %04X\n",
           eng_text_hi, ENG_IMG_HI, 0x06000000 + eng_bss_end_off, ldr_bss_lo, ldr_bss_hi, ldr_sampling_off);
    if (sdt_hi)
        printf("layout: engine code in SDRAM (.sdtext) %08X-%08X (%u bytes)\n", sdt_lo, sdt_hi, sdt_hi - sdt_lo);
}
static void summary(void)
{
    printf("\n==================== SUMMARY ====================\n");
    printf("build folder      %s\n", opt_dir);
    printf("run               %d frames (%.1f s of console time)%s%s\n", frame_no(), frame_no() / 60.0,
           stopped ? ", stopped: " : "", stopped ? stop_reason : "");
    printf("engine started    %s", ms_engine >= 0 ? "yes" : "NO\n");
    if (ms_engine >= 0)
        printf(" (frame %d)\n", ms_engine);
    printf("stage set-up 5C16 %s", ms_setup >= 0 ? "yes" : "no\n");
    if (ms_setup >= 0)
        printf(" (frame %d)\n", ms_setup);
    printf("stage loaded      %s", ms_loaded >= 0 ? "yes" : "no\n");
    if (ms_loaded >= 0)
        printf(" (STATE %04X, frame %d)\n", ms_loaded_code, ms_loaded);
    printf("planes on (CMD 68) %s", ms_planes >= 0 ? "yes" : "no\n");
    if (ms_planes >= 0)
        printf(" (frame %d)\n", ms_planes);
    printf("line 10 now       STATE:%X CKPT:%04X COMM4:%04X\n", COMM(12), COMM(8), COMM(4));
    printf("Genesis display   %s at the end%s\n", md_display ? "on" : "off (CMD 52)",
           splash_cmds ? "; the loading picture: command 51 used" : ""); /* REV R4B */
    if (ms_loaded >= 0 && presented_addr && frame_no() > ms_loaded + 60) {
        u32 n    = sdram_read32(presented_addr) - presented_at_loaded;
        double s = (frame_no() - ms_loaded) / 60.0;
        printf("game speed        %u engine frames in %.1f s of play = %.1f frames per second (the game is made for 60)\n", n, s,
               n / s);
    }
    if (fifo_lost_events)
        printf("frame-buffer FIFO %llu word(s) dropped in %llu hand-over(s) to the 68000\n", (unsigned long long)fifo_lost_words,
               (unsigned long long)fifo_lost_events);
    if (ms_planes >= 0)
        printf("plane records     %llu sent by the SH2, %llu taken by the 68000, %llu into the VDP%s\n",
               (unsigned long long)rec_sent, (unsigned long long)rec_taken, (unsigned long long)rec_applied,
               rec_junk ? " - INCLUDING A JUNK RECORD (COMM4 held a value when the planes came on: dots in every "
                          "blank cell)" : "");
    if (rec_crc_count)
        printf("record contents   records 1-%u: CRC %08X (the same for the same picture sequence, at any speed)\n",
               rec_crc_count, ~rec_crc);
    if (pad_requests)
        printf("controller        %llu pad requests (command 48) answered\n", (unsigned long long)pad_requests);
    if (sfx_plays || music_cmds)
        printf("audio             %llu sound effects played, %llu CD audio commands\n", (unsigned long long)sfx_plays,
               (unsigned long long)music_cmds);
    int problems = 0;
    for (int sev = 2; sev >= 0; --sev) {
        int any = 0;
        for (int i = 0; i < nkinds; ++i) {
            if (kind_severity(kinds[i].kind) != sev)
                continue;
            if (!any)
                printf("%s\n", sev == 2 ? "PROBLEMS (can hang or corrupt the console):" : sev == 1 ? "warnings:" : "information:");
            any = 1;
            char b[128];
            printf("  %-11s x%-8llu first at frame %d, %s\n", kinds[i].kind, (unsigned long long)kinds[i].count,
                   kinds[i].first_frame, symname(kinds[i].first_pc, b));
            if (sev == 2)
                problems++;
            /* REV R4: where most of the warnings and problems happen (the places with the highest counts, up to 4) */
            int shown = 0;
            for (int pass = 0; sev > 0 && pass < 4 && shown < 4; ++pass) {
                int best = -1;
                for (int k = 0; k < nlogkeys; ++k)
                    if (!strcmp(logkeys[k].kind, kinds[i].kind) && logkeys[k].count > 0 &&
                        (best < 0 || logkeys[k].count > logkeys[best].count))
                        best = k;
                if (best < 0 || (u64)logkeys[best].count == kinds[i].count)
                    break; /* one place only: the line above says it */
                printf("              x%-8d at %s\n", logkeys[best].count, symname(logkeys[best].pc, b));
                logkeys[best].count = -logkeys[best].count; /* (shown) */
                shown++;
            }
            for (int k = 0; k < nlogkeys; ++k)
                if (logkeys[k].count < 0)
                    logkeys[k].count = -logkeys[k].count;
        }
    }
    if (stopped && strcmp(stop_reason, "frame limit"))
        problems++;
    print_peeks("end");
    printf("VERDICT           %s\n", problems ? "PROBLEMS FOUND - do not burn yet" : "no problems found");
}
static u64 last_vbi_cycle;
static void fill_garbage(u8 *p, u32 n, u32 *seed)
{
    for (u32 i = 0; i < n; ++i) {
        if (opt_fill) {
            *seed = *seed * 1103515245u + 12345u;
            p[i]  = (u8)(*seed >> 16);
        }
        else
            p[i] = 0;
    }
}
static void print_peeks(const char *when) /* REV R4: -peek - read straight from SDRAM (no bus time, no cache) */
{
    for (int i = 0; i < opt_npeek; ++i) {
        u32 a = 0, v = 0;
        { /* REV R4B: NAME+OFFSET too (e.g. globalVariables+104: the game's global 26) */
            char nm[128];
            const char *plus = strchr(opt_peek[i], '+');
            size_t L         = plus ? (size_t)(plus - opt_peek[i]) : strlen(opt_peek[i]);
            if (L >= sizeof nm)
                L = sizeof nm - 1;
            memcpy(nm, opt_peek[i], L);
            nm[L] = 0;
            a     = sym_addr(nm);
            if (a && plus)
                a += (u32)strtoul(plus + 1, 0, 0);
        }
        int n = opt_peek_size[i] == 1 || opt_peek_size[i] == 2 ? opt_peek_size[i] : 4;
        if (!a || (a & 0x1FFFFFFF) < 0x06000000u || (a & 0x1FFFFFFF) >= 0x06000000u + SDRAM_SIZE) {
            printf("[f%5d] peek %s: no SDRAM variable of that name\n", frame_no(), opt_peek[i]);
            continue;
        }
        u32 o = (a & 0x1FFFFFFF) - 0x06000000u;
        for (int k = 0; k < n; ++k) v = v << 8 | sdram[(o + (u32)k) & (SDRAM_SIZE - 1)];
        printf("[f%5d] peek (%s) %s = %08X\n", frame_no(), when, opt_peek[i], v);
    }
}
int main(int argc, char **argv)
{
    for (int i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help")) {
            printf("usage: sh2emu [build folder] [options]\n"
                   "  build folder   default cd32x/build_engine (needs SCDSH2.BIN, sh2.map, engine.map,\n"
                   "                 engine_symbols.txt and disc/ with SONICCD.PAK, SCDDATA.PAK, L*.IMG, G*.MDG)\n"
                   "  -f N           run N frames (default 1500 = 25 s)\n"
                   "  -v             every STATE change, every Sub-CPU chunk\n"
                   "  -maxlog N      lines per (event, PC) (default 4)\n"
                   "  -zero          power-on RAM = zeros (default: pseudo-random), -seed N\n"
                   "  -nocache       no SH2 cache model\n"
                   "  -hook ADDR     print registers whenever the SH2 reaches ADDR (up to -hookmax, default 50)\n"
                   "  -trace N COUNT print COUNT instructions from instruction N\n"
                   "  -dumpsdram FRAME FILE   write the 256 KB SDRAM at FRAME\n"
                   "  -shot FRAME FILE.bmp   the Genesis picture at FRAME (planes and sprites: records into a VDP model), up to 64\n"
                   "  -shots A B STEP PREFIX a picture every STEP frames from A to B (PREFIX_<frame>.bmp)\n"
                   "  -records N     print the first N plane records (default 3)\n"
                   "  -pad SCRIPT    the controller: \"FRAME:BUTTONS,...\" (U D L R A B C S X Y Z, '-' none; FRAME:demo = the\n"
                   "                 scripted spin dash / run right from FRAME on)\n"
                   "  -set NAME=V    the engine variable NAME (32 bits) = V at the first stage set-up (e.g. scdDemoPad=1)\n"
                   "  -peek NAME[:N] print the engine variable NAME (N = 1, 2 or 4 bytes) at each stage load and at the end\n"
                   "  -peekevery N   ... and every N frames\n"
                   "  -pokeg F:I=V   the game's global variable I (GameConfig.bin's order, e.g. 25 Warp.Destination,\n"
                   "                 26 Warp.Timer) = V at frame F (REV R4B; up to 256)\n"
                   "  -syms FILE     more function names (nm -n -C), e.g. a scene's native scripts: game_data/native/\n"
                   "                 scene_<list>_<pos>.syms (tools/build_game_data.py --native); up to 8\n"
                   "  -reccrc N      CRC of the first N records' contents (default 600): the same pictures at any speed\n"
                   "  -nofifo        no frame-buffer write FIFO model; -fbword N cycles per FIFO word (default 5)\n"
                   "  -profile A B   where the SH2's time goes between frames A and B (per engine frame, by function,\n"
                   "                 by script object type, cart stalls)\n"
                   "  -callers NAME  with -profile: who calls NAME\n  -pcprof NAME   with -profile: NAME's every instruction (count, cycles, stalls per address; up to 8)\n  -frames A B    with -profile: every engine frame that ends in console frames A..B, with its top functions\n"
                   "  -cartwait N    cart waits per single access (default 15); -cartrd N, -cartfill N (default 87) set\n"
                   "                 an uncached long read / a cache line directly; -fbread N (default 8); -k68word N (75)\n"
                   "  -novbistop     keep running when line 10 freezes\n");
            return 0;
        }
        if (argv[i][0] != '-') {
            opt_dir = argv[i];
            continue;
        }
        if (!strcmp(argv[i], "-f") && i + 1 < argc)
            opt_frames = atoi(argv[++i]);
        else if (!strcmp(argv[i], "-zero"))
            opt_fill = 0;
        else if (!strcmp(argv[i], "-seed") && i + 1 < argc)
            opt_seed = (u32)strtoul(argv[++i], 0, 0);
        else if (!strcmp(argv[i], "-nocache"))
            opt_cache = 0;
        else if (!strcmp(argv[i], "-v"))
            opt_verbose = 1;
        else if (!strcmp(argv[i], "-maxlog") && i + 1 < argc)
            opt_maxlog = atoi(argv[++i]);
        else if (!strcmp(argv[i], "-trace") && i + 2 < argc) {
            opt_trace_from  = strtoull(argv[++i], 0, 0);
            opt_trace_count = strtoull(argv[++i], 0, 0);
        }
        else if (!strcmp(argv[i], "-cpf") && i + 1 < argc)
            opt_cpf = atoi(argv[++i]);
        else if (!strcmp(argv[i], "-cartwait") && i + 1 < argc)
            opt_cart_wait = atoi(argv[++i]);
        else if (!strcmp(argv[i], "-fbread") && i + 1 < argc)
            opt_fb_rd = atoi(argv[++i]);
        else if (!strcmp(argv[i], "-k68word") && i + 1 < argc)
            opt_68k_word = atoi(argv[++i]);
        else if (!strcmp(argv[i], "-cartrd") && i + 1 < argc)
            opt_cart_rd = atoi(argv[++i]);
        else if (!strcmp(argv[i], "-cartfill") && i + 1 < argc)
            opt_cart_fill = atoi(argv[++i]);
        else if (!strcmp(argv[i], "-dir") && i + 1 < argc)
            opt_dir = argv[++i];
        else if (!strcmp(argv[i], "-hook") && i + 1 < argc && opt_nhooks < 16)
            opt_hooks[opt_nhooks++] = (u32)strtoul(argv[++i], 0, 0);
        else if (!strcmp(argv[i], "-hookmax") && i + 1 < argc)
            opt_hook_max = atoi(argv[++i]);
        else if (!strcmp(argv[i], "-dumpsdram") && i + 2 < argc) {
            opt_dump_frame = atoi(argv[++i]);
            opt_dump_file  = argv[++i];
        }
        else if (!strcmp(argv[i], "-shot") && i + 2 < argc && nshots < MAX_SHOTS) {
            shot_frame[nshots] = atoi(argv[++i]);
            shot_file[nshots]  = argv[++i];
            nshots++;
        }
        else if (!strcmp(argv[i], "-reccrc") && i + 1 < argc)
            opt_rec_crc_n = atoi(argv[++i]);
        else if (!strcmp(argv[i], "-pad") && i + 1 < argc)
            pad_parse(argv[++i]);
        else if (!strcmp(argv[i], "-peekevery") && i + 1 < argc)
            opt_peek_every = atoi(argv[++i]);
        else if (!strcmp(argv[i], "-peek") && i + 1 < argc && opt_npeek < 8) {
            char *c = strchr(argv[++i], ':');
            opt_peek_size[opt_npeek] = c ? atoi(c + 1) : 4;
            if (c)
                *c = 0;
            opt_peek[opt_npeek++] = argv[i];
        }
        else if (!strcmp(argv[i], "-syms") && i + 1 < argc && opt_nxsyms < 8) /* REV R4: more names (nm), e.g. a scene's */
            opt_xsyms[opt_nxsyms++] = argv[++i];                              /* native scripts (game_data/native/) */
        else if (!strcmp(argv[i], "-set") && i + 1 < argc && strchr(argv[i + 1], '=') && opt_nsets < MAX_SETS) {
            char *e = strchr(argv[++i], '=');
            *e                         = 0;
            opt_set_name[opt_nsets]    = argv[i];
            opt_set_value[opt_nsets++] = (u32)strtoul(e + 1, 0, 0);
        }
        else if (!strcmp(argv[i], "-pokeg") && i + 1 < argc && opt_npokes < MAX_POKES) { /* REV R4B */
            int fr = 0, ix = 0;
            long val = 0;
            if (sscanf(argv[++i], "%d:%d=%ld", &fr, &ix, &val) == 3 && ix >= 0 && ix < 256) {
                opt_poke_frame[opt_npokes] = fr;
                opt_poke_index[opt_npokes] = ix;
                opt_poke_value[opt_npokes++] = (u32)val;
            }
        }
        else if (!strcmp(argv[i], "-callers") && i + 1 < argc)
            opt_callers = argv[++i];
        else if (!strcmp(argv[i], "-frames") && i + 2 < argc) {
            opt_fl_from = atoi(argv[++i]);
            opt_fl_to   = atoi(argv[++i]);
        }
        else if (!strcmp(argv[i], "-pcprof") && i + 1 < argc) {
            if (pcp_n < PCP_MAX)
                pcp_name[pcp_n++] = argv[++i];
            else
                ++i;
        }
        else if (!strcmp(argv[i], "-shots") && i + 4 < argc) {
            shots_from   = atoi(argv[++i]);
            shots_to     = atoi(argv[++i]);
            shots_step   = atoi(argv[++i]);
            shots_prefix = argv[++i];
            if (shots_step < 1)
                shots_step = 1;
        }
        else if (!strcmp(argv[i], "-profile") && i + 2 < argc) {
            prof_from   = atoi(argv[++i]);
            prof_to     = atoi(argv[++i]);
            dpage_stall = calloc(1024, sizeof(u64));
        }
        else if (!strcmp(argv[i], "-nofifo"))
            opt_fifo = 0;
        else if (!strcmp(argv[i], "-fbword") && i + 1 < argc)
            opt_fb_word = atoi(argv[++i]);
        else if (!strcmp(argv[i], "-records") && i + 1 < argc)
            opt_records = atoi(argv[++i]);
        else if (!strcmp(argv[i], "-novbistop"))
            opt_stop_on_vbi_loss = 0;
    }
    if (opt_cart_rd < 0) /* a bus cycle is 2 clocks plus the waits; the instruction's own cycle overlaps the first */
        opt_cart_rd = 2 * (2 + opt_cart_wait) - 1;
    if (opt_cart_fill < 0) /* -cartfill -1: 8 full accesses */
        opt_cart_fill = 8 * (2 + opt_cart_wait) - 1;
    char path[512];
    u32 seed = opt_seed;
    fill_garbage(sdram, SDRAM_SIZE, &seed);
    fill_garbage(fb[0], FB_SIZE, &seed);
    fill_garbage(fb[1], FB_SIZE, &seed);
    fill_garbage(cart, CART_SIZE, &seed);
    sprintf(path, "%s/engine_symbols.txt", opt_dir);
    load_engine_syms(path);
    sprintf(path, "%s/sh2.map", opt_dir);
    load_map_syms(path);
    for (int i = 0; i < opt_nxsyms; ++i)
        load_engine_syms(opt_xsyms[i]);
    qsort(syms, nsyms, sizeof(Sym), sym_cmp);
    parse_layout();
    presented_addr = sym_addr("presented");
    plat_log_addr  = sym_addr("plat_log(char const*)") & 0x1FFFFFFF;
    for (int i = 0; i < nsyms && !scr_fn; ++i) /* the function (code), not its static variables ("...::told") */
        if (!strncmp(syms[i].name, "ProcessScript(", 14) && sym_code(i))
            scr_fn = syms[i].a;
    scr_loop_addr = sym_addr("objectLoop");
    scr_ents_addr = sym_addr("objectEntityList");
    if (!scr_loop_addr || !scr_ents_addr)
        scr_fn = 0;
    if (opt_callers) {
        for (int i = 0; i < nsyms; ++i)
            if (!strcmp(syms[i].name, opt_callers) ||
                (!callers_addr && !strncmp(syms[i].name, opt_callers, strlen(opt_callers)) &&
                 syms[i].name[strlen(opt_callers)] == '('))
                callers_addr = syms[i].a;
        if (callers_addr)
            callers_count = calloc((size_t)nsyms + 1, sizeof(u64));
        else
            printf("-callers: no function %s\n", opt_callers);
    }
    for (int p = 0; p < pcp_n; ++p) { /* -pcprof: the function's address range (to the next code symbol) */
        int k = -1;
        size_t L = strlen(pcp_name[p]);
        for (int i = 0; i < nsyms; ++i)
            if (sym_code(i) && (!strcmp(syms[i].name, pcp_name[p]) || (!strncmp(syms[i].name, pcp_name[p], L) &&
                                                                        syms[i].name[L] == '(')))
                k = i;
        if (k < 0) {
            printf("-pcprof: no function %s\n", pcp_name[p]);
            continue;
        }
        pcp[p].lo = syms[k].a;
        pcp[p].hi = k + 1 < nsyms ? syms[k + 1].a : syms[k].a + 0x1000;
        if (pcp[p].hi - pcp[p].lo > 0x10000)
            pcp[p].hi = pcp[p].lo + 0x10000;
        size_t n = (pcp[p].hi - pcp[p].lo) / 2 + 1;
        pcp[p].cnt  = calloc(n, sizeof(u64));
        pcp[p].cyc  = calloc(n, sizeof(u64));
        pcp[p].kind = calloc(n * PK_N, sizeof(u64));
    }

    /* the CD boot table + the loader program (Kobo crt0 + sh2_engboot) */
    u32 blen;
    sprintf(path, "%s/SCDSH2.BIN", opt_dir);
    u8 *boot = load_file(path, &blen);
    if (!boot) {
        fprintf(stderr, "no SCDSH2.BIN\n");
        return 1;
    }
    u32 dest = (u32)boot[0x18] << 24 | boot[0x19] << 16 | boot[0x1A] << 8 | boot[0x1B];
    u32 len  = (u32)boot[0x1C] << 24 | boot[0x1D] << 16 | boot[0x1E] << 8 | boot[0x1F];
    u32 entry = (u32)boot[0x20] << 24 | boot[0x21] << 16 | boot[0x22] << 8 | boot[0x23];
    u32 vbr   = (u32)boot[0x28] << 24 | boot[0x29] << 16 | boot[0x2A] << 8 | boot[0x2B];
    printf("boot table: dest %08X len %X entry %08X vbr %08X\n", dest, len, entry, vbr);
    for (u32 i = 0; i < len; ++i) {
        sdram[(dest - 0x06000000 + i) & (SDRAM_SIZE - 1)]     = boot[0x38 + i];
        sdram_def[(dest - 0x06000000 + i) & (SDRAM_SIZE - 1)] = 1;
    }
    for (u32 i = 0; i < blen && i < FB_SIZE; ++i) {
        fb[0][i]     = boot[i];
        fb_def[0][i] = 1;
    }
    ldr_text_hi = 0x06000000 + len;
    /* the boot ROM's hand-over: M_OK / S_OK in COMM0 / COMM4, VBR, entry */
    SET_COMM(0, 0x4D5F);
    SET_COMM(2, 0x4F4B);
    SET_COMM(4, 0x535F);
    SET_COMM(6, 0x4F4B);
    VBR   = vbr;
    PC    = entry;
    SR    = 0xF0;
    R[15] = 0x06040000;
    ccr   = 0;

    u64 next_vbi    = (u64)opt_cpf * 224 / 262;
    int sampler_frozen_reported = 0;
    u64 last_sample_frame = 0;
    u16 last_comm12 = 0;
    u64 stack_low = 0xFFFFFFFF;
    while (!stopped) {
        if (frame_no() >= opt_frames)
            break;
        /* this instruction's time starts here: the interrupt entry and the instruction fetch count too */
        bus_penalty = 0;
        for (int k = 0; k < PK_N; ++k) pen_kind[k] = fill_kind[k] = 0;
        prof_active   = prof_from >= 0 && frame_no() >= prof_from && frame_no() < prof_to;
        prof_active_c = prof_active;
        u64 c_start   = cycles;
        if (opt_dump_file && frame_no() == opt_dump_frame) {
            FILE *df = fopen(opt_dump_file, "wb");
            if (df) {
                fwrite(sdram, 1, SDRAM_SIZE, df);
                fclose(df);
                printf("[f%5d] SDRAM dumped to %s\n", frame_no(), opt_dump_file);
            }
            opt_dump_file = 0;
        }
        /* interrupts */
        if (cycles >= next_vbi) {
            next_vbi += (u64)opt_cpf;
            frame_tick();
            if (intmsk_lo & 0x08)
                vint_pending = 1;
            vbi_count++;
            if (opt_peek_every > 0 && opt_npeek && frame_no() % opt_peek_every == 0)
                print_peeks("every");
        }
        if (!no_int_next && vint_pending && ((SR >> 4) & 15) < 12) {
            take_interrupt(12, 70);
            last_vbi_cycle = cycles;
        }
        no_int_next = 0;
        /* the Sub-CPU / 68000 */
        sub_poll();
        /* one instruction */
        u32 pc = PC;
        cur_pc = pc;
        last_pcs[last_pos++ & (LOGH - 1)] = pc;
        if (fbq_n)
            fbq_drain(cycles);
        if (pc == 0x02000000 && !engine_started) {
            engine_started = 1;
            ms_engine      = frame_no();
            printf("[f%5d] ENGINE ENTRY (SR %03X, R15 %08X, VBR %08X, CCR %02X, INTMSK %02X)\n", frame_no(), SR, R[15], VBR,
                   ccr, intmsk_lo);
        }
        for (int h = 0; h < opt_nhooks; ++h)
            if (pc == opt_hooks[h] && hook_count[h]++ < opt_hook_max) {
                char b[128], b2[128];
                printf("[f%5d] HOOK %s PR=%s r4=%08X r5=%08X r6=%08X r7=%08X r0=%08X r1=%08X r2=%08X r3=%08X r8=%08X r9=%08X r10=%08X r11=%08X r12=%08X r13=%08X r14=%08X\n",
                       frame_no(), symname(pc, b), symname(PR, b2), R[4], R[5], R[6], R[7], R[0], R[1], R[2], R[3], R[8], R[9], R[10], R[11], R[12], R[13], R[14]);
            }
        u16 op = fetch(pc);
        if (stopped)
            break;
        if (insns >= opt_trace_from && insns < opt_trace_from + opt_trace_count) {
            char b[128];
            printf("T %llu %s %04X r0=%08X r1=%08X r2=%08X r3=%08X r4=%08X r5=%08X r14=%08X r15=%08X sr=%03X\n",
                   (unsigned long long)insns, symname(pc, b), op, R[0], R[1], R[2], R[3], R[4], R[5], R[14], R[15], SR);
        }
        PC = pc + 2;
        exec(op, pc);
        cycles += 1 + (u64)bus_penalty;
        if (prof_active)
            prof_account(pc, cycles - c_start);
        if (prof_from >= 0 && frame_no() >= prof_from && frame_no() < prof_to) {
            if (!prof_next)
                prof_next = cycles;
            while (prof_next <= cycles) {
                prof_sample(cur_pc);
                prof_next += PROF_STEP;
            }
        }
        insns++;
        if (pending_addr_err) {
            pending_addr_err = 0;
            exception(9, PC, "address error");
        }
        if (engine_started) {
            if (R[15] < stack_low) {
                stack_low = R[15];
            }
            if (R[15] < STACK_RES || R[15] > STACK_TOP) {
                if (!(cur_pc >= LDR_LO && cur_pc < ldr_text_hi))
                    evlog("STACK", "R15=%08X outside the master stack's 8 KB %08X-%08X", R[15], STACK_RES, STACK_TOP);
            }
            /* the sampler: COMM12 changes every 30 vblanks once on */
            u16 c12 = COMM(12);
            if (c12 != last_comm12) {
                last_comm12       = c12;
                last_sample_frame = (u64)frame_no();
            }
            u32 so   = ldr_sampling_off;
            u32 samp = (u32)sdram[so] << 24 | sdram[so + 1] << 16 | sdram[so + 2] << 8 | sdram[so + 3];
            if (samp && (u64)frame_no() > last_sample_frame + 45 && !sampler_frozen_reported) {
                sampler_frozen_reported = 1;
                printf("[f%5d] *** LINE 10 FROZEN: sampling on, COMM12 unchanged since frame %llu (last VBI taken at "
                       "frame %llu)\n",
                       frame_no(), (unsigned long long)last_sample_frame, (unsigned long long)(last_vbi_cycle / opt_cpf));
                dump_state();
                if (opt_stop_on_vbi_loss)
                    stop("line 10 frozen");
            }
        }
    }
    char b[128];
    printf("\nEND after %llu instructions, %llu cycles, frame %d: %s\n", (unsigned long long)insns,
           (unsigned long long)cycles, frame_no(), stopped ? stop_reason : "frame limit");
    printf("PC %s  STATE %04X  stack low %08X  VBIs %llu\n", symname(PC, b), COMM(12), (u32)stack_low,
           (unsigned long long)vbi_count);
    if (opt_verbose || stopped)
        dump_state();
    summary();
    prof_report();
    if (prof_from >= 0) {
        int last  = frame_no() < prof_to ? frame_no() : prof_to;
        cost_frames = last - prof_from;
        cost_report();
        pcp_report();
    }
    return 0;
}
