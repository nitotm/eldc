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

/* global hastable delete */
ELD_API void eldc_close(void)
{
    ht = NULL; ht_sz = 0; ht_mask = 0;
}


/* ── Private language-parsing helper ─────────────────────────────────────── */
/* Parses a comma-separated codes string into *out_mask / *out_subset.
 * Returns a comma-separated ISO 639-1 string of matched codes in result_buf.
 * Unrecognised codes are printed to stderr and skipped. */
static const char *_parse_languages(const char *codes,
                                     uint64_t *out_mask, int *out_subset,
                                     char *result_buf, size_t result_sz)
{
    result_buf[0] = '\0';
    if (!codes || !*codes) {
        *out_mask = (uint64_t)-1; *out_subset = 0;
        return result_buf;
    }

    uint64_t mask = 0;
    char    *dst  = result_buf;
    char    *lim  = result_buf + result_sz - 1;
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

    if (mask) { *out_mask = mask; *out_subset = 1; }
    return result_buf;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Configuration setters
 * ═══════════════════════════════════════════════════════════════════════════ */
static ELD_THREAD_LOCAL char _lang_result[512];

ELD_API const char *eldc_set_languages_cfg(EldConfig *cfg, const char *codes)
{
	 if (!cfg) return NULL;
    return _parse_languages(codes,
                            &cfg->lang_mask, &cfg->subset,
                            _lang_result, sizeof _lang_result);
}

ELD_API void eldc_set_scheme_cfg(EldConfig *cfg, const char *scheme)
{
	 if (!cfg) return;
    if (!scheme) return;
    if (!strcmp(scheme,"iso639-1")||!strcmp(scheme,"iso639_1"))
        cfg->scheme = SCHEME_ISO639_1;
    else if (!strcmp(scheme,"iso639-2t")||!strcmp(scheme,"iso639_2t"))
        cfg->scheme = SCHEME_ISO639_2T;
}

ELD_API void eldc_set_scores_cfg(EldConfig *cfg, int n)
{
    if (!cfg) return;
    if (n < 1) n = 1;
    if (n > ELD_MAX_SCORES) n = ELD_MAX_SCORES;
    cfg->scores = n;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Global setters
 * ═══════════════════════════════════════════════════════════════════════════ */

ELD_API const char *eldc_set_languages(const char *codes)
{
    return eldc_set_languages_cfg(&g_config, codes);
}

ELD_API void eldc_set_scheme(const char *scheme)
{
    eldc_set_scheme_cfg(&g_config, scheme);
}

ELD_API void eldc_set_scores(int n)
{
    eldc_set_scores_cfg(&g_config, n);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Detection
 * ═══════════════════════════════════════════════════════════════════════════ */
ELD_API const char *eldc_detect_cfg(EldConfig *cfg, const char *text)
{
    if (__builtin_expect(!ht, 0)) {
        fprintf(stderr, "eldc error: library not initialized. Call eldc_init() before detecting.\n");
        return NULL;
    }
    return detect_ex(text, NULL, NULL, cfg);
}

ELD_API const char *eldc_detect(const char *text)
{
    if (__builtin_expect(!ht, 0)) {
        fprintf(stderr, "eldc error: library not initialized. Call eldc_init() before detecting.\n");
        return NULL;
    }
    return detect_ex(text, NULL, NULL, NULL);   /* NULL → uses g_config */
}

ELD_API void eldc_detect_details_cfg(EldConfig *cfg, const char *text, EldcDetectResult *result)
{
    if (__builtin_expect(!ht, 0)) {
        fprintf(stderr, "eldc error: library not initialized. Call eldc_init() before detecting.\n");
        if (result) {
            result->language = "und";
            result->reliable = 0;
            result->n_scores = 0;
        }
        return;
    }
    if (!result) return; // detect_ex(text, NULL, NULL, cfg);

    EldResult r = {0};
    eld_process_line(text, cfg, &r);

    result->language = r.language;
    result->reliable = r.reliable;
    result->n_scores = r.n_entries;

    for (int i = 0; i < r.n_entries; i++) {
        result->scores[i].language = cfg->scheme
                                   ? ELD_ISO639_2T[r.entries[i].idx]
                                   : ELD_langCodes[r.entries[i].idx];
        result->scores[i].score = r.entries[i].ns;
    }
    // return result->language;
}

ELD_API void eldc_detect_details(const char *text, EldcDetectResult *result)
{
    eldc_detect_details_cfg(&g_config, text, result);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Isolated configuration Instance
 * Multiple configuration instances and the global API can coexist safely.
 * ═══════════════════════════════════════════════════════════════════════════ */
ELD_API EldConfig *eldc_config_create(void)
{
    EldConfig *cfg = malloc(sizeof *cfg);
    if (!cfg) return NULL;
    *cfg = default_config;   // copy contents
    return cfg;
}

ELD_API void eldc_config_free(EldConfig *cfg)
{
    free(cfg);
}