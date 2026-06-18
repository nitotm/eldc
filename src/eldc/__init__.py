"""
eldc — Efficient Language Detector (C extension)
=================================================

Usage
-----
    import eldc

    eldc.init()
    
    eldc.detect("Bonjour le monde")   # 'fr'
"""

from ._eldc import (
    init,
    detect,
    detect_mt,
    detect_details,
    set_scores,
    set_languages,
    set_scheme,
    LANGUAGES,
    LANGUAGES_ISO2T,
    MAX_LANGUAGES,
    MAX_SCORES,
    Result,
)

__all__ = (
    "init",
    "detect",
    "detect_mt",
    "detect_details",
    "set_scores",
    "set_languages",
    "set_scheme",  
    "LANGUAGES",
    "LANGUAGES_ISO2T",
    "MAX_LANGUAGES",
    "MAX_SCORES",
    "Result",
)
