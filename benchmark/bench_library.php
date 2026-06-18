<?php

// Update with the folder containing the compiled library
$library_folder = __DIR__ . '/'; 

$lib_name =
    PHP_OS_FAMILY === 'Windows' ? $library_folder . 'eldc.dll' :
    (PHP_OS_FAMILY === 'Darwin' ? $library_folder . 'libeldc.dylib' 
    : $library_folder . 'libeldc.so');

$BENCHMARKS = [
    "tatoeba_50_v3",
    "eld_test",
    "sentences_v3",
    "word_pairs_v3",
    "single_words_v3"
];

$folder = __DIR__ . "/text_files/";

$tatoeba_50_langs = 
    "ar, az, be, bg, bn, ca, cs, da, de, el, en, es, et, eu, fa, fi, fr, gu, ".
    "he, hi, hr, hu, hy, is, it, ja, ka, ko, lt, lv, ms, nl, no, pl, pt, ro, ".
    "ru, sk, sl, sq, sv, ta, th, tl, tr, uk, ur, vi, yo, zh";


$ffi = FFI::cdef('
    void        eldc_init(void);
    void        eldc_close(void);
    const char *eldc_detect(const char *text);
    const char *eldc_set_languages(const char *codes);    
', $lib_name);
$ffi->eldc_init();

$results = [];

foreach ($BENCHMARKS as $testName) {
    $totalLines = 0;
    $correct = 0;
    $duration = 0.0;

    $textFile = $folder . $testName . ".txt";
    $langFile = $folder . $testName . ".languages.txt";

    if (!file_exists($textFile)) {
        echo "[{$testName}] Skipped — file not found: {$textFile}\n";
        continue;
    }

    if (!file_exists($langFile)) {
        echo "[{$testName}] Skipped — file not found: {$langFile}\n";
        continue;
    }

    if ($testName === "tatoeba_50_v3") {
        $ffi->eldc_set_languages($tatoeba_50_langs);
    } else {
        $ffi->eldc_set_languages("");
    }

    $tf = new SplFileObject($textFile);
    $lf = new SplFileObject($langFile);

    while (!$tf->eof() && !$lf->eof()) {
        $line = rtrim($tf->fgets(), "\r\n");
        $expected = trim($lf->fgets());

        if ($line === '') {
            continue;
        }

        $startTime = hrtime(true);

        $res = $ffi->eldc_detect($line);

        $duration += (hrtime(true) - $startTime);

        $totalLines++;

        if ($res === $expected) {
            $correct++;
        }
    }

    $duration = $duration / 1e9;

    $accuracy = $totalLines > 0
        ? ($correct / $totalLines) * 100
        : 0.0;

    $results[] = [
        $testName,
        $duration,
        $accuracy,
        $totalLines
    ];

    echo "[{$testName}]\n";
    echo "  Lines    : " . number_format($totalLines) . "\n";
    echo "  Duration : " . number_format($duration, 4) . "s\n";
    echo "  Accuracy : " . number_format($accuracy, 4) . "%\n\n";
}

$ffi->eldc_close();

if (!empty($results)) {
    $avgDuration = array_sum(array_column($results, 1)) / count($results);
    $avgAccuracy = array_sum(array_column($results, 2)) / count($results);
    $totalLines = array_sum(array_column($results, 3));

    echo str_repeat("=", 40) . "\n";
    echo "  Files processed : " . count($results) . "\n";
    echo "  Total lines     : " . number_format($totalLines) . "\n";
    echo "  Avg duration    : " . number_format($avgDuration, 4) . "s\n";
    echo "  Avg accuracy    : " . number_format($avgAccuracy, 4) . "%\n";
}

