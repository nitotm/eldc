"""
setup.py for the eldc Python package.

The extension is a unity build: _eldc_module.c #includes eld_core.c directly,
giving the compiler full visibility for inlining and optimisation.

Database
--------
src/eldc/large_db.h holds the n-gram database compiled into the extension.

Environment variables
---------------------
ELD_HT_BITS        Hash table size as log2 of slot count. Default: 21 (32 MB).
                   Use 22 (64 MB) for minimal speedup.
ELD_EXTRA_CFLAGS   Extra flags appended to the compiler line.
"""

import os
import sys
from setuptools import Extension, find_packages, setup

ht_bits  = int(os.environ.get("ELD_HT_BITS", "21"))
extra_cf = os.environ.get("ELD_EXTRA_CFLAGS", "").split()

if sys.platform == "win32":
    base_flags = ["/O2", "/W3", f"/DELD_HT_BITS={ht_bits}"]
    link_flags: list = []
else:
    # -std=c11 is intentionally omitted: it prevents GCC from statically
    # proving memcpy(&k, ngram, 8) is safe inside detect_ex(), leaving a
    # _FORTIFY_SOURCE runtime bounds-check wrapper in the hot hash-table
    # scan loop.  GCC's default (gnu17) eliminates the wrapper and runs
    # ~7% faster.  _POSIX_C_SOURCE is set explicitly in _eldc_module.c
    # so POSIX declarations remain correct without any -std= flag.
    base_flags = ["-O3", "-Wall", f"-DELD_HT_BITS={ht_bits}"]
    link_flags = ["-lm"]

ext = Extension(
    name="eldc._eldc",
    sources=["src/eldc/_eldc_module.c"],
    include_dirs=["src/eldc"],
    extra_compile_args=base_flags + extra_cf,
    extra_link_args=link_flags,
    language="c",
)

setup(
    # Explicitly declare the Python package so __init__.py is always
    # included in the wheel — critical when setup.py is present alongside
    # pyproject.toml, because setup() takes precedence over pyproject
    # package-discovery when both are present.
    packages=find_packages(where="src"),
    package_dir={"": "src"},
    ext_modules=[ext],
)
