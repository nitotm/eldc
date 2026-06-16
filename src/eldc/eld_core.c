/*
 * eld_core.c — ELD engine core
 *
 * Shared by every ELD front-end via unity-build (#include "eld_core.c").
 * The include guard prevents double-inclusion when front-ends chain includes.
 *
 * Front-ends:
 *   eld.c           — single-threaded CLI
 *   eld_mt.c        — multi-threaded CLI
 *   _eldc_module.c  — Python C extension
 *   eldc_lib.c       — C shared library (.so / .dll)
 *
 * Compile examples (each front-end compiles alone):
 *   gcc -O3 -march=native -o eldc        eld.c       -lm
 *   gcc -O3 -march=native -o eldc_mt     eld_mt.c    -lm -lpthread
 *   gcc -O3 -march=native -shared -fPIC -DELD_BUILD_DLL -o libeldc.so eldc_lib.c -lm
 *
 * Thread safety:
 *   init_detector() must be called exactly once from a single thread.
 *   After that, detect_ex() / detect() and eld_process_line() are fully
 *   thread-safe: all per-call mutable state uses _Thread_local storage.
 *   Config setters (g_scheme, g_lang_mask, g_subset) are NOT thread-safe;
 *   set them before starting parallel detection.
 */

#ifndef ELD_CORE_C_INCLUDED
#define ELD_CORE_C_INCLUDED

/* ── Platform / compiler compatibility ───────────────────────────────────── */
#ifdef _WIN32
   /* MSVC has strnlen and strtok_s (same signature as POSIX strtok_r). */
#  define strtok_r strtok_s
   /* GCC/Clang built-ins unavailable in MSVC — replace with neutral equivalents. */
#  include <intrin.h>
#  define __builtin_expect(expr, val)    (expr)
#  define __builtin_prefetch(addr, ...)  _mm_prefetch((const char *)(addr), _MM_HINT_T0)
#else
   /* POSIX 2008: strnlen, strtok_r */
#  ifndef _POSIX_C_SOURCE
#    define _POSIX_C_SOURCE 200809L
#  endif
#endif

/* Thread-local storage: C11 keyword, or compiler-specific fallback. */
#if defined(_MSC_VER)
#  define ELD_THREAD_LOCAL __declspec(thread)
#elif defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L
#  define ELD_THREAD_LOCAL _Thread_local
#elif defined(__GNUC__) || defined(__clang__)
#  define ELD_THREAD_LOCAL __thread
#else
#  define ELD_THREAD_LOCAL  /* single-threaded fallback */
#  pragma message("ELD: no thread-local storage; detect_ex() is not thread-safe")
#endif

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <ctype.h>
#include <time.h>

#ifdef _WIN32
#  include <windows.h>
#  include <io.h>
#  define isatty _isatty
#  define fileno _fileno
#else
#  include <unistd.h>
#  include <sys/stat.h>
#endif


/* ── Hash table slot ─────────────────────────────────────────────────────
 * 8 bytes per slot (was 16):
 *   fp   : upper 32 bits of h64(ngram); 0 = empty-slot sentinel.
 *   meta : bits[23:0] = offset into ELD_scores_blob / ELD_lang_ids_blob;
 *          bits[31:24] = num_scores (max ~30 in practice, fits uint8_t).
 * The table is prebuilt at compile time and embedded in large_db.h;
 * init_detector() just points 'ht' at it — no calloc, no insertion loop.
 * ─────────────────────────────────────────────────────────────────────── */
typedef struct {
    uint32_t fp;    /* fingerprint: upper 32 bits of h64(ngram); 0 = empty */
    uint32_t meta;  /* (num_scores << 24) | data_ptr                        */
} Slot;
#include <stdint.h>  /* ensure uint32_t is available before large_db.h */

/* ── splitmix64 ──────────────────────────────────────────────────────────
 * Returns the full 64-bit hash.
 *   Lower 32 bits → bucket index (& ht_mask).
 *   Upper 32 bits → fingerprint stored in Slot.fp.
 * Using both halves of one hash call costs nothing extra vs the old code
 * that discarded the upper 32 bits. ─────────────────────────────────── */
static inline uint64_t h64(uint64_t v) {
    v ^= v >> 30; v *= 0xbf58476d1ce4e5b9ULL; v ^= v >> 27;
    return v;
}

/* ── Database header ─────────────────────────────────────────────────────── */
#ifdef ELD_DB_INCLUDE
#  include ELD_DB_INCLUDE
#else
#  include "large_db.h"
#endif
#include "eld_unicode_bits.h"
#include "eld_iso639_2t.h"
#include "eld_tolower.h"

/* ═══════════════════════════════════════════════════════════════════════════
 * Bitset lookup tables
 * ═══════════════════════════════════════════════════════════════════════════ */
static uint8_t letter_bits[8192];
static uint8_t cjk_bits[8192];

static inline int is_letter(uint32_t cp) {
    if (__builtin_expect(cp > 0xFFFF, 0)) return 0;
    return (letter_bits[cp >> 3] >> (cp & 7)) & 1;
}
static inline int is_cjk(uint32_t cp) {
    if (__builtin_expect(cp < 0x1100 || cp > 0xFFDC, 1)) return 0;
    return (cjk_bits[cp >> 3] >> (cp & 7)) & 1;
}

/* ── 2-byte tolower byte-pair table ─────────────────────────────────────── */
static uint16_t g_tolower_bytes2[1920];

/* ── Hash table runtime state ────────────────────────────────────────────── */
static const Slot *ht      = NULL;   /* points into ELD_hashtable (prebuilt)  */
static uint32_t    ht_sz   = 0;
static uint32_t    ht_mask = 0;

/* ── Dedup set — ELD_THREAD_LOCAL: each thread owns its own dedup state ──── */
#define DS_BITS 10
#define DS_SIZE (1u << DS_BITS)
#define DS_MASK (DS_SIZE - 1u)
static ELD_THREAD_LOCAL uint64_t g_ds_keys[DS_SIZE];
static ELD_THREAD_LOCAL uint32_t g_ds_gen[DS_SIZE];
static ELD_THREAD_LOCAL uint32_t g_call_gen = 0;

/* ── Output scheme ───────────────────────────────────────────────────────── */
typedef enum { SCHEME_ISO639_1, SCHEME_ISO639_2T } Scheme;
static Scheme g_scheme = SCHEME_ISO639_1;
static inline const char *lang_code(int idx) {
    return g_scheme == SCHEME_ISO639_2T ? ELD_ISO639_2T[idx] : ELD_langCodes[idx];
}

/* ── Language subset filter ──────────────────────────────────────────────── */
static uint64_t g_lang_mask = (uint64_t)-1;
static int      g_subset    = 0;

#define PF_DIST 16

/* ═══════════════════════════════════════════════════════════════════════════
 * Scoring constants and types
 * ═══════════════════════════════════════════════════════════════════════════ */

/* ELD_NORM_FACTOR: exponent scale for the geometric-mean normalisation.
 * ns = 1 - raw^(-ELD_NORM_FACTOR / nk)
 * Changing this requires recalibrating ELD_avg_score[] in the database. */
#define ELD_NORM_FACTOR 3.5f

/* Maximum scores that can be requested or returned. */
#define ELD_MAX_SCORES  20

/* Worst-case formatted output bytes for eld_emit_result().
 * Header (~42) + 10 × "\"xxx\":0.9234," (~14 each) + footer (~3) ≈ 185.
 * 512 for up to 20 scores */
#define ELD_EMIT_BUFSIZE 512

typedef struct { int idx; float ns; } EldScoreEntry;

/* ═══════════════════════════════════════════════════════════════════════════
 * EldConfig — per-call detection configuration
 *
 *   scores  : 0 = no scores output
 *             1..ELD_MAX_SCORES = return top-N normalised scores
 *             When reliable=1 and scores=1, top-2 are computed internally
 *             for the reliability check but only 1 is returned.
 *   reliable: 0 = skip; 1 = compute reliability flag (needs top-2)
 *   scheme  : 0 = ISO 639-1 (default); 1 = ISO 639-2/T
 * ═══════════════════════════════════════════════════════════════════════════ */
typedef struct {
    int      scores;    /* 0=disabled, 1..ELD_MAX_SCORES = top-N to return */
    int      reliable;
    int      scheme;    /* 0=iso639-1, 1=iso639-2t */
    uint64_t lang_mask;
    int      subset;
} EldConfig;

/* ═══════════════════════════════════════════════════════════════════════════
 * EldResult — output of eld_process_line()
 *   lang      : NULL if not detected; pointer to static storage otherwise
 *   reliable  : 0/1; valid only when cfg.reliable was 1
 *   n_entries : number of filled entries[]; 0 when cfg.scores was 0
 * ═══════════════════════════════════════════════════════════════════════════ */
typedef struct {
    const char   *language;
    int           reliable;
    int           n_entries;
    EldScoreEntry entries[ELD_MAX_SCORES];
} EldResult;

/* ═══════════════════════════════════════════════════════════════════════════
 * init_detector()
 *
 * Sets up Unicode tables and points 'ht' at the prebuilt hash table embedded
 * in large_db.h.  No calloc, no insertion loop — init is now nearly instant.
 * ═══════════════════════════════════════════════════════════════════════════ */
static void init_detector(void)
{
    /* Catch accidental slot-layout drift between large_db.h and eld_core.c. */
#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L
    _Static_assert(sizeof(Slot) == 8, "Slot must be exactly 8 bytes");
#endif

    memcpy(letter_bits, LETTER_BITS, sizeof letter_bits);
    memcpy(cjk_bits,    CJK_BITS,    sizeof cjk_bits);

    for (int i = 0; i < 1920; i++) {
        uint32_t cp  = (uint32_t)i + 0x80;
        uint32_t out = TOLOWER_BMP2[i] ? (uint32_t)TOLOWER_BMP2[i] : cp;
        g_tolower_bytes2[i] = (uint16_t)(((0xC0 | (out >> 6)) << 8)
                                        |  (0x80 | (out & 0x3F)));
    }

    ht      = ELD_hashtable;    /* prebuilt, lives in read-only data segment  */
    ht_sz   = ELD_HT_SZ;
    ht_mask = ELD_HT_MASK;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * utf8_tolower_buf()
 * ═══════════════════════════════════════════════════════════════════════════ */
static inline uint64_t swar_tolower8(uint64_t w) {
    uint64_t t    = w & 0xDFDFDFDFDFDFDFDFULL;        /* fold a-z onto A-Z for the test */
    uint64_t a    = t + 0x3F3F3F3F3F3F3F3FULL;        /* bit7 set iff byte(t) >= 0x41 */
    uint64_t b    = t + 0x2525252525252525ULL;        /* bit7 set iff byte(t) >= 0x5B */
    uint64_t mask = (a & ~b) & 0x8080808080808080ULL; /* bit7 set iff byte(t) in [0x41,0x5A] */
    return w | (mask >> 2);
}

static void utf8_tolower_buf(const char *src, size_t len, char *dst)
{
    unsigned char *ud = (unsigned char *)dst;
    size_t i = 0;
    while (i < len) {
        unsigned char b0 = (unsigned char)src[i];

        if (b0 < 0x80) {
            if (i + 8 <= len) {
                uint64_t chunk; memcpy(&chunk, src + i, 8);
                if (!(chunk & 0x8080808080808080ULL)) {
                    chunk = swar_tolower8(chunk);
                    memcpy(ud + i, &chunk, 8);
                    i += 8; continue;
                }
            }
            ud[i++] = (b0 >= 'A' && b0 <= 'Z') ? (b0 + 32u) : b0;
            continue;
        }

        if (b0 < 0xC2 || i + 1 >= len) { ud[i] = b0; i++; continue; }

        if (b0 < 0xE0) {
            uint32_t idx  = ((uint32_t)(b0 - 0xC2) << 6)
                          | ((unsigned char)src[i+1] & 0x3F);
            uint16_t pair = g_tolower_bytes2[idx];
            ud[i]   = (unsigned char)(pair >> 8);
            ud[i+1] = (unsigned char) pair;
            i += 2; continue;
        }

        if (b0 < 0xF0) {
            if (i + 2 >= len) { ud[i] = b0; i++; continue; }
            unsigned char b1 = (unsigned char)src[i+1];
            unsigned char b2 = (unsigned char)src[i+2];
            if (b0 == 0xE1) {
                if      (b1 == 0x82 && b2 >= 0xA0)
                    { ud[i]=0xE2; ud[i+1]=0xB4; ud[i+2]=b2-0x20u; }
                else if (b1 == 0x83 && b2 >= 0x80 && b2 <= 0x85)
                    { ud[i]=0xE2; ud[i+1]=0xB4; ud[i+2]=b2+0x20u; }
                else if (b1 >= 0xB8 && b1 <= 0xBB && !(b2 & 1)
                         && !(b1 == 0xBA && b2 == 0x9E))
                    { ud[i]=b0; ud[i+1]=b1; ud[i+2]=b2+1u; }
                else
                    { ud[i]=b0; ud[i+1]=b1; ud[i+2]=b2; }
            } else if (b0==0xEF && b1==0xBC && b2>=0xA1 && b2<=0xBA)
                { ud[i]=0xEF; ud[i+1]=0xBD; ud[i+2]=b2-0x20u; }
            else
                { ud[i]=b0; ud[i+1]=b1; ud[i+2]=b2; }
            i += 3; continue;
        }

        int w = (i+3 < len) ? 4 : (int)(len - i);
        for (int k = 0; k < w; k++) ud[i+k] = (unsigned char)src[i+k];
        i += (size_t)w;
    }
    dst[len] = '\0';
}

/* ═══════════════════════════════════════════════════════════════════════════
 * nextchar()
 * ═══════════════════════════════════════════════════════════════════════════ */
static inline int nextchar(const unsigned char *p, size_t rem, int *type)
{
    unsigned char b = *p;
    if (b < 0x80) {
        *type = (b>='a'&&b<='z')||(b>='A'&&b<='Z') ? 1
              : (b==0x27||b==0x60)                   ? 3 : 0;
        return 1;
    }
    if (__builtin_expect(b < 0xC2 || rem < 2, 0)) { *type=0; return 1; }
    if (b < 0xE0) {
        *type = is_letter(((uint32_t)(b&0x1F)<<6)|(p[1]&0x3F)) ? 1 : 0;
        return 2;
    }
    if (b < 0xF0) {
        if (__builtin_expect(rem < 3, 0)) { *type=0; return 1; }
        uint32_t cp = ((uint32_t)(b&0x0F)<<12)|((uint32_t)(p[1]&0x3F)<<6)|(p[2]&0x3F);
        if (cp==0x2019) { *type=3; return 3; }
        if (is_cjk(cp)) { *type=2; return 3; }
        *type = is_letter(cp) ? 1 : 0; return 3;
    }
    if (__builtin_expect(rem < 4, 0)) { *type=0; return (int)rem; }
    uint32_t cp = ((uint32_t)(b&0x07)<<18)|((uint32_t)(p[1]&0x3F)<<12)
                |((uint32_t)(p[2]&0x3F)<<6)|(p[3]&0x3F);
    *type = ((cp>=0x20000&&cp<=0x2A6DF)||(cp>=0x2A700&&cp<=0x2CEAF)
            ||(cp>=0x2CEB0&&cp<=0x2EBEF)||(cp>=0x2F800&&cp<=0x2FA1F)) ? 2 : 0;
    return 4;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * detect_ex()
 * ═══════════════════════════════════════════════════════════════════════════ */
static const char *detect_ex(const char *text,
                      float        scores_out[MAX_LANGUAGES],
                      int         *ngram_count_out)
{
    if (__builtin_expect(!text || !*text, 0)) return NULL;

    size_t ilen = strnlen(text, 1002);
    size_t len  = ilen > 1000 ? 1000 : ilen;
    while (len > 0 && len < ilen && ((unsigned char)text[len] & 0xC0) == 0x80) len--;

    char buf[1025];
    utf8_tolower_buf(text, len, buf);

    if (__builtin_expect(++g_call_gen == 0, 0)) {
        g_call_gen = 1; memset(g_ds_gen, 0, sizeof g_ds_gen);
    }
    const uint32_t gen = g_call_gen;

    float ls[MAX_LANGUAGES];
    for (int i = 0; i < MAX_LANGUAGES; i++) ls[i] = 1.0f;

    struct { uint32_t fp; uint32_t hv; } nk[500];
    int nk_cnt = 0;

#define ADD_NGRAM(k64) do {                                              \
    if (nk_cnt < 500) {                                                  \
        uint64_t _h  = h64(k64);                                        \
        uint32_t _hv = (uint32_t)_h;                                    \
        uint32_t _fp = (uint32_t)(_h >> 32);                            \
        if (__builtin_expect(!_fp, 0)) _fp = 1u;  /* 0 = empty sentinel */ \
        uint32_t _dh = (_hv >> (32-DS_BITS)) & DS_MASK;                 \
        int _dup = 0;                                                    \
        while (g_ds_gen[_dh] == gen) {                                  \
            if (g_ds_keys[_dh] == (k64)) { _dup=1; break; }            \
            _dh = (_dh+1) & DS_MASK;                                    \
        }                                                                \
        if (!_dup) {                                                     \
            g_ds_gen[_dh]=gen; g_ds_keys[_dh]=(k64);                   \
            nk[nk_cnt].fp=_fp; nk[nk_cnt].hv=_hv; nk_cnt++;           \
        }                                                                \
    } } while(0)

    const unsigned char *p = (const unsigned char *)buf, *end = p + len;
    while (__builtin_expect(p < end && nk_cnt < 500, 1)) {
        int type, w; w = nextchar(p, (size_t)(end-p), &type);

        if (type == 2) {
            uint64_t k=0; char *kp=(char*)&k;
            kp[0]=' '; memcpy(kp+1, p, (size_t)w); kp[1+w]=' ';
            ADD_NGRAM(k); p += w; continue;
        }
        if (type != 1) { p += w; continue; }

        const unsigned char *ws = p; p += w;
        while (p < end) {
            int it, iw; iw = nextchar(p, (size_t)(end-p), &it);
            if (it == 1) { p += iw; }
            else if (it == 3) {
                const unsigned char *q = p + iw;
                if (q < end) {
                    int nt, nw2; nw2=nextchar(q,(size_t)(end-q),&nt); (void)nw2;
                    if (nt==1) { p+=iw; } else break;
                } else break;
            } else break;
        }

        const size_t wlen = (size_t)(p-ws); const char *word = (const char*)ws;
        uint64_t k; char *kp = (char*)&k;
        if (wlen <= 6) {
            k=0; kp[0]=' '; memcpy(kp+1,word,wlen); kp[1+wlen]=' ';
            ADD_NGRAM(k);
        } else {
            k=0; kp[0]=' '; memcpy(kp+1,word,6);          ADD_NGRAM(k);
            if (nk_cnt>=500) break;
            for (size_t j=6; j+6<wlen&&nk_cnt<500; j+=6) {
                k=0; memcpy(kp,word+j,6);                  ADD_NGRAM(k);
            }
            if (nk_cnt>=500) break;
            k=0; memcpy(kp,word+wlen-6,6); kp[6]=' ';     ADD_NGRAM(k);
        }
    }
#undef ADD_NGRAM

    if (ngram_count_out) *ngram_count_out = nk_cnt;
    if (__builtin_expect(!nk_cnt, 0)) return NULL;

    for (int i = 0; i < nk_cnt; i++) {
        if (__builtin_expect(i+PF_DIST < nk_cnt, 1))
            __builtin_prefetch(&ht[nk[i+PF_DIST].hv & ht_mask], 0, 1);
        uint32_t fp = nk[i].fp; uint32_t lh = nk[i].hv & ht_mask;
        while (ht[lh].fp) {
            if (ht[lh].fp == fp) {
                uint32_t meta = ht[lh].meta;
                uint32_t off  = meta & 0x00FFFFFFu;
                uint8_t  n    = (uint8_t)(meta >> 24);
                for (uint8_t j = 0; j < n; j++)
                    ls[ELD_lang_ids_blob[off+j]] *= ELD_scores_blob[off+j];
                break;
            }
            lh = (lh+1) & ht_mask;
        }
    }

    if (scores_out) memcpy(scores_out, ls, MAX_LANGUAGES * sizeof(float));

    int   best = -1;
    float best_score = 1.0f;
    if (!g_subset) {
        for (int i = 0; i < MAX_LANGUAGES; i++)
            if (ls[i] > best_score) { best_score = ls[i]; best = i; }
    } else {
        for (int i = 0; i < MAX_LANGUAGES; i++) {
            if (!((g_lang_mask >> i) & 1)) continue;
            if (ls[i] > best_score) { best_score = ls[i]; best = i; }
        }
    }
    return (best >= 0) ? lang_code(best) : NULL;
}

/* detect() — simple convenience wrapper; returns "und" instead of NULL.
 * Not currently called by any front-end (each has its own eldc_detect /
 * py_detect / eld_process_line path), kept for C callers that #include
 * eld_core.c directly. static avoids an exported symbol in libeldc.so;
 * the unused-function attribute (GCC/Clang only) silences the resulting
 * "defined but not used" warning when nothing references it. */
#if defined(__GNUC__) || defined(__clang__)
__attribute__((unused))
#endif
static const char *detect(const char *text)
{
    const char *r = detect_ex(text, NULL, NULL);
    return r ? r : "und";
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Reliability scoring
 *
 * A detection is "reliable" when:
 *   1. The top normalised score is >= 82% of that language's corpus average.
 *   2. The gap between top and second score is > 4 percentage points.
 *      (If no second language matched, the gap condition is always satisfied.)
 * ═══════════════════════════════════════════════════════════════════════════ */
static int eld_is_reliable(int best_idx, float best_ns, float second_ns, int nk)
{
	 if (nk < 3)                                       return 0;
    if (best_idx < 0 || best_ns <= 0.0f)              return 0;
    if (best_ns < 0.82f * ELD_avg_score[best_idx])    return 0;
    if ((best_ns - second_ns) <= 0.04f)               return 0;
    return 1;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * eld_normalize()
 *
 * Normalise a raw detect_ex() score array into [0,1] entries.
 *
 *   raw[]     : MAX_LANGUAGES raw scores from detect_ex().
 *   nk        : ngram count returned by detect_ex(); must be > 0.
 *   lang_mask : language bitmask (used when subset != 0).
 *   subset    : non-zero → apply lang_mask filter.
 *   sort      : non-zero → sort entries[] descending by ns on return.
 *   max_out   : > 0 → trim to top-max_out before powf(); 0 = no limit.
 *   entries[] : caller-supplied array of at least MAX_LANGUAGES slots.
 *
 * Returns the number of valid entries written.  When sort != 0, entries[0]
 * is the top language; entries[1] is the runner-up (needed for reliability).
 * ═══════════════════════════════════════════════════════════════════════════ */
static int eld_score_entry_cmp(const void *a, const void *b)
{
    float d = ((const EldScoreEntry *)b)->ns - ((const EldScoreEntry *)a)->ns;
    return (d > 0.0f) - (d < 0.0f);
}

static int eld_normalize(const float raw[MAX_LANGUAGES], int nk,
                          uint64_t lang_mask, int subset, int sort,
                          int max_out,
                          EldScoreEntry entries[MAX_LANGUAGES])
{
    int n = 0;
    for (int i = 0; i < MAX_LANGUAGES; i++) {
        if (subset && !((lang_mask >> i) & 1)) continue;
        if (raw[i] <= 1.0f) continue;
        entries[n].idx = i;
        entries[n].ns  = raw[i];
        n++;
    }

    int presorted = 0;
    if (max_out > 0 && n > max_out) {
        qsort(entries, (size_t)n, sizeof(EldScoreEntry), eld_score_entry_cmp);
        n        = max_out;
        presorted = 1;
    }

    float inv_nk = -ELD_NORM_FACTOR / (float)nk;
    for (int i = 0; i < n; i++)
        entries[i].ns = 1.0f - powf(entries[i].ns, inv_nk);

    if (sort && !presorted && n > 1)
        qsort(entries, (size_t)n, sizeof(EldScoreEntry), eld_score_entry_cmp);

    return n;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * eld_process_line()
 *
 * Full detection pipeline for one text string into an EldResult.
 * Thread-safe (detect_ex uses thread-local state; EldResult is caller-owned).
 *
 * scores count and reliable check interaction:
 *   When cfg.reliable=1 and cfg.scores < 2, the normalisation internally
 *   computes top-2 so the reliability gap check has a second score to
 *   compare against.  Only cfg.scores entries are stored in result->entries.
 * ═══════════════════════════════════════════════════════════════════════════ */
static void eld_process_line(const char    *text,
                              const EldConfig *cfg,
                              EldResult       *r)
{
    /* Fast path: no filter, no scores, no reliable check.
     * A scheme override alone does NOT disable this path: g_scheme is kept
     * in sync with cfg->scheme by every set_scheme() path (eldc_lib.c,
     * _eldc_module.c, eld.c/eld_mt.c --scheme), and detect_ex()'s returned
     * code already uses g_scheme, so r->language comes back in the requested
     * scheme either way. */
    if (!cfg->subset && !cfg->scores && !cfg->reliable) {
        const char *language = detect_ex(text, NULL, NULL);
        r->language      = language;   /* NULL = not detected */
        r->n_entries = 0;
        r->reliable  = 0;
        return;
    }

    /* General path: get raw scores from detect_ex. */
    float raw[MAX_LANGUAGES];
    int   nk = 0;
    detect_ex(text, raw, &nk);

    if (nk == 0) {
        r->language      = NULL;
        r->n_entries = 0;
        r->reliable  = 0;
        return;
    }

    int n_scores_out = cfg->scores;           /* 0 = disabled */
    int want_reliable = cfg->reliable;
    int sort = (n_scores_out > 0) || want_reliable;

    /* How many entries to normalise:
     *   - At least n_scores_out (for output).
     *   - At least 2 when reliable check is needed (requires runner-up score).
     *   - 0 when neither scores nor reliable: fall-through to linear scan. */
    int max_out = n_scores_out;
    if (want_reliable && max_out < 2) max_out = 2;

    EldScoreEntry entries[MAX_LANGUAGES];
    int n = eld_normalize(raw, nk, cfg->lang_mask, cfg->subset,
                          sort, max_out, entries);

    /* ── Best language index ─────────────────────────────────────────────── */
    int best = -1;
    if (n > 0) {
        if (sort) {
            best = entries[0].idx;   /* already sorted: cheapest path */
        } else {
            float bns = -1.0f;
            for (int j = 0; j < n; j++)
                if (entries[j].ns > bns) { bns = entries[j].ns; best = entries[j].idx; }
        }
    }

    r->language = (best >= 0)
            ? (cfg->scheme ? ELD_ISO639_2T[best] : ELD_langCodes[best])
            : NULL;

    /* ── Reliability check (uses top-2, always available after sort) ─────── */
    r->reliable = (want_reliable && best >= 0)
                ? eld_is_reliable(best,
                                   n > 0 ? entries[0].ns : 0.0f,
                                   n > 1 ? entries[1].ns : 0.0f,
											  nk)
                : 0;

    /* ── Store scores for output — only up to n_scores_out entries ───────── */
    if (n_scores_out > 0) {
        r->n_entries = (n < n_scores_out) ? n : n_scores_out;
        for (int j = 0; j < r->n_entries; j++) r->entries[j] = entries[j];
    } else {
        r->n_entries = 0;
    }
}

#endif /* ELD_CORE_C_INCLUDED */
