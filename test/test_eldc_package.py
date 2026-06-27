"""
test_eldc_package.py — Assertion tests for the eldc Python package.

Run with:
    python -m pytest tests/test_eldc_package.py -v
    # or
    python test_eldc_package.py
"""

import unittest
import warnings
import concurrent.futures
import eldc


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


# ── Base class: shared init + default teardown ────────────────────────────────
class _EldcTestCase(unittest.TestCase):

    @classmethod
    def setUpClass(cls):
        eldc.init()

    def tearDown(self):
        eldc.set_languages("")
        eldc.set_scheme("iso639-1")
        eldc.set_scores(3)


# ── Test groups ───────────────────────────────────────────────────────────────

class TestEldcDetect(_EldcTestCase):
    """Tests for eldc.detect() — fast path."""

    def test_detect_returns_str(self):
        self.assertIsInstance(eldc.detect("Bonjour le monde"), str)

    def test_detect_french(self):
        self.assertEqual(eldc.detect("Bonjour le monde"), "fr")

    def test_detect_undetermined_returns_und(self):
        self.assertEqual(eldc.detect("12345 !@#"), "und")


class TestEldcDetectDetails(_EldcTestCase):
    """Tests for eldc.detect_details() — full result object."""

    def test_language_attribute(self):
        result = eldc.detect_details("Bonjour le monde")
        self.assertEqual(result.language, "fr")

    def test_reliable_attribute_is_true(self):
        result = eldc.detect_details("Bonjour le monde")
        self.assertTrue(result.reliable)

    def test_scores_is_dict(self):
        result = eldc.detect_details("Bonjour le monde")
        self.assertIsInstance(result.scores, dict)

    def test_scores_contains_detected_language(self):
        result = eldc.detect_details("Bonjour le monde")
        self.assertIn("fr", result.scores)

    def test_str_returns_language_code(self):
        result = eldc.detect_details("Bonjour le monde")
        self.assertEqual(str(result), "fr")

    def test_bool_true_when_language_detected(self):
        result = eldc.detect_details("Bonjour le monde")
        self.assertTrue(bool(result))

    def test_bool_false_when_undetermined(self):
        result = eldc.detect_details("12345 !@#")
        self.assertFalse(bool(result))

    def test_equality_match(self):
        result = eldc.detect_details("Bonjour le monde")
        self.assertEqual(result, "fr")

    def test_equality_no_match(self):
        result = eldc.detect_details("Bonjour le monde")
        self.assertNotEqual(result, "en")

    def test_language_und_when_undetermined(self):
        result = eldc.detect_details("12345 !@#")
        self.assertEqual(result.language, "und")


class TestEldcSetScores(_EldcTestCase):
    """Tests for eldc.set_scores()."""

    def test_set_scores_limits_count(self):
        eldc.set_scores(2)
        result = eldc.detect_details("Bonjour le monde")
        self.assertEqual(len(result.scores), 2)

    def test_set_scores_minimum_one(self):
        eldc.set_scores(1)
        result = eldc.detect_details("Bonjour le monde")
        self.assertEqual(len(result.scores), 1)

    def test_set_scores_ten(self):
        eldc.set_scores(10)
        result = eldc.detect_details("Was ist das? A ship. Todo bien en la costa.")
        self.assertLessEqual(len(result.scores), 10)
        self.assertGreaterEqual(len(result.scores), 1)

    def test_set_scores_does_not_exceed_max(self):
        eldc.set_scores(eldc.MAX_SCORES)
        result = eldc.detect_details("Bonjour le monde")
        self.assertLessEqual(len(result.scores), eldc.MAX_SCORES)


class TestEldcSetLanguages(_EldcTestCase):
    """Tests for eldc.set_languages()."""

    def test_string_input_returns_matched_list(self):
        matched = eldc.set_languages("en, fr, es, de")
        self.assertEqual(matched, ["en", "fr", "es", "de"])

    def test_list_input_returns_matched_list(self):
        matched = eldc.set_languages(["it", "pt", "nl"])
        self.assertEqual(matched, ["it", "pt", "nl"])

    def test_unknown_code_triggers_user_warning(self):
        with warnings.catch_warnings(record=True) as w:
            warnings.simplefilter("always")
            eldc.set_languages(["en", "xyz", "fr"])
        self.assertTrue(any("xyz" in str(warning.message) for warning in w))

    def test_unknown_code_is_skipped_in_result(self):
        with warnings.catch_warnings(record=True):
            warnings.simplefilter("always")
            matched = eldc.set_languages(["en", "xyz", "fr"])
        self.assertEqual(matched, ["en", "fr"])

    def test_filter_restricts_detection(self):
        eldc.set_languages(["en", "fr"])
        self.assertNotEqual(eldc.detect("Hola mundo"), "es")

    def test_french_detected_within_filter(self):
        eldc.set_languages(["en", "fr"])
        self.assertEqual(eldc.detect("Bonjour le monde"), "fr")

    def test_spanish_detected_after_filter_in_set(self):
        eldc.set_languages(["en", "fr", "es", "de"])
        self.assertEqual(eldc.detect("Hola mundo"), "es")

    def test_empty_string_clears_filter(self):
        eldc.set_languages(["en", "fr"])
        matched = eldc.set_languages("")
        self.assertEqual(matched, [])

    def test_empty_list_clears_filter(self):
        eldc.set_languages(["en", "fr"])
        matched = eldc.set_languages([])
        self.assertEqual(matched, [])

    def test_clear_restores_all_languages(self):
        eldc.set_languages(["en", "fr"])
        eldc.set_languages("")
        self.assertEqual(eldc.detect("Hola mundo"), "es")


class TestEldcSetScheme(_EldcTestCase):
    """Tests for eldc.set_scheme()."""

    def test_iso639_2t_detect(self):
        eldc.set_scheme("iso639-2t")
        self.assertEqual(eldc.detect("Bonjour"), "fra")

    def test_iso639_2t_details_language(self):
        eldc.set_scheme("iso639-2t")
        result = eldc.detect_details("Hello world")
        self.assertEqual(result.language, "eng")

    def test_iso639_2t_details_score_keys_are_three_letter(self):
        eldc.set_scheme("iso639-2t")
        result = eldc.detect_details("Hello world")
        self.assertTrue(all(len(k) == 3 for k in result.scores))

    def test_reset_to_iso639_1_detect(self):
        eldc.set_scheme("iso639-2t")
        eldc.set_scheme("iso639-1")
        self.assertEqual(eldc.detect("Bonjour"), "fr")

    def test_reset_to_iso639_1_details_score_keys_are_two_letter(self):
        eldc.set_scheme("iso639-2t")
        eldc.set_scheme("iso639-1")
        result = eldc.detect_details("Bonjour le monde")
        self.assertTrue(all(len(k) == 2 for k in result.scores))


class TestEldcConstants(_EldcTestCase):
    """Tests for module-level constants."""

    def test_max_languages_is_positive_int(self):
        self.assertIsInstance(eldc.MAX_LANGUAGES, int)
        self.assertGreater(eldc.MAX_LANGUAGES, 0)

    def test_max_scores_is_at_least_20(self):
        self.assertIsInstance(eldc.MAX_SCORES, int)
        self.assertGreaterEqual(eldc.MAX_SCORES, 20)

    def test_languages_is_list_of_two_letter_codes(self):
        self.assertIsInstance(eldc.LANGUAGES, list)
        self.assertGreater(len(eldc.LANGUAGES), 0)
        self.assertTrue(all(len(c) == 2 for c in eldc.LANGUAGES))

    def test_languages_iso2t_is_list_of_three_letter_codes(self):
        self.assertIsInstance(eldc.LANGUAGES_ISO2T, list)
        self.assertGreater(len(eldc.LANGUAGES_ISO2T), 0)
        self.assertTrue(all(len(c) == 3 for c in eldc.LANGUAGES_ISO2T))

    def test_languages_and_iso2t_same_length(self):
        self.assertEqual(len(eldc.LANGUAGES), len(eldc.LANGUAGES_ISO2T))

    def test_languages_count_matches_max_languages(self):
        self.assertEqual(len(eldc.LANGUAGES), eldc.MAX_LANGUAGES)


class TestEldcInstance(_EldcTestCase):
    """Tests for eldc.instance() — isolated configuration.

    Verifies that instances are fully isolated from the global config
    and from each other, for scheme, language filter, and score count.
    """

    def test_instance_detect_returns_str(self):
        d = eldc.instance()
        self.assertIsInstance(d.detect("Bonjour le monde"), str)

    def test_instance_detect_correct_language(self):
        d = eldc.instance()
        self.assertEqual(d.detect("Bonjour le monde"), "fr")

    def test_instance_detect_und_for_garbage(self):
        d = eldc.instance()
        self.assertEqual(d.detect("12345 !@#"), "und")

    def test_instance_scheme_independent_of_global(self):
        eldc.set_scheme("iso639-2t")           # change global
        d = eldc.instance()
        d.set_scheme("iso639-1")
        # Instance overridden to iso639-1; global is still iso639-2t
        self.assertEqual(d.detect("Bonjour"), "fr")
        self.assertEqual(eldc.detect("Bonjour"), "fra")

    def test_global_scheme_independent_of_instance(self):
        d = eldc.instance()
        d.set_scheme("iso639-2t")
        # Global must be unaffected
        self.assertEqual(eldc.detect("Bonjour"), "fr")
        self.assertEqual(d.detect("Bonjour"), "fra")

    def test_global_change_after_creation_does_not_affect_instance(self):
        d = eldc.instance()
        eldc.set_scheme("iso639-2t")           # global changes after instance created
        # Instance was built with factory defaults (iso639-1) — must be unaffected
        self.assertEqual(d.detect("Bonjour"), "fr")

    def test_instance_language_filter_independent_of_global(self):
        eldc.set_languages(["en", "fr"])       # global filtered
        d = eldc.instance()
        d.set_languages(["de", "es"])
        self.assertEqual(d.detect("Hola mundo"), "es")
        self.assertNotEqual(eldc.detect("Hola mundo"), "es")

    def test_global_filter_independent_of_instance(self):
        d = eldc.instance()
        d.set_languages(["en", "fr"])
        # Global must be unaffected — es should still be detectable
        self.assertEqual(eldc.detect("Hola mundo"), "es")

    def test_instance_scores_independent_of_global(self):
        eldc.set_scores(3)
        d = eldc.instance()
        d.set_scores(7)
        r_inst = d.detect_details("Bonjour le monde")
        self.assertEqual(len(r_inst.scores), 7)
        r_global = eldc.detect_details("Bonjour le monde")
        self.assertEqual(len(r_global.scores), 3)

    def test_two_instances_are_independent(self):
        d1 = eldc.instance()
        d2 = eldc.instance()
        d1.set_scheme("iso639-2t")
        self.assertEqual(d1.detect("Bonjour"), "fra")
        self.assertEqual(d2.detect("Bonjour"), "fr")

    def test_instance_detect_details_language(self):
        d = eldc.instance()
        result = d.detect_details("Bonjour le monde")
        self.assertEqual(result.language, "fr")

    def test_instance_detect_details_reliable(self):
        d = eldc.instance()
        result = d.detect_details("Bonjour le monde")
        self.assertTrue(result.reliable)

    def test_instance_detect_details_scores_dict(self):
        d = eldc.instance()
        result = d.detect_details("Bonjour le monde")
        self.assertIn("fr", result.scores)

    def test_instance_detect_details_scheme(self):
        d = eldc.instance()
        d.set_scheme("iso639-2t")
        result = d.detect_details("Bonjour le monde")
        self.assertEqual(result.language, "fra")
        self.assertTrue(all(len(k) == 3 for k in result.scores))

    def test_instance_detect_details_filter(self):
        d = eldc.instance()
        d.set_languages(["en", "fr"])
        result = d.detect_details("Hola mundo")
        self.assertNotEqual(result.language, "es")

    def test_instance_detect_details_scores_count(self):
        d = eldc.instance()
        d.set_scores(5)
        result = d.detect_details("Bonjour le monde")
        self.assertEqual(len(result.scores), 5)

    def test_combined_scheme_and_filter_on_instance(self):
        d = eldc.instance()
        d.set_scheme("iso639-2t")
        d.set_languages(["fr", "de"])
        self.assertEqual(d.detect("Bonjour"), "fra")
        self.assertNotEqual(d.detect("Hola"), "spa")
        # Global must remain fully unaffected
        self.assertEqual(eldc.detect("Bonjour"), "fr")
        self.assertEqual(eldc.detect("Hola"), "es")

    def test_instance_detect_mt_returns_str(self):
        d = eldc.instance()
        self.assertIsInstance(d.detect_mt("Bonjour le monde"), str)

    def test_instance_detect_mt_correct_language(self):
        d = eldc.instance()
        self.assertEqual(d.detect_mt("Bonjour le monde"), "fr")

    def test_instance_detect_mt_scheme(self):
        d = eldc.instance()
        d.set_scheme("iso639-2t")
        self.assertEqual(d.detect_mt("Bonjour"), "fra")


class TestEldcThreadSafety(_EldcTestCase):
    """Tests for concurrent use of detect() and detect_details()."""

    def test_detect_concurrent_no_exceptions(self):
        texts = [
            "Hello world", "Bonjour le monde", "Hola mundo",
            "Ciao mondo", "Привет мир", "مرحبا بالعالم", "你好世界",
        ]
        with concurrent.futures.ThreadPoolExecutor(max_workers=4) as pool:
            results = list(pool.map(eldc.detect, texts))
        self.assertEqual(len(results), len(texts))
        self.assertTrue(all(isinstance(r, str) for r in results))

    def test_detect_details_concurrent_no_exceptions(self):
        texts = ["Hello world", "Bonjour le monde", "Hola mundo", "Ciao mondo"]
        with concurrent.futures.ThreadPoolExecutor(max_workers=4) as pool:
            results = list(pool.map(eldc.detect_details, texts))
        self.assertEqual(len(results), len(texts))
        self.assertTrue(all(r.language is not None for r in results))


if __name__ == "__main__":
    unittest.main(testRunner=_SpacedRunner(verbosity=2))
