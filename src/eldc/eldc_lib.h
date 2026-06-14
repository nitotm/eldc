/*
 * eldc_lib.h — Public API for the ELD-C shared library
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
 * Quick start (C):
 *   #include "eldc_lib.h"
 *   eldc_init();                          // load DB; call once at startup
 *   printf("%s\n", eldc_detect("Bonjour le monde"));  // "fr"
 *
 *   EldcDetectResult r;
 *   eldc_set_scores(5);
 *   eldc_detect_details("Bonjour le monde", &r);
 *   printf("language=%s reliable=%d score=%.4f\n",
 *          r.language, r.reliable, r.scores[0].score);
 *   eldc_close();
 *
 * Quick start (PHP FFI):
 *   $ffi = FFI::cdef(file_get_contents("eldc_lib.h"), "eldc.dll");
 *   $ffi->eldc_init();                          // load DB; call once at startup
 *   echo $ffi->eldc_detect("Bonjour le monde");  // "fr"
 *
 * Thread safety:
 *   eldc_init() must complete before any detection starts.
 *   eldc_detect() and eldc_detect_details() are fully thread-safe after that.
 *   Config setters (eldc_set_*) are NOT thread-safe; call them before
 *   starting parallel detection, or protect with your own mutex.
 */

#pragma once
#ifdef __cplusplus
extern "C" {
#endif

/* ── Export / import marker ──────────────────────────────────────────────── */
#ifdef _WIN32
#  ifdef ELD_BUILD_DLL
#    define ELD_API __declspec(dllexport)
#  else
#    define ELD_API __declspec(dllimport)
#  endif
#else
#  ifdef ELD_BUILD_DLL
#    define ELD_API __attribute__((visibility("default")))
#  else
#    define ELD_API
#  endif
#endif

/* ── Constants ───────────────────────────────────────────────────────────── */
#define ELD_LIB_MAX_SCORES 20

/* ── Result types ────────────────────────────────────────────────────────── */

/* One language/score pair inside EldcDetectResult. */
typedef struct {
    const char *language; /* ISO 639-1 or 639-2/T code; static storage, never freed */
    float       score;    /* normalised confidence [0.0, 1.0] */
} EldcScoreItem;

/* Full detection result populated by eldc_detect_details(). */
typedef struct {
    const char  *language;                        /* "und" if not detected       */
    int          reliable;                        /* 1=reliable, 0=not reliable  */
    int          n_scores;                        /* valid entries in scores[]   */
    EldcScoreItem scores[ELD_LIB_MAX_SCORES];     /* top-N scores, descending    */
} EldcDetectResult;

/* ── Lifecycle ───────────────────────────────────────────────────────────── */

/* Load the n-gram database and build the hash table.
 * Must be called once before any detection — loading the library does NOT
 * allocate the table, so FFI callers (PHP, Python ctypes, …) DO need to call
 * this explicitly, exactly like C callers.
*/
ELD_API void eldc_init(void);

/* Release all resources. */
ELD_API void eldc_close(void);

/* ── Configuration ───────────────────────────────────────────────────────── */

/* Restrict detection to a comma-separated list of language codes.
 * Accepts ISO 639-1 ("en,fr,de") or ISO 639-2/T ("eng,fra,deu") or mixed.
 * Pass NULL or "" to reset to all languages.
 *
 * Returns a comma-separated string of the ISO 639-1 codes that were actually
 * matched (e.g. input "en,fra,xyz" → returns "en,fr", warns about "xyz").
 * Returns "" when the filter is cleared (all languages active).
 * The returned pointer is valid until the next call to eldc_set_languages.
 * Unrecognised codes are printed to stderr and skipped. */
ELD_API const char *eldc_set_languages(const char *codes);

/* Set the language code scheme used in all output.
 * "iso639-1"  (default) → "en", "fr", "zh", …
 * "iso639-2t"           → "eng", "fra", "zho", … */
ELD_API void eldc_set_scheme(const char *scheme);

/* Control how many top scores eldc_detect_details() returns.
 *   n is clamped to [1, ELD_LIB_MAX_SCORES]; values below 1 become 1, since
 *   detect_details() always returns at least one score.  Default: 3.
 * Reliability is always computed regardless of this setting. */
ELD_API void eldc_set_scores(int n);

/* Switch hash-table size. Must be called before eldc_init()
 *   0 — 32 MB (default)
 *   1 — 64 MB (marginal speedup; rebuilds the table immediately) */
ELD_API void eldc_set_faster(int flag);

/* ── Detection ───────────────────────────────────────────────────────────── */

/* Detect the language of text.
 * Returns a pointer to static storage: a language code (e.g. "fr") or "und".
 * Always returns a non-NULL string.  Thread-safe. */
ELD_API const char *eldc_detect(const char *text);

/* Detect with reliability flag and optional top-N scores.
 * Fills *result and returns the language code ("und" if none).
 * result must point to a caller-allocated EldcDetectResult.
 * Thread-safe. */
ELD_API const char *eldc_detect_details(const char *text, EldcDetectResult *result);

#ifdef __cplusplus
}
#endif
