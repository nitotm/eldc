"""
demo_eldc_module.py — Usage demo for the eldc Python C extension.

The extension module (_eldc.so / _eldc.pyd) is built from _eldc_module.c.
Typical build via setup.py:
    python setup.py build_ext --inplace
"""

import warnings
import concurrent.futures
import eldc   # the Python wrapper around _eldc

# ── 1. Initialise ────────────────────────────────────────────────────────────
# Must be called before any detection.  Loads the n-gram DB into a hash table.
eldc.init()

# ── 2. detect() — fast path ──────────────────────────────────────────────────
# Always returns a str: a language code or "und" if undetermined.
print("-- detect() --")
print(eldc.detect("Bonjour le monde"))           # fr
print(eldc.detect("12345 !@#"))                 # und  (no recognisable text)


# ── 3. detect_details() — full result ───────────────────────────────────────
# Always returns reliability + top-N scores.
# Result attributes: .language (str|None), .reliable (bool), .scores (dict)
print("-- detect_details() --")
result = eldc.detect_details("Bonjour le monde")
print(f"language : {result.language}")
print(f"reliable : {result.reliable}")
print(f"scores   : {result.scores}")  # top-10 by default
print(f"as str   : {str(result)}")    # __str__ returns the language code
print(f"bool     : {bool(result)}")   # False when language is None
print()

# ── 4. set_scores() — control how many scores appear ────────────────────────
# Minimum 1 (detect_details always returns scores).  Default: 3. Max 20.
print("-- set_scores(10) --")
eldc.set_scores(10)
result = eldc.detect_details("Was ist das? Todo bien en la costa.")
print(f"language : {result.language}")            # de
print(f"scores   : {result.scores}")              # top-10 only
eldc.set_scores(3)                                # reset to default
print()

# ── 5. set_languages() — restrict the candidate set ─────────────────────────
# Accepts a comma-separated str OR a list[str].
# Returns list[str] of the ISO 639-1 codes that were actually matched.
# Unrecognised codes produce a UserWarning and are skipped.
# Pass "" or [] to clear the filter.
print("-- set_languages() --")

matched = eldc.set_languages("en, fr, es, de")
print(f"str input   matched: {matched}")          # ['en', 'fr', 'es', 'de']

matched = eldc.set_languages(["it", "pt", "nl"])
print(f"list input  matched: {matched}")          # ['it', 'pt', 'nl']

# Unknown code triggers UserWarning (not an exception)
with warnings.catch_warnings(record=True) as w:
    warnings.simplefilter("always")
    matched = eldc.set_languages(["en", "xyz", "fr"])
    if w:
        print(f"warning: {w[0].message}")        # eldc: unknown language code 'xyz'
print(f"partial match: {matched}")               # ['en', 'fr']

print(eldc.detect("Bonjour le monde"))           # fr  (only en/fr active)
print(eldc.detect("Hola mundo"))                 # en  (es not in filter → falls to en)

matched = eldc.set_languages("")                 # clear filter → all languages
print(f"cleared: {matched}")                     # []
print(eldc.detect("Hola mundo"))                 # es  (back to all languages)
print()

# ── 6. set_scheme() — ISO 639-1 vs ISO 639-2/T output ───────────────────────
print("-- set_scheme() --")
eldc.set_scheme("iso639-2t")
print(eldc.detect("Bonjour"))                    # fra
result = eldc.detect_details("Hello world")
print(result.language, list(result.scores.keys())[:3])  # eng ['eng', 'deu', ...]

eldc.set_scheme("iso639-1")                      # reset to default
print(eldc.detect("Bonjour"))                    # fr
print()

# ── 7. Result equality and truthiness ───────────────────────────────────────
print("-- Result comparisons --")
result = eldc.detect_details("Bonjour le monde")
print(result == "fr")                            # True
print(result == "en")                            # False
print(bool(eldc.detect_details("12345 !@#")))   # False (language is None)
print()


# ── 8. LANGUAGES constant ───────────────────────────────────────────────────
print("-- available constants --")
print(f"MAX_LANGUAGES : {eldc.MAX_LANGUAGES}")
print(f"MAX_SCORES    : {eldc.MAX_SCORES}")
print(f"First 5 codes : {eldc.LANGUAGES[:5]}")
print(f"First 5 ISO2T : {eldc.LANGUAGES_ISO2T[:5]}")
print()

# ── 9. Multi-threaded detection ─────────────────────────────────────────────
# The GIL is released inside detect() and detect_details(), so threads
# genuinely run in parallel on multi-core machines.
print("-- multithreaded detection --")
texts = [
    "Hello world", "Bonjour le monde", "Hola mundo",
    "Ciao mondo", "Привет мир", "مرحبا بالعالم", "你好世界",
]
with concurrent.futures.ThreadPoolExecutor(max_workers=4) as pool:
    langs = list(pool.map(eldc.detect, texts))
for text, lang in zip(texts, langs):
    print(f"  {lang}  {text}")
