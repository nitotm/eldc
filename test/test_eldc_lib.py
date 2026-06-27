"""
test_eldc_lib.py — Assertion tests for the eldc shared library (ctypes).

Build the library before running:
    Linux:   gcc -O3 -shared -fPIC -DELD_BUILD_DLL -o libeldc.so  eldc_lib.c -lm
    macOS:   gcc -O3 -shared -fPIC -DELD_BUILD_DLL -o libeldc.dylib eldc_lib.c -lm
    Windows: gcc -O3 -shared -DELD_BUILD_DLL -o eldc.dll eldc_lib.c -lm -Wl,--out-implib,libeldc.a

Run with:
    python -m pytest tests/test_eldc_lib.py -v
    # or
    python test_eldc_lib.py
"""

import ctypes
import sys
import os
import unittest
import concurrent.futures

# ── Load library ──────────────────────────────────────────────────────────────
_names    = {"win32": "eldc.dll", "darwin": "libeldc.dylib"}
_lib_name = _names.get(sys.platform, "libeldc.so")
_lib_path = os.path.join(os.path.dirname(__file__), _lib_name)

try:
    lib            = ctypes.CDLL(_lib_path)
    _lib_available = True
except OSError:
    _lib_available = False
    lib            = None

# ── Struct definitions (must match eldc_lib.h exactly) ────────────────────────
ELD_LIB_MAX_SCORES = 20

class EldcScoreItem(ctypes.Structure):
    _fields_ = [
        ("language", ctypes.c_char_p),
        ("score",    ctypes.c_float),
    ]

class EldcDetectResult(ctypes.Structure):
    _fields_ = [
        ("language", ctypes.c_char_p),
        ("reliable", ctypes.c_int),
        ("n_scores", ctypes.c_int),
        ("scores",   EldcScoreItem * ELD_LIB_MAX_SCORES),
    ]

# ── Register signatures only when library loaded ──────────────────────────────
if _lib_available:
    # Global API
    lib.eldc_init.restype            = None
    lib.eldc_init.argtypes           = []

    lib.eldc_close.restype           = None
    lib.eldc_close.argtypes          = []

    lib.eldc_detect.restype          = ctypes.c_char_p
    lib.eldc_detect.argtypes         = [ctypes.c_char_p]

    lib.eldc_detect_details.restype  = None               # void — result via struct
    lib.eldc_detect_details.argtypes = [ctypes.c_char_p,
                                         ctypes.POINTER(EldcDetectResult)]

    lib.eldc_set_languages.restype   = ctypes.c_char_p
    lib.eldc_set_languages.argtypes  = [ctypes.c_char_p]

    lib.eldc_set_scheme.restype      = None
    lib.eldc_set_scheme.argtypes     = [ctypes.c_char_p]

    lib.eldc_set_scores.restype      = None
    lib.eldc_set_scores.argtypes     = [ctypes.c_int]

    # Instance (cfg) API — EldcConfig is opaque, use c_void_p
    lib.eldc_config_create.restype  = ctypes.c_void_p
    lib.eldc_config_create.argtypes = []

    lib.eldc_config_free.restype  = None
    lib.eldc_config_free.argtypes = [ctypes.c_void_p]

    lib.eldc_detect_cfg.restype   = ctypes.c_char_p
    lib.eldc_detect_cfg.argtypes  = [ctypes.c_void_p, ctypes.c_char_p]

    lib.eldc_detect_details_cfg.restype  = None
    lib.eldc_detect_details_cfg.argtypes = [ctypes.c_void_p, ctypes.c_char_p,
                                              ctypes.POINTER(EldcDetectResult)]

    lib.eldc_set_languages_cfg.restype  = ctypes.c_char_p
    lib.eldc_set_languages_cfg.argtypes = [ctypes.c_void_p, ctypes.c_char_p]

    lib.eldc_set_scheme_cfg.restype  = None
    lib.eldc_set_scheme_cfg.argtypes = [ctypes.c_void_p, ctypes.c_char_p]

    lib.eldc_set_scores_cfg.restype  = None
    lib.eldc_set_scores_cfg.argtypes = [ctypes.c_void_p, ctypes.c_int]


# ── Helpers ───────────────────────────────────────────────────────────────────
def s(b):
    """Decode a c_char_p bytes value to str; return '' for None."""
    return b.decode("utf-8") if b else ""

def detect(text: bytes) -> str:
    return s(lib.eldc_detect(text))

def detect_details(text: bytes) -> EldcDetectResult:
    r = EldcDetectResult()
    lib.eldc_detect_details(text, ctypes.byref(r))
    return r

def score_langs(r: EldcDetectResult) -> list:
    return [s(r.scores[i].language) for i in range(r.n_scores)]


# ── Custom runner: blank line between each test in verbose output ─────────────
class _SpacedResult(unittest.TextTestResult):
    def addSuccess(self, test):
        super().addSuccess(test)
        if self.showAll: self.stream.writeln()

    def addError(self, test, err):
        super().addError(test, err)
        if self.showAll: self.stream.writeln()

    def addFailure(self, test, err):
        super().addFailure(test, err)
        if self.showAll: self.stream.writeln()

    def addSkip(self, test, reason):
        super().addSkip(test, reason)
        if self.showAll: self.stream.writeln()

class _SpacedRunner(unittest.TextTestRunner):
    resultclass = _SpacedResult


# ── Base class shared by all test cases ───────────────────────────────────────
@unittest.skipUnless(_lib_available,
    f"Shared library not found at {_lib_path!r} — build it first.")
class _LibTestCase(unittest.TestCase):

    @classmethod
    def setUpClass(cls):
        lib.eldc_init()

    @classmethod
    def tearDownClass(cls):
        lib.eldc_close()

    def tearDown(self):
        lib.eldc_set_languages(b"")
        lib.eldc_set_scheme(b"iso639-1")
        lib.eldc_set_scores(3)


# ── Test groups ───────────────────────────────────────────────────────────────

class TestLibDetect(_LibTestCase):
    """Tests for eldc_detect()."""

    def test_returns_bytes(self):
        self.assertIsInstance(lib.eldc_detect(b"Bonjour le monde"), bytes)

    def test_detect_french(self):
        self.assertEqual(detect(b"Bonjour le monde"), "fr")

    def test_detect_undetermined_returns_und(self):
        self.assertEqual(detect(b"12345 !@#"), "und")


class TestLibDetectDetails(_LibTestCase):
    """Tests for eldc_detect_details()."""

    def test_language_field(self):
        r = detect_details(b"Bonjour le monde")
        self.assertEqual(s(r.language), "fr")

    def test_reliable_field_is_one(self):
        r = detect_details(b"Bonjour le monde")
        self.assertEqual(r.reliable, 1)

    def test_n_scores_within_bounds(self):
        r = detect_details(b"Bonjour le monde")
        self.assertGreater(r.n_scores, 0)
        self.assertLessEqual(r.n_scores, ELD_LIB_MAX_SCORES)

    def test_top_score_matches_detected_language(self):
        r = detect_details(b"Bonjour le monde")
        self.assertIn("fr", score_langs(r))

    def test_language_field_set_correctly(self):
        r = EldcDetectResult()
        lib.eldc_detect_details(b"Bonjour le monde", ctypes.byref(r))
        self.assertEqual(s(r.language), "fr")

    def test_scores_have_valid_floats(self):
        r = detect_details(b"Bonjour le monde")
        for i in range(r.n_scores):
            self.assertIsInstance(r.scores[i].score, float)
            self.assertGreaterEqual(r.scores[i].score, 0.0)


class TestLibSetScores(_LibTestCase):
    """Tests for eldc_set_scores()."""

    def test_set_scores_two(self):
        lib.eldc_set_scores(2)
        r = detect_details(b"Was ist das?")
        self.assertEqual(r.n_scores, 2)

    def test_set_scores_detects_correct_language(self):
        lib.eldc_set_scores(2)
        r = detect_details(b"Was ist das?")
        self.assertEqual(s(r.language), "de")

    def test_set_scores_minimum_one(self):
        lib.eldc_set_scores(1)
        r = detect_details(b"Bonjour le monde")
        self.assertEqual(r.n_scores, 1)

    def test_set_scores_ten(self):
        lib.eldc_set_scores(10)
        r = detect_details(b"Bonjour le monde")
        self.assertLessEqual(r.n_scores, 10)
        self.assertGreaterEqual(r.n_scores, 1)


class TestLibSetLanguages(_LibTestCase):
    """Tests for eldc_set_languages()."""

    def test_returns_matched_iso639_1_codes(self):
        matched = lib.eldc_set_languages(b"en,fr,es,de")
        self.assertEqual(s(matched), "en,fr,es,de")

    def test_accepts_iso639_2t_input(self):
        matched = lib.eldc_set_languages(b"eng,fra")
        self.assertEqual(s(matched), "en,fr")

    def test_unknown_code_skipped_in_return(self):
        matched = lib.eldc_set_languages(b"eng,fra,xyz")
        self.assertEqual(s(matched), "en,fr")

    def test_filter_restricts_to_matched_set(self):
        lib.eldc_set_languages(b"en,fr")
        self.assertNotEqual(detect(b"Hola mundo"), "es")

    def test_language_in_set_detected_correctly(self):
        lib.eldc_set_languages(b"en,fr,es,de")
        self.assertEqual(detect(b"Bonjour le monde"), "fr")

    def test_empty_string_clears_filter(self):
        lib.eldc_set_languages(b"en,fr")
        cleared = lib.eldc_set_languages(b"")
        self.assertEqual(s(cleared), "")

    def test_clear_restores_all_languages(self):
        lib.eldc_set_languages(b"en,fr")
        lib.eldc_set_languages(b"")
        self.assertEqual(detect(b"Hola mundo"), "es")


class TestLibSetScheme(_LibTestCase):
    """Tests for eldc_set_scheme()."""

    def test_iso639_2t_detect(self):
        lib.eldc_set_scheme(b"iso639-2t")
        self.assertEqual(detect(b"Bonjour"), "fra")

    def test_iso639_2t_details_language_field(self):
        lib.eldc_set_scheme(b"iso639-2t")
        r = detect_details(b"Hello world")
        self.assertEqual(s(r.language), "eng")

    def test_iso639_2t_score_languages_are_three_letters(self):
        lib.eldc_set_scheme(b"iso639-2t")
        r = detect_details(b"Hello world")
        self.assertTrue(all(len(lang) == 3 for lang in score_langs(r)))

    def test_reset_to_iso639_1_detect(self):
        lib.eldc_set_scheme(b"iso639-2t")
        lib.eldc_set_scheme(b"iso639-1")
        self.assertEqual(detect(b"Bonjour"), "fr")

    def test_reset_to_iso639_1_score_languages_are_two_letters(self):
        lib.eldc_set_scheme(b"iso639-2t")
        lib.eldc_set_scheme(b"iso639-1")
        r = detect_details(b"Bonjour le monde")
        self.assertTrue(all(len(lang) == 2 for lang in score_langs(r)))


class TestLibCfgInstance(_LibTestCase):
    """Tests for eldc_config_create() / _cfg isolated configuration.

    Verifies that instance configs are isolated from the global config
    and from each other.
    """

    def _make_cfg(self):
        """Create a cfg and register cleanup so it's always freed."""
        cfg = lib.eldc_config_create()
        self.addCleanup(lib.eldc_config_free, cfg)
        return cfg

    def test_instance_detect_returns_correct_language(self):
        cfg = self._make_cfg()
        self.assertEqual(s(lib.eldc_detect_cfg(cfg, b"Bonjour le monde")), "fr")

    def test_instance_detect_und_for_garbage(self):
        cfg = self._make_cfg()
        self.assertEqual(s(lib.eldc_detect_cfg(cfg, b"12345 !@#")), "und")

    def test_instance_scheme_independent_of_global(self):
        lib.eldc_set_scheme(b"iso639-2t")     # change global
        cfg = self._make_cfg()
        lib.eldc_set_scheme_cfg(cfg, b"iso639-1")
        # Instance uses iso639-1, global is iso639-2t
        self.assertEqual(s(lib.eldc_detect_cfg(cfg, b"Bonjour")), "fr")
        self.assertEqual(detect(b"Bonjour"), "fra")

    def test_global_scheme_independent_of_instance(self):
        cfg = self._make_cfg()
        lib.eldc_set_scheme_cfg(cfg, b"iso639-2t")
        # Global must be unaffected
        self.assertEqual(detect(b"Bonjour"), "fr")
        self.assertEqual(s(lib.eldc_detect_cfg(cfg, b"Bonjour")), "fra")

    def test_instance_language_filter_independent_of_global(self):
        lib.eldc_set_languages(b"en,fr")      # global filtered
        cfg = self._make_cfg()
        lib.eldc_set_languages_cfg(cfg, b"de,es")
        self.assertEqual(s(lib.eldc_detect_cfg(cfg, b"Hola mundo")), "es")
        self.assertNotEqual(detect(b"Hola mundo"), "es")  # global still en,fr only

    def test_global_filter_independent_of_instance(self):
        cfg = self._make_cfg()
        lib.eldc_set_languages_cfg(cfg, b"en,fr")
        # Global must be unaffected
        self.assertEqual(detect(b"Hola mundo"), "es")

    def test_instance_scores_independent_of_global(self):
        lib.eldc_set_scores(3)
        cfg = self._make_cfg()
        lib.eldc_set_scores_cfg(cfg, 7)
        r_cfg = EldcDetectResult()
        lib.eldc_detect_details_cfg(cfg, b"Bonjour le monde", ctypes.byref(r_cfg))
        self.assertEqual(r_cfg.n_scores, 7)
        r_global = detect_details(b"Bonjour le monde")
        self.assertEqual(r_global.n_scores, 3)

    def test_two_instances_are_independent(self):
        cfg1 = self._make_cfg()
        cfg2 = self._make_cfg()
        lib.eldc_set_scheme_cfg(cfg1, b"iso639-2t")
        # cfg2 unchanged — must still return iso639-1
        self.assertEqual(s(lib.eldc_detect_cfg(cfg1, b"Bonjour")), "fra")
        self.assertEqual(s(lib.eldc_detect_cfg(cfg2, b"Bonjour")), "fr")

    def test_global_change_after_instance_creation_does_not_affect_instance(self):
        cfg = self._make_cfg()
        lib.eldc_set_scheme(b"iso639-2t")     # global changes after instance created
        # Instance was created with factory defaults (iso639-1) — must be unaffected
        self.assertEqual(s(lib.eldc_detect_cfg(cfg, b"Bonjour")), "fr")

    def test_instance_detect_details_language_field(self):
        cfg = self._make_cfg()
        lib.eldc_set_scheme_cfg(cfg, b"iso639-2t")
        r = EldcDetectResult()
        lib.eldc_detect_details_cfg(cfg, b"Bonjour le monde", ctypes.byref(r))
        self.assertEqual(s(r.language), "fra")

    def test_instance_detect_details_score_codes_match_scheme(self):
        cfg = self._make_cfg()
        lib.eldc_set_scheme_cfg(cfg, b"iso639-2t")
        r = EldcDetectResult()
        lib.eldc_detect_details_cfg(cfg, b"Bonjour le monde", ctypes.byref(r))
        self.assertTrue(all(len(s(r.scores[i].language)) == 3
                            for i in range(r.n_scores)))

    def test_instance_language_filter_restricts_detect_details(self):
        cfg = self._make_cfg()
        lib.eldc_set_languages_cfg(cfg, b"en,fr")
        r = EldcDetectResult()
        lib.eldc_detect_details_cfg(cfg, b"Hola mundo", ctypes.byref(r))
        self.assertNotEqual(s(r.language), "es")

    def test_combined_scheme_and_filter_on_instance(self):
        cfg = self._make_cfg()
        lib.eldc_set_scheme_cfg(cfg, b"iso639-2t")
        lib.eldc_set_languages_cfg(cfg, b"en,fr,de")
        self.assertEqual(s(lib.eldc_detect_cfg(cfg, b"Bonjour")), "fra")
        self.assertNotEqual(s(lib.eldc_detect_cfg(cfg, b"Hola")), "spa")
        # Global must be fully unaffected
        self.assertEqual(detect(b"Bonjour"), "fr")
        self.assertEqual(detect(b"Hola"), "es")


class TestLibThreadSafety(_LibTestCase):
    """Tests for concurrent use of eldc_detect_details()."""

    def test_concurrent_detect_details_no_exceptions(self):
        texts = [
            b"Hello world", b"Bonjour", b"Hola", b"Ciao",
            b"\xd9\x85\xd8\xb1\xd8\xad\xd8\xa8\xd8\xa7",
        ]

        def worker(text):
            r = EldcDetectResult()
            lib.eldc_detect_details(text, ctypes.byref(r))
            return s(r.language), bool(r.reliable)

        with concurrent.futures.ThreadPoolExecutor(max_workers=4) as pool:
            results = list(pool.map(worker, texts))

        self.assertEqual(len(results), len(texts))
        self.assertTrue(all(isinstance(lang, str) and lang for lang, _ in results))

    def test_concurrent_detect_returns_correct_languages(self):
        texts    = [b"Hello world", b"Bonjour", b"Hola", b"Ciao",
                    b"\xd9\x85\xd8\xb1\xd8\xad\xd8\xa8\xd8\xa7"]
        expected = ["en", "fr", "es", "it", "ar"]

        def worker(text):
            r = EldcDetectResult()
            lib.eldc_detect_details(text, ctypes.byref(r))
            return s(r.language)

        with concurrent.futures.ThreadPoolExecutor(max_workers=4) as pool:
            results = list(pool.map(worker, texts))

        self.assertEqual(results, expected)


if __name__ == "__main__":
    unittest.main(testRunner=_SpacedRunner(verbosity=2))
