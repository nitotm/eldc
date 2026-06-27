import eldc
import time

BENCHMARKS = ["tatoeba_50_v3", "eld_test", "sentences_v3", "word_pairs_v3", "single_words_v3"]

folder = "text_files/"

tatoeba_50_langs = ["ar", "az", "be", "bg", "bn", "ca", "cs", "da", "de", "el", "en", "es",
                    "et", "eu", "fa", "fi", "fr", "gu", "he", "hi", "hr", "hu", "hy", "is",
                    "it", "ja", "ka", "ko", "lt", "lv", "ms", "nl", "no", "pl", "pt", "ro",
                    "ru", "sk", "sl", "sq", "sv", "ta", "th", "tl", "tr", "uk", "ur", "vi",
                    "yo", "zh"]

eldc.init()
eldc_instance = eldc.instance()

results = []

for test_name in BENCHMARKS:
    total_lines = 0
    correct     = 0
    duration    = 0.0

    try:
        with open(folder + test_name + ".txt", encoding="utf-8") as tf, \
             open(folder + test_name + ".languages.txt", encoding="utf-8") as lf:

            if (test_name == "tatoeba_50_v3"):
                eldc.set_languages(tatoeba_50_langs)
                eldc_instance.set_languages(tatoeba_50_langs)
            else:
                eldc.set_languages([])
                eldc_instance.set_languages([])

            for line, expected in zip(tf, lf):
                line     = line.rstrip("\n")
                expected = expected.strip()
                if not line:
                    continue

                start_time = time.perf_counter()
                
                res       = eldc.detect(line)
                #res       = eldc.detect_details(line).language
                #res       = eldc_instance.detect(line)
                
                duration  += time.perf_counter() - start_time

                total_lines += 1
                if res == expected:
                    correct += 1

    except FileNotFoundError as e:
        print(f"[{test_name}] Skipped — file not found: {e.filename}")
        continue

    accuracy = (correct / total_lines * 100) if total_lines else 0.0
    results.append((test_name, duration, accuracy, total_lines))

    print(f"[{test_name}]")
    print(f"  Lines    : {total_lines:,}")
    print(f"  Duration : {duration:.4f}s")
    print(f"  Accuracy : {accuracy:.4f}%")
    print()

if results:
    avg_duration = sum(r[1] for r in results) / len(results)
    avg_accuracy = sum(r[2] for r in results) / len(results)
    total_lines  = sum(r[3] for r in results)

    print("=" * 40)
    print(f"  Files processed : {len(results)}")
    print(f"  Total lines     : {total_lines:,}")
    print(f"  Avg duration    : {avg_duration:.4f}s")
    print(f"  Avg accuracy    : {avg_accuracy:.4f}%")
