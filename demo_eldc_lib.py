"""
demo_eldc_lib.py — Usage demo for the ELD-C shared library via ctypes.

For Python I RECOMEND USING official package "pip install eldc"
The package is easier to install, and can run faster than the library.

The library is built from eld_lib.c:
  Linux:   gcc -O3 -shared -fPIC -DELD_BUILD_DLL -o libeldc.so  eldc_lib.c -lm
  macOS:   gcc -O3 -shared -fPIC -DELD_BUILD_DLL -o libeldc.dylib eldc_lib.c -lm
  Windows: gcc -O3 -shared -DELD_BUILD_DLL -o eldc.dll eldc_lib.c -lm -Wl,--out-implib,libeldc.a

No Python package installation needed — just ctypes (stdlib).
"""

import ctypes
import sys
import os

# ── Load the library ─────────────────────────────────────────────────────────
_names = {"win32": "eldc.dll", "darwin": "libeldc.dylib"}
_lib_name = _names.get(sys.platform, "libeldc.so")
_lib_path = os.path.join(os.path.dirname(__file__), _lib_name)

try:
    lib = ctypes.CDLL(_lib_path)
except OSError as e:
    sys.exit(f"Could not load {_lib_path}: {e}\nBuild with: "
             "gcc -O3 -shared -fPIC -DELD_BUILD_DLL -o libeldc.so eldc_lib.c -lm")

# ── Struct definitions (must match eld_lib.h exactly) ────────────────────────
ELD_LIB_MAX_SCORES = 20

class EldcScoreItem(ctypes.Structure):
    _fields_ = [
        ("lang",  ctypes.c_char_p),   # static storage in the library, never freed
        ("score", ctypes.c_float),
    ]

class EldcDetectResult(ctypes.Structure):
    _fields_ = [
        ("lang",     ctypes.c_char_p),
        ("reliable", ctypes.c_int),
        ("n_scores", ctypes.c_int),
        ("scores",   EldcScoreItem * ELD_LIB_MAX_SCORES),
    ]

# ── Function signatures ───────────────────────────────────────────────────────
lib.eldc_init.restype            = None
lib.eldc_init.argtypes           = []

lib.eldc_close.restype           = None
lib.eldc_close.argtypes          = []

lib.eldc_detect.restype          = ctypes.c_char_p
lib.eldc_detect.argtypes         = [ctypes.c_char_p]

lib.eldc_detect_details.restype  = ctypes.c_char_p
lib.eldc_detect_details.argtypes = [ctypes.c_char_p, ctypes.POINTER(EldcDetectResult)]

lib.eldc_set_languages.restype   = ctypes.c_char_p  # matched codes, or "" if cleared
lib.eldc_set_languages.argtypes  = [ctypes.c_char_p]

lib.eldc_set_scheme.restype      = None
lib.eldc_set_scheme.argtypes     = [ctypes.c_char_p]

lib.eldc_set_scores.restype      = None
lib.eldc_set_scores.argtypes     = [ctypes.c_int]

lib.eldc_set_faster.restype      = None
lib.eldc_set_faster.argtypes     = [ctypes.c_int]

# ── Helper: decode c_char_p safely ───────────────────────────────────────────
def s(b): return b.decode("utf-8") if b else ""

# ── 0. eldc_set_faster() — larger hash table, call before init() ─────────────
# 0 → 32 MB (default).  1 → 64 MB. Not recommended, minimal speedup.
# lib.eldc_set_faster(1)

# ── 1. Initialise ────────────────────────────────────────────────────────────
# eldc_init() is the only required call before detection.
lib.eldc_init()
print("=== eldc library demo (ctypes) ===\n")

# ── 2. eldc_detect() — fast path ─────────────────────────────────────────────
# Returns bytes (c_char_p); decode to str.  Always "und" when undetermined.
print("-- eldc_detect() --")
print(s(lib.eldc_detect(b"Bonjour le monde")))          # fr
print(s(lib.eldc_detect(b"12345 !@#")))                 # und
print()

# ── 3. eldc_detect_details() — full result ───────────────────────────────────
# Fills an EldcDetectResult struct.  Returns the language code as c_char_p.
# .reliable: 1=reliable, 0=uncertain.  .n_scores: number of valid score entries.
print("-- eldc_detect_details() --")
r = EldcDetectResult()
lib.eldc_detect_details(b"Bonjour le monde", ctypes.byref(r))
print(f"language : {s(r.lang)}")
print(f"reliable : {bool(r.reliable)}")
print(f"scores   : ", end="")
for i in range(r.n_scores):
    print(f"{s(r.scores[i].lang)}={r.scores[i].score:.4f}", end="  ")
print("\n")

# ── 4. eldc_set_scores() — number of scores returned ─────────────────────────
# Minimum 1. Default: 3. Max 20.
print("-- eldc_set_scores(2) --")
lib.eldc_set_scores(2)
# We could reuse r, if we make sure to only read up to n_scores, but not thread-safe
r = EldcDetectResult()
lib.eldc_detect_details(b"Was ist das?", ctypes.byref(r))
print(f"language : {s(r.lang)},  n_scores: {r.n_scores}")  # de, 2
scores_str = "  ".join(f"{s(r.scores[i].lang)}={r.scores[i].score:.4f}"
                        for i in range(r.n_scores))
print(f"scores   : {scores_str}")
lib.eldc_set_scores(10)   # reset
print()

# ── 5. eldc_set_languages() — restrict candidate set ────────────────────────
# Input: comma-separated codes (ISO 639-1 or ISO 639-2/T, or mixed).
# Returns: comma-separated ISO 639-1 codes that were actually matched.
#          "" means filter cleared / all languages active.
# Unknown codes are printed to stderr and skipped.
print("-- eldc_set_languages() --")
matched = lib.eldc_set_languages(b"en,fr,es,de")
print(f"matched: {s(matched)}")                    # en,fr,es,de

matched = lib.eldc_set_languages(b"eng,fra,xyz")   # ISO 639-2/T input + unknown
print(f"matched: {s(matched)}")                    # en,fr  (xyz warned to stderr)

print(s(lib.eldc_detect(b"Bonjour le monde")))     # fr
print(s(lib.eldc_detect(b"Hola mundo")))           # Not spanish, not in set

lib.eldc_set_languages(b"")                        # clear filter
print(f"cleared: '{s(lib.eldc_detect(b'Hola mundo'))}'")  # es
print()

# ── 6. eldc_set_scheme() — output code scheme ────────────────────────────────
print("-- eldc_set_scheme() --")
lib.eldc_set_scheme(b"iso639-2t")
print(s(lib.eldc_detect(b"Bonjour")))              # fra
r = EldcDetectResult()
lib.eldc_detect_details(b"Hello world", ctypes.byref(r))
print(s(r.lang), [s(r.scores[i].lang) for i in range(min(r.n_scores, 3))])

lib.eldc_set_scheme(b"iso639-1")                   # reset
print(s(lib.eldc_detect(b"Bonjour")))              # fr
print()

# ── 7. Multithreaded detection ────────────────────────────────────────────────
# eldc_detect() and eldc_detect_details() are fully thread-safe.
import concurrent.futures, threading

texts = [b"Hello world", b"Bonjour", b"Hola", b"Ciao", b"\xd9\x85\xd8\xb1\xd8\xad\xd8\xa8\xd8\xa7"]

def detect_worker(text):
    # Each thread uses its own EldcDetectResult on the stack
    r = EldcDetectResult()
    lib.eldc_detect_details(text, ctypes.byref(r))
    return s(r.lang), bool(r.reliable)

print("-- multithreaded eldc_detect_details() --")
with concurrent.futures.ThreadPoolExecutor(max_workers=4) as pool:
    results = list(pool.map(detect_worker, texts))
for (lang, reliable), text in zip(results, texts):
    print(f"  {lang}  reliable={reliable}  {text}")
print()

# ── 8. Cleanup ───────────────────────────────────────────────────────────────
lib.eldc_close()
print("Library unloaded.")
