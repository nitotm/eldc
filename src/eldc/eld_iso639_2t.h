/*
 * eld_iso639_2t.h — ISO 639-2/T (terminology) three-letter language codes
 *
 * Maps in the same index order as ELD_langCodes[] in large_db_new.h.
 * Enable with:  eld_detect --scheme iso639-2t
 *
 * Where ISO 639-2 T and B codes differ, the T (terminology) form is used:
 *   ces (not cze), deu (not ger), ell (not gre), eus (not baq),
 *   fas (not per), fra (not fre), hye (not arm), isl (not ice),
 *   kat (not geo), nld (not dut), ron (not rum), slk (not slo),
 *   sqi (not alb), zho (not chi).
 *
 * "und" (undetermined) is the ISO 639-2 standard code for unknown language,
 * matching the detector's NULL-result output.
 */

#ifndef ELD_ISO639_2T_H
#define ELD_ISO639_2T_H

static const char * const ELD_ISO639_2T[] = {
/*  0  am  */ "amh",
/*  1  ar  */ "ara",
/*  2  az  */ "aze",
/*  3  be  */ "bel",
/*  4  bg  */ "bul",
/*  5  bn  */ "ben",
/*  6  ca  */ "cat",
/*  7  cs  */ "ces",
/*  8  da  */ "dan",
/*  9  de  */ "deu",
/* 10  el  */ "ell",
/* 11  en  */ "eng",
/* 12  es  */ "spa",
/* 13  et  */ "est",
/* 14  eu  */ "eus",
/* 15  fa  */ "fas",
/* 16  fi  */ "fin",
/* 17  fr  */ "fra",
/* 18  gu  */ "guj",
/* 19  he  */ "heb",
/* 20  hi  */ "hin",
/* 21  hr  */ "hrv",
/* 22  hu  */ "hun",
/* 23  hy  */ "hye",
/* 24  is  */ "isl",
/* 25  it  */ "ita",
/* 26  ja  */ "jpn",
/* 27  ka  */ "kat",
/* 28  kn  */ "kan",
/* 29  ko  */ "kor",
/* 30  ku  */ "kur",
/* 31  lo  */ "lao",
/* 32  lt  */ "lit",
/* 33  lv  */ "lav",
/* 34  ml  */ "mal",
/* 35  mr  */ "mar",
/* 36  ms  */ "msa",
/* 37  nl  */ "nld",
/* 38  no  */ "nor",
/* 39  or  */ "ori",
/* 40  pa  */ "pan",
/* 41  pl  */ "pol",
/* 42  pt  */ "por",
/* 43  ro  */ "ron",
/* 44  ru  */ "rus",
/* 45  sk  */ "slk",
/* 46  sl  */ "slv",
/* 47  sq  */ "sqi",
/* 48  sr  */ "srp",
/* 49  sv  */ "swe",
/* 50  ta  */ "tam",
/* 51  te  */ "tel",
/* 52  th  */ "tha",
/* 53  tl  */ "tgl",
/* 54  tr  */ "tur",
/* 55  uk  */ "ukr",
/* 56  ur  */ "urd",
/* 57  vi  */ "vie",
/* 58  yo  */ "yor",
/* 59  zh  */ "zho",
};

#endif /* ELD_ISO639_2T_H */
