/*
 * _eldc_module.c — Python C extension for the Efficient Language Detector
 *
 * Two detection functions:
 *   eldc.detect(text)         Fast — returns str | None directly.
 *                             Thin wrapper around detect_ex(); no Python
 *                             object allocation beyond the returned string.
 *
 *   eldc.detect_details(text) Full — always computes reliability + optional
 *                             top-N scores.  Returns a Result object with
 *                             .language, .reliable, .scores.
 *
 * Configuration (call before detection; not thread-safe):
 *   eldc.set_languages("en,es,fr")   restrict detection to a subset
 *   eldc.set_scheme("iso639-2t")     output code scheme
 *   eldc.set_scores(5)               top-N scores in detect_details result
 *
 * The GIL is released during every C detection call, so multiple Python
 * threads can run detect() / detect_details() concurrently without contention.
 */

#define PY_SSIZE_T_CLEAN
#include <Python.h>
#include <stddef.h>
#if PY_VERSION_HEX < 0x030C0000
#  include <structmember.h>
#  define _ELD_T_OBJ  T_OBJECT_EX
#  define _ELD_RO     READONLY
#else
#  define _ELD_T_OBJ  Py_T_OBJECT_EX
#  define _ELD_RO     Py_READONLY
#endif
#include <math.h>

#include "eld_core.c"


/* ═══════════════════════════════════════════════════════════════════════════
 * Module-level configuration
 * ═══════════════════════════════════════════════════════════════════════════ */
static uint64_t _cfg_lang_mask = (uint64_t)-1;
static int      _cfg_subset    = 0;
static int      _cfg_scheme    = 0;   /* 0=iso639-1, 1=iso639-2t */
static int      _cfg_scores    = 3; /* min 1; N=top-N in detect_details */
static int      _cfg_inited    = 0; /* 1 once init() has been called; */

/* Pre-interned PyUnicode objects for all language codes in both schemes.
 *   _lang_str[0][i]  ISO 639-1 code  e.g. "fr"  (2-letter)
 *   _lang_str[1][i]  ISO 639-2/T code e.g. "fra" (3-letter)
 * The first dimension [2] is the NUMBER OF SCHEMES — not the code length.
 * These are reused as pre-built dict keys in detect_details(), avoiding a
 * fresh PyUnicode allocation on every call. */
static PyObject *_lang_str[2][MAX_LANGUAGES];

/* ═══════════════════════════════════════════════════════════════════════════
 * EldcResult — Python type returned by detect_details()
 *   .language  str | None
 *   .scores    dict[str, float]  always has at least one entry
 *   .reliable  bool              always True or False (never None)
 * ═══════════════════════════════════════════════════════════════════════════ */
typedef struct {
    PyObject_HEAD
    PyObject *language;
    PyObject *scores;
    PyObject *reliable;
} EldcResult;

static void EldcResult_dealloc(EldcResult *self)
{
    Py_CLEAR(self->language);
    Py_CLEAR(self->scores);
    Py_CLEAR(self->reliable);
    Py_TYPE(self)->tp_free((PyObject *)self);
}

static PyObject *EldcResult_repr(EldcResult *self)
{
    return PyUnicode_FromFormat("Result(language=%R, reliable=%R, scores=%R)",
                                self->language, self->reliable, self->scores);
}

static PyObject *EldcResult_str(EldcResult *self)
{
    if (self->language)
        return PyUnicode_FromFormat("%S", self->language);
    return PyUnicode_FromString("und");  /* safety fallback — should never reach here */
}

static int EldcResult_bool(EldcResult *self)
{
    if (!self->language) return 0;
    const char *s = PyUnicode_AsUTF8(self->language);
    return s && strcmp(s, "und") != 0;
}

static PyObject *EldcResult_richcompare(EldcResult *self, PyObject *other, int op)
{
    if (op != Py_EQ && op != Py_NE) Py_RETURN_NOTIMPLEMENTED;
    int eq;
    if (PyUnicode_Check(other)) {
        if (!self->language || self->language == Py_None) eq = 0;
        else {
            const char *s = PyUnicode_AsUTF8(self->language);
            eq = s ? (PyUnicode_CompareWithASCIIString(other, s) == 0) : 0;
        }
    } else if (Py_TYPE(other) == Py_TYPE(self)) {
        EldcResult *o = (EldcResult *)other;
        const char *a = self->language ? PyUnicode_AsUTF8(self->language) : NULL;
        const char *b = o->language    ? PyUnicode_AsUTF8(o->language)    : NULL;
        eq = (a && b) ? (strcmp(a, b) == 0) : (a == b);
    } else {
        Py_RETURN_NOTIMPLEMENTED;
    }
    return PyBool_FromLong(op == Py_EQ ? eq : !eq);
}

static PyNumberMethods EldcResult_as_number = {
    .nb_bool = (inquiry)EldcResult_bool,
};

static PyMemberDef EldcResult_members[] = {
    {"language", _ELD_T_OBJ, offsetof(EldcResult, language), _ELD_RO,
     "Top detected language code (str); 'und' if undetermined."},
    {"scores",   _ELD_T_OBJ, offsetof(EldcResult, scores),   _ELD_RO,
     "Dict[str, float] normalised [0,1] scores, descending. "
     "Always has at least one entry (set_scores() is clamped to a minimum of 1)."},
    {"reliable", _ELD_T_OBJ, offsetof(EldcResult, reliable), _ELD_RO,
     "True if detection is reliable, False if uncertain. "
     "Reliable = score >= 85% of language average AND gap to runner-up > 2 pp "
     "AND at least 3 n-grams matched."},
    {NULL}
};

static PyTypeObject EldcResult_Type = {
    PyVarObject_HEAD_INIT(NULL, 0)
    .tp_name        = "eldc.Result",
    .tp_basicsize   = sizeof(EldcResult),
    .tp_dealloc     = (destructor)EldcResult_dealloc,
    .tp_repr        = (reprfunc)EldcResult_repr,
    .tp_str         = (reprfunc)EldcResult_str,
    .tp_as_number   = &EldcResult_as_number,
    .tp_richcompare = (richcmpfunc)EldcResult_richcompare,
    .tp_flags       = Py_TPFLAGS_DEFAULT,
    .tp_doc         = "Language detection result (from detect_details). "
                      "Attributes: .language, .reliable, .scores",
    .tp_members     = EldcResult_members,
};

/* Build a new EldcResult.
 *   scores_dict : stolen reference; NULL → empty dict allocated internally.
 *   reliable    : 0 or 1 → stored as Py_False / Py_True. */
static PyObject *make_result(const char *language, PyObject *scores_dict, int reliable)
{
    EldcResult *r = PyObject_New(EldcResult, &EldcResult_Type);
    if (!r) { Py_XDECREF(scores_dict); return NULL; }

    r->language = PyUnicode_FromString(language ? language : "und");
    r->scores   = scores_dict ? scores_dict : PyDict_New();
    r->reliable = PyBool_FromLong(reliable);   /* new ref: Py_True or Py_False */

    if (!r->language || !r->scores || !r->reliable) { Py_DECREF(r); return NULL; }
    return (PyObject *)r;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * detect(text: str) -> str
 *
 * Fastest possible path: calls detect_ex() directly — no EldConfig struct,
 * no EldResult, no dict construction.
 *
 * Returns the language code (e.g. "fr") or "und" if the text could not be
 * classified.  Always returns a string — never None — so callers can rely on
 * a consistent, non-None return value and still use a falsy check via
 * result != "und".
 *
 * Language filter (set_languages) and output scheme (set_scheme) are applied
 * via the engine's global state, which the setters keep in sync so this path
 * always produces correct, configured output.
 *
 * Raises RuntimeError if eldc.init() has not been called.
 * ═══════════════════════════════════════════════════════════════════════════ */
static PyObject *py_detect(PyObject *self, PyObject *obj)
{
    if (__builtin_expect(!ht, 0)) {
        PyErr_SetString(PyExc_RuntimeError,
            "eldc not initialized — call eldc.init() before detection");
        return NULL;
    }

    Py_ssize_t len;
    const char *text = PyUnicode_AsUTF8AndSize(obj, &len);
    if (!text) return NULL;

    const char *language;

    language = detect_ex(text, NULL, NULL);

    return PyUnicode_FromString(language ? language : "und");
}
/* Same as py_detect, but allows for for multi-threaded parallel Python threads */
static PyObject *py_detect_mt(PyObject *self, PyObject *obj)
{
    if (__builtin_expect(!ht, 0)) {
        PyErr_SetString(PyExc_RuntimeError,
            "eldc not initialized — call eldc.init() before detection");
        return NULL;
    }

    Py_ssize_t len;
    const char *text = PyUnicode_AsUTF8AndSize(obj, &len);
    if (!text) return NULL;

    const char *language;

    Py_BEGIN_ALLOW_THREADS
    language = detect_ex(text, NULL, NULL);
    Py_END_ALLOW_THREADS

    return PyUnicode_FromString(language ? language : "und");
}

/* ═══════════════════════════════════════════════════════════════════════════
 * detect_details(text: str) -> Result
 *
 * Always computes reliability.  Optionally computes top-N scores (set by
 * set_scores).  When set_scores(1) is in effect, the engine still normalises
 * 2 entries internally for the gap check; only 1 appears in .scores.
 *
 * Raises RuntimeError if eldc.init() has not been called.
 * ═══════════════════════════════════════════════════════════════════════════ */
static PyObject *py_detect_details(PyObject *self, PyObject *obj)
{
    if (__builtin_expect(!ht, 0)) {
        PyErr_SetString(PyExc_RuntimeError,
            "eldc not initialized — call eldc.init() before detection");
        return NULL;
    }

    Py_ssize_t len;
    const char *text = PyUnicode_AsUTF8AndSize(obj, &len);
    if (!text) return NULL;   // obj was not a str, or UTF-8 conversion failed

    EldConfig cfg;
    cfg.scores    = _cfg_scores;
    cfg.reliable  = 1;             /* always on in detect_details */
    cfg.scheme    = _cfg_scheme;
    cfg.lang_mask = _cfg_lang_mask;
    cfg.subset    = _cfg_subset;

    EldResult result = {0};
    Py_BEGIN_ALLOW_THREADS
    eld_process_line(text, &cfg, &result);
    Py_END_ALLOW_THREADS

    PyObject *dct = NULL;
    if (_cfg_scores > 0) {
        dct = PyDict_New();
        if (!dct) return NULL;
        for (int i = 0; i < result.n_entries; i++) {
            double    v    = (double)(roundf(result.entries[i].ns * 10000.0f) / 10000.0f);
            PyObject *k    = _lang_str[_cfg_scheme][result.entries[i].idx];
            PyObject *vobj = PyFloat_FromDouble(v);
            if (!vobj || PyDict_SetItem(dct, k, vobj) < 0) {
                Py_XDECREF(vobj); Py_DECREF(dct); return NULL;
            }
            Py_DECREF(vobj);
        }
    }

    return make_result(result.language, dct, result.reliable);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Configuration setters
 * ═══════════════════════════════════════════════════════════════════════════ */

static PyObject *py_set_scores(PyObject *module, PyObject *args)
{
    PyObject *arg;
    if (!PyArg_ParseTuple(args, "O", &arg)) return NULL;

    long n;
    if (PyBool_Check(arg)) {
        /* True  → all scores (backward compat shorthand).
         * False → minimum 1 (detect_details always returns at least one score). */
        n = (arg == Py_True) ? ELD_MAX_SCORES : 1;
    } else if (PyLong_Check(arg)) {
        n = PyLong_AsLong(arg);
    } else {
        PyErr_SetString(PyExc_TypeError, "set_scores expects int or bool");
        return NULL;
    }
    /* Clamp: detect_details always returns scores, so minimum is 1. */
    if (n < 1) n = 1;
    if (n > ELD_MAX_SCORES) n = ELD_MAX_SCORES;
    _cfg_scores = (int)n;
    Py_RETURN_NONE;
}

/* set_languages — accepts str ("en,fr,es") or list/tuple (["en","fr","es"]).
 * Returns a list[str] of the ISO 639-1 codes that were actually matched.
 *   []           → language filter cleared, all languages active
 *   ["fr","en"]  → only those two matched; unrecognised codes produce UserWarning
 * Raises ValueError if a non-empty input matches nothing at all.
 * Syncs both the module config and the detect_ex globals. */
static PyObject *py_set_languages(PyObject *module, PyObject *args)
{
    PyObject *arg;
    if (!PyArg_ParseTuple(args, "O", &arg)) return NULL;

    /* ── Flatten input into a C buffer of comma-separated codes ─────────── */
    char flat[2048]; flat[0] = '\0';
    size_t pos = 0;

    if (PyUnicode_Check(arg)) {
        const char *s = PyUnicode_AsUTF8(arg);
        if (!s) return NULL;
        snprintf(flat, sizeof flat, "%s", s);
    } else if (PyList_Check(arg) || PyTuple_Check(arg)) {
        Py_ssize_t len = PySequence_Size(arg);
        if (len < 0) return NULL;
        for (Py_ssize_t i = 0; i < len; i++) {
            PyObject *item = PySequence_GetItem(arg, i);
            if (!item) return NULL;
            if (!PyUnicode_Check(item)) {
                Py_DECREF(item);
                PyErr_SetString(PyExc_TypeError,
                    "set_languages list items must be str");
                return NULL;
            }
            const char *s = PyUnicode_AsUTF8(item);
            Py_DECREF(item);
            if (!s) return NULL;
            if (pos > 0 && pos < sizeof flat - 2) flat[pos++] = ',';
            size_t slen = strlen(s);
            if (pos + slen >= sizeof flat) break;  /* silently truncate on absurd input */
            memcpy(flat + pos, s, slen);
            pos += slen;
        }
        flat[pos] = '\0';
    } else {
        PyErr_SetString(PyExc_TypeError,
            "set_languages expects str or list[str]");
        return NULL;
    }

    /* ── Empty input → reset to all languages ────────────────────────────── */
    if (!flat[0]) {
        _cfg_lang_mask = (uint64_t)-1; _cfg_subset = 0;
        g_lang_mask    = (uint64_t)-1; g_subset    = 0;
        return PyList_New(0);   /* empty list signals "all languages active" */
    }

    /* ── Parse individual codes, build mask, collect matched codes ────────── */
    uint64_t mask    = 0;
    PyObject *matched = PyList_New(0);
    if (!matched) return NULL;

    char buf[2048];
    snprintf(buf, sizeof buf, "%s", flat);
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
                /* Append the canonical ISO 639-1 code using the pre-interned object. */
                if (PyList_Append(matched, _lang_str[0][i]) < 0) {
                    Py_DECREF(matched); return NULL;
                }
                break;
            }
        }
        if (!found) {
            /* Python-level UserWarning — integrates with warnings filters. */
            if (PyErr_WarnFormat(PyExc_UserWarning, 1,
                    "eldc: unknown language code '%s' (ignored)", tok) < 0) {
                Py_DECREF(matched); return NULL;
            }
        }
    }

    if (!mask) {
        PyErr_SetString(PyExc_ValueError,
            "No valid language codes found. "
            "Check eldc.LANGUAGES for supported codes.");
        Py_DECREF(matched);
        return NULL;
    }

    _cfg_lang_mask = mask; _cfg_subset = 1;
    g_lang_mask    = mask; g_subset    = 1;
    return matched;
}

/* set_scheme syncs g_scheme so detect() returns the configured scheme. */
static PyObject *py_set_scheme(PyObject *module, PyObject *args)
{
    const char *scheme;
    if (!PyArg_ParseTuple(args, "s", &scheme)) return NULL;

    if (!strcmp(scheme,"iso639-1")||!strcmp(scheme,"iso639_1")) {
        _cfg_scheme = 0; g_scheme = SCHEME_ISO639_1;
    } else if (!strcmp(scheme,"iso639-2t")||!strcmp(scheme,"iso639_2t")) {
        _cfg_scheme = 1; g_scheme = SCHEME_ISO639_2T;
    } else {
        PyErr_Format(PyExc_ValueError,
            "Unknown scheme '%s'; use 'iso639-1' or 'iso639-2t'", scheme);
        return NULL;
    }
    Py_RETURN_NONE;
}


/* init() / reinit(): same operation, init() is the primary name.
 * Calling init() again with a different ht_bits is always safe — it frees
 * the old table and allocates a fresh one.  Do this before any detection. */
static PyObject *py_init(PyObject *module, PyObject *Py_UNUSED(ignored))
{
    init_detector();
	 _cfg_inited = 1;
    Py_RETURN_NONE;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Module setup
 * ═══════════════════════════════════════════════════════════════════════════ */
#define STRINGIFY2(x) #x
#define STRINGIFY(x)  STRINGIFY2(x)

static PyMethodDef eldc_methods[] = {
     {"detect", (PyCFunction)py_detect, METH_O,
     "detect(text: str) -> str\n"
     "Fastest detection: returns the language code (e.g. 'fr') or 'und'.\n"
     "For single thread use.\n"
     "Always returns a string — never None.\n"
     "set_languages() and set_scheme() are respected.\n"
     "Raises RuntimeError if eldc.init() has not been called."},

	 {"detect_mt", (PyCFunction)py_detect_mt, METH_O,
	 "detect_mt(text: str) -> str\n"
	 "Multi-threaded version of detect().\n"
	 "Releases the Python GIL, allowing concurrent detection from multiple threads.\n"
	 "Returns the language code (e.g. 'fr') or 'und'.\n"
	 "Always returns a string — never None.\n"
	 "set_languages() and set_scheme() are respected.\n"
	 "Raises RuntimeError if eldc.init() has not been called."},
	  
    {"detect_details", (PyCFunction)py_detect_details, METH_O,
     "detect_details(text: str) -> Result\n"
     "Multi-threaded by default.\n"
     "Detect with reliability flag and optional top-N scores.\n"
     "Returns Result(.language, .reliable, .scores).\n"
     ".language is always a str: a language code or 'und' if undetermined.\n"
     "Reliability is always computed regardless of set_scores().\n"
     "Raises RuntimeError if eldc.init() has not been called."},

    {"set_scores",    py_set_scores,    METH_VARARGS,
     "set_scores(n: int | bool) -> None\n"
     "Set how many top scores detect_details() returns (minimum 1).\n"
     "  N     — top-N scores, clamped to 1.." STRINGIFY(ELD_MAX_SCORES) "\n"
     "  True  — all scores (" STRINGIFY(ELD_MAX_SCORES) ", backward compat)\n"
     "  False — clamps to 1 (detect_details always returns at least one score)\n"
     "Default: 3."},

    {"set_languages", py_set_languages, METH_VARARGS,
     "set_languages(codes: str | list[str]) -> list[str]\n"
     "Restrict detection to a subset of languages.\n"
     "Input: comma-separated str (\"en,fr,de\") or list ([\"en\",\"fr\",\"de\"]).\n"
     "       Accepts ISO 639-1 and ISO 639-2/T codes, or mixed.\n"
     "       Pass '' or [] to reset to all languages.\n"
     "Returns: list of ISO 639-1 codes that were matched.\n"
     "         Empty list means all languages are active (filter cleared).\n"
     "Unrecognised codes produce a UserWarning and are skipped."},

    {"set_scheme",    py_set_scheme,    METH_VARARGS,
     "set_scheme(scheme: str) -> None\n"
     "Output scheme: 'iso639-1' (default, 'en') or 'iso639-2t' ('eng')."}, 

    {"init",          py_init,          METH_NOARGS,
     "init() -> None\n"
     "Load the n-gram database and build the hash table."},

    {NULL, NULL, 0, NULL}
};

static struct PyModuleDef eldc_module_def = {
    PyModuleDef_HEAD_INIT, "_eldc", NULL, -1, eldc_methods
};

PyMODINIT_FUNC PyInit__eldc(void)
{
    if (PyType_Ready(&EldcResult_Type) < 0) return NULL;

    /* No init_detector() here: the hash table is NOT loaded on import.
     * Call eldc.init()
     * This keeps import cheap — no 16 MB allocation just for importing. */

    PyObject *m = PyModule_Create(&eldc_module_def);
    if (!m) return NULL;

    Py_INCREF(&EldcResult_Type);
    if (PyModule_AddObject(m, "Result", (PyObject *)&EldcResult_Type) < 0) {
        Py_DECREF(&EldcResult_Type); Py_DECREF(m); return NULL;
    }

    PyObject *langs = PyList_New(MAX_LANGUAGES);
    if (!langs) { Py_DECREF(m); return NULL; }
    for (int i = 0; i < MAX_LANGUAGES; i++) {
        PyObject *s = PyUnicode_FromString(ELD_langCodes[i]);
        if (!s) { Py_DECREF(langs); Py_DECREF(m); return NULL; }
        PyUnicode_InternInPlace(&s);
        _lang_str[0][i] = s;
        Py_INCREF(s);
        PyList_SET_ITEM(langs, i, s);
    }
    if (PyModule_AddObject(m, "LANGUAGES", langs) < 0) {
        Py_DECREF(langs); Py_DECREF(m); return NULL;
    }

    PyObject *langs2 = PyList_New(MAX_LANGUAGES);
    if (!langs2) { Py_DECREF(m); return NULL; }
    for (int i = 0; i < MAX_LANGUAGES; i++) {
        PyObject *s = PyUnicode_FromString(ELD_ISO639_2T[i]);
        if (!s) { Py_DECREF(langs2); Py_DECREF(m); return NULL; }
        PyUnicode_InternInPlace(&s);
        _lang_str[1][i] = s;
        Py_INCREF(s);
        PyList_SET_ITEM(langs2, i, s);
    }
    if (PyModule_AddObject(m, "LANGUAGES_ISO2T", langs2) < 0) {
        Py_DECREF(langs2); Py_DECREF(m); return NULL;
    }

    if (PyModule_AddIntConstant(m, "MAX_LANGUAGES", MAX_LANGUAGES) < 0 ||
        PyModule_AddIntConstant(m, "MAX_SCORES",    ELD_MAX_SCORES) < 0) {
        Py_DECREF(m); return NULL;
    }

    return m;
}
