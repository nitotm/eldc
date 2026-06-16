/*
 * eldc_lib.c — ELD-C shared library implementation
 *
 * Compile:
 *   Linux/macOS:
 *     gcc  -O3 -march=native -shared -fPIC -DELD_BUILD_DLL -o libeldc.so  eldc_lib.c -lm
 *     clang -O3 -march=native -shared -fPIC -DELD_BUILD_DLL -o libeldc.dylib eldc_lib.c -lm
 *
 *   Windows (MinGW-w64):
 *     gcc -O3 -shared -DELD_BUILD_DLL -o eldc.dll eldc_lib.c -lm -Wl,--out-implib,libeldc.a
 *
 *   Windows (MSVC):
 *     cl /O2 /LD /DELD_BUILD_DLL eldc_lib.c /Fe:eldc.dll
 *
 *   Custom DB or larger hash table:
 *     gcc ... -DELD_DB_INCLUDE='"my_db.h"' ...
 */

#ifndef _WIN32
#  ifndef _POSIX_C_SOURCE
#    define _POSIX_C_SOURCE 200809L
#  endif
#endif

#include "eld_core.c"
#include "eldc_lib.h"

/* ── Library-level config ────────────────────────────────────────────────── */
static int      _lib_scores    = 3; /* min 1; detect_details always returns scores */
static int      _lib_scheme    = 0;
static uint64_t _lib_lang_mask = (uint64_t)-1;
static int      _lib_subset    = 0;

/*
 * No auto-init on library load (by design)
 *
 * Loading libeldc.so/.dll does NOT allocate the n-gram hash table — callers
 * must call eldc_init() once before detection.  This keeps the cost of
 * merely *linking against* the library at zero: a process that loads it but
 * never calls a detect function never pays the ~16 MB table allocation.
 * ═══════════════════════════════════════════════════════════════════════════ */

/* ═══════════════════════════════════════════════════════════════════════════
 * Lifecycle
 * ═══════════════════════════════════════════════════════════════════════════ */
ELD_API void eldc_init(void)
{
    init_detector();
}

ELD_API void eldc_close(void)
{
    ht = NULL; ht_sz = 0; ht_mask = 0;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Configuration setters
 * ═══════════════════════════════════════════════════════════════════════════ */
/* Static buffer for the return value — valid until the next call.
 * Set_languages is a config function called before detection, so no
 * thread-safety concern here. */
static char _lang_result[512];

ELD_API const char *eldc_set_languages(const char *codes)
{
    _lang_result[0] = '\0';

    if (!codes || !*codes) {
        _lib_lang_mask = (uint64_t)-1; _lib_subset = 0;
        g_lang_mask    = (uint64_t)-1; g_subset    = 0;
        return _lang_result;   /* empty string = filter cleared, all languages active */
    }

    uint64_t mask = 0;
    char    *dst  = _lang_result;
    char    *lim  = _lang_result + sizeof _lang_result - 1;
    int      first = 1;

    char buf[512];
    snprintf(buf, sizeof buf, "%s", codes);
    char *saveptr = NULL;
    for (char *tok = strtok_r(buf, ",", &saveptr);
         tok;
         tok = strtok_r(NULL, ",", &saveptr))
    {
        while (*tok == ' ') tok++;
        char *e = tok + strlen(tok) - 1;
        while (e > tok && *e == ' ') *e-- = '\0';
        if (!*tok) continue;

        int found = 0;
        for (int i = 0; i < MAX_LANGUAGES; i++) {
            if (!strcmp(tok, ELD_langCodes[i]) || !strcmp(tok, ELD_ISO639_2T[i])) {
                mask |= 1ULL << i;
                found = 1;
                /* Append canonical ISO 639-1 code to return buffer. */
                const char *code = ELD_langCodes[i];
                size_t      clen = strlen(code);
                if (dst + (first ? 0 : 1) + clen < lim) {
                    if (!first) *dst++ = ',';
                    memcpy(dst, code, clen);
                    dst += clen;
                    first = 0;
                }
                break;
            }
        }
        if (!found)
            fprintf(stderr, "eldc: unknown language '%s' (ignored)\n", tok);
    }
    *dst = '\0';

    if (mask) {
        _lib_lang_mask = mask; _lib_subset = 1;
        g_lang_mask    = mask; g_subset    = 1;
    }
    return _lang_result;
}

ELD_API void eldc_set_scheme(const char *scheme)
{
    if (!scheme) return;
    if (!strcmp(scheme,"iso639-1")||!strcmp(scheme,"iso639_1")) {
        _lib_scheme = 0; g_scheme = SCHEME_ISO639_1;
    } else if (!strcmp(scheme,"iso639-2t")||!strcmp(scheme,"iso639_2t")) {
        _lib_scheme = 1; g_scheme = SCHEME_ISO639_2T;
    }
}

ELD_API void eldc_set_scores(int n)
{
    if (n < 1) n = 1;                  /* detect_details always returns scores */
    if (n > ELD_MAX_SCORES) n = ELD_MAX_SCORES;
    _lib_scores = n;
}


/* ═══════════════════════════════════════════════════════════════════════════
 * Detection
 *
 * eldc_detect — unconditional detect_ex() wrapper.
 *   detect_ex uses g_lang_mask, g_subset, g_scheme globals which are kept
 *   in sync by the setters above, so no extra condition checks are needed.
 *
 * eldc_detect_details — always passes reliable=1 to eld_process_line.
 *   _lib_scores is clamped to >= 1, so result->scores always has at least
 *   one entry (default 3, max ELD_LIB_MAX_SCORES).
 * ═══════════════════════════════════════════════════════════════════════════ */
ELD_API const char *eldc_detect(const char *text)
{
    if (__builtin_expect(!ht, 0)) {
        fprintf(stderr, "eldc error: library not initialized. Call eldc_init() before detecting.\n");
        return NULL;
    }
	 
    const char *r = detect_ex(text, NULL, NULL);
    return r ? r : "und";
}

ELD_API const char *eldc_detect_details(const char *text, EldcDetectResult *result)
{
    if (__builtin_expect(!ht, 0)) {
        fprintf(stderr, "eldc error: library not initialized. Call eldc_init() before detecting.\n");
        if (result) {
            result->language = NULL;
            result->reliable = 0;
            result->n_scores = 0;
        }
        return NULL;
    }
    if (!result) return eldc_detect(text);

    EldConfig cfg;
    cfg.scores    = _lib_scores;
    cfg.reliable  = 1;              /* always on in detect_details */
    cfg.scheme    = _lib_scheme;
    cfg.lang_mask = _lib_lang_mask;
    cfg.subset    = _lib_subset;

    EldResult r;
    eld_process_line(text, &cfg, &r);

    result->language = r.language ? r.language : "und";
    result->reliable = r.reliable;
    result->n_scores = r.n_entries;

    for (int i = 0; i < r.n_entries; i++) {
        result->scores[i].language  = _lib_scheme
                                    ? ELD_ISO639_2T[r.entries[i].idx]
                                    : ELD_langCodes[r.entries[i].idx];
        result->scores[i].score = r.entries[i].ns;
    }

    return result->language;
}
