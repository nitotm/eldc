<?php
/**
 * I recommend using native PHP ELD implementation instead of ELDC library.
 * Simpler installation, more options, still fast.
 * "composer require nitotm/efficient-language-detector"
 *
 * demo_eldc_lib.php — ELDC shared library demo for PHP (FFI).
 * Requires PHP 7.4+ with FFI extension enabled (extension=ffi in php.ini).
 * Run: php demo_eldc_lib.php
 */

$base = __DIR__ . '/';
$lib_name =
    PHP_OS_FAMILY === 'Windows' ? $base . 'eldc.dll' :
    (PHP_OS_FAMILY === 'Darwin' ? $base . 'libeldc.dylib' : $base . 'libeldc.so');

$ffi = FFI::cdef('
    typedef struct { const char *language; float score; } EldcScoreItem;
    typedef struct {
        const char   *language;
        int           reliable;
        int           n_scores;
        EldcScoreItem scores[20];
    } EldcDetectResult;

    struct EldcConfig;
    typedef struct EldcConfig EldcConfig;

    void        eldc_init(void);
    void        eldc_close(void);
    const char *eldc_detect(const char *text);
    void        eldc_detect_details(const char *text, EldcDetectResult *result);
    const char *eldc_set_languages(const char *codes);
    void        eldc_set_scheme(const char *scheme);
    void        eldc_set_scores(int n);

    EldcConfig *eldc_config_create(void);
    void        eldc_config_free(EldcConfig *cfg);
    const char *eldc_detect_cfg(EldcConfig *cfg, const char *text);
    void        eldc_detect_details_cfg(EldcConfig *cfg, const char *text, EldcDetectResult *result);
    const char *eldc_set_languages_cfg(EldcConfig *cfg, const char *codes);
    void        eldc_set_scheme_cfg(EldcConfig *cfg, const char *scheme);
    void        eldc_set_scores_cfg(EldcConfig *cfg, int n);
', $lib_name);

// ── 1. init ──────────────────────────────────────────────────────────────────
$ffi->eldc_init();
echo "=== eldc PHP demo ===\n\n";
// ── Optional: Isolated configuration instance ─────────────────────────────────
$i_config = $ffi->eldc_config_create();

// ── 2. detect ────────────────────────────────────────────────────────────────
echo "-- detect --\n";
echo $ffi->eldc_detect("Bonjour le monde") . "\n";  // fr
echo $ffi->eldc_detect("12345 !@#") . "\n";         // und

echo $ffi->eldc_detect_cfg($i_config, "Bonjour");   // fr, Same behavior

// ── 3. detect_details ────────────────────────────────────────────────────────
echo "\n-- detect_details --\n";
$r = $ffi->new("EldcDetectResult");
$ffi->eldc_detect_details("Bonjour le monde", FFI::addr($r));
printf("language : %s  reliable: %s\n", $r->language, $r->reliable ? 'true' : 'false');
for ($i = 0; $i < $r->n_scores; $i++)
    printf("  %s: %.4f\n", $r->scores[$i]->language, $r->scores[$i]->score);

$ffi->eldc_detect_details_cfg($i_config, "Bonjour", FFI::addr($r)); // Same behavior

// ── 4. set_scores ────────────────────────────────────────────────────────────
echo "\n-- set_scores(2) --\n";
$ffi->eldc_set_scores(2); // global setter
// We could reuse $r, if we make sure to only read up to n_scores, not thread-safe
$r2 = $ffi->new("EldcDetectResult");
$ffi->eldc_detect_details("Was ist das?", FFI::addr($r2));
printf("language: %s  n_scores: %d\n", $r2->language, $r2->n_scores);  // de, 2
$ffi->eldc_set_scores(3);

$ffi->eldc_set_scores_cfg($i_config, 2); // instance setter

// ── 5. set_languages ─────────────────────────────────────────────────────────
echo "\n-- set_languages --\n";
echo $ffi->eldc_set_languages("en,fr,es") . "\n";    // global setter
echo $ffi->eldc_set_languages("eng,fra,xyz") . "\n"; // en,fr  (xyz to stderr)
echo $ffi->eldc_detect("Bonjour le monde") . "\n";   // fr
$ffi->eldc_set_languages("");
echo $ffi->eldc_detect("Hola doctor") . "\n";        // es

echo $ffi->eldc_set_languages_cfg($i_config, "en,fr,es"). "\n"; // instance setter

// ── 6. set_scheme ────────────────────────────────────────────────────────────
echo "\n-- set_scheme --\n";
$ffi->eldc_set_scheme("iso639-2t");  // global setter
echo $ffi->eldc_detect("Bonjour") . "\n";           // fra
$ffi->eldc_set_scheme("iso639-1");
echo $ffi->eldc_detect("Bonjour") . "\n";           // fr

$ffi->eldc_set_scheme_cfg($i_config, "iso639-2t");  // instance setter

// ── 7. cleanup ───────────────────────────────────────────────────────────────
$ffi->eldc_config_free($i_config);
// Global unload
$ffi->eldc_close();
