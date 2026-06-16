/*
 * eld_cli.c — helpers shared by the eld.c / eld_mt.c command-line front-ends.
 *
 * #include this AFTER "eld_core.c" (it relies on types, macros and headers
 * eld_core.c already brought in: EldResult, EldConfig, ELD_EMIT_BUFSIZE,
 * MAX_LANGUAGES, ELD_langCodes/ELD_ISO639_2T, ELD_MAX_SCORES, <math.h>,
 * <time.h>, and the platform timing headers <windows.h> / <unistd.h>).
 *
 * Everything here is CLI-only: eldc_lib.c and _eldc_module.c also do
 * #include "eld_core.c" (unity build) but never call any of these, so
 * keeping them in eld_core.c produced a handful of -Wunused-function
 * warnings for the library/module builds. Splitting them out here keeps
 * those builds warning-clean without changing eld.c / eld_mt.c at all.
 */
#ifndef ELD_CLI_C_INCLUDED
#define ELD_CLI_C_INCLUDED

/* ═══════════════════════════════════════════════════════════════════════════
 * get_time_seconds() — wall-clock timing for -v/--verbose
 *
 * Only eld.c uses this (eld_mt.c calls clock_gettime() directly for its
 * timing, since it needs CLOCK_MONOTONIC for thread-pool-wide totals
 * regardless of platform). The unused-function attribute (GCC/Clang) keeps
 * eld_mt.c's build warning-clean too, without a second copy of this function.
 * ═══════════════════════════════════════════════════════════════════════════ */
#if defined(__GNUC__) || defined(__clang__)
__attribute__((unused))
#endif
#ifdef _WIN32
static double get_time_seconds(void) {
    LARGE_INTEGER count, freq;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&count);
    return (double)count.QuadPart / (double)freq.QuadPart;
}
#else
static double get_time_seconds(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}
#endif

/* ═══════════════════════════════════════════════════════════════════════════
 * parse_lang_mask() — used by -l/--languages
 * ═══════════════════════════════════════════════════════════════════════════ */
static uint64_t parse_lang_mask(const char *str)
{
    uint64_t mask = 0;
    char buf[512]; snprintf(buf, sizeof buf, "%s", str);
    char *saveptr = NULL;
    for (char *tok = strtok_r(buf, ",", &saveptr); tok; tok = strtok_r(NULL, ",", &saveptr)) {
        while (*tok==' ') tok++;
        char *e=tok+strlen(tok)-1; while(e>tok&&*e==' ') *e--='\0';
        if (!*tok) continue;
        int found = 0;
        for (int i = 0; i < MAX_LANGUAGES; i++)
            if (!strcmp(tok,ELD_langCodes[i])||!strcmp(tok,ELD_ISO639_2T[i]))
                { mask |= 1ULL<<i; found=1; break; }
        if (!found)
            fprintf(stderr,"Warning: unknown language '%s' (ignored)\n",tok);
    }
    return mask;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * eld_emit_result()
 *
 * Format one EldResult into buf.  Returns bytes written.
 * buf must have at least ELD_EMIT_BUFSIZE bytes available.
 *
 * Output formats:
 *   cfg.scores > 0              {"language":"fr","reliable":true,"scores":{"fr":0.9234,...}}\n
 *   cfg.scores == 0, reliable   {"language":"fr","reliable":true}\n
 *   plain                       fr\n
 *
 * For all cases NULL language is rendered as "und".
 * ═══════════════════════════════════════════════════════════════════════════ */
static size_t eld_emit_result(char *buf, const EldResult *r, const EldConfig *cfg)
{
    char *p = buf;
    const char *l = r->language ? r->language : "und";

#define _ES(s)  do { const char *_s=(s); while (*_s) *p++=*_s++; } while (0)
#define _EC(ch) (*p++ = (ch))

    if (cfg->scores > 0) {
        _ES("{\"language\":\""); _ES(l); _EC('"');
        if (cfg->reliable) {
            _ES(",\"reliable\":");
            _ES(r->reliable ? "true" : "false");
        }
        _ES(",\"scores\":{");
        for (int i = 0; i < r->n_entries; i++) {
            if (i) _EC(',');
            _EC('"');
            _ES(cfg->scheme ? ELD_ISO639_2T[r->entries[i].idx]
                            : ELD_langCodes[r->entries[i].idx]);
            _EC('"'); _EC(':');
            int v = (int)roundf(r->entries[i].ns * 10000.0f);
            if (v >= 10000) { _ES("1.0000"); }
            else {
                _EC('0'); _EC('.');
                _EC((char)('0' + v / 1000));
                _EC((char)('0' + v / 100 % 10));
                _EC((char)('0' + v / 10  % 10));
                _EC((char)('0' + v       % 10));
            }
        }
        _ES("}}\n");
    } else if (cfg->reliable) {
        _ES("{\"language\":\""); _ES(l); _ES("\",\"reliable\":");
        _ES(r->reliable ? "true" : "false"); _ES("}\n");
    } else {
        _ES(l); _EC('\n');
    }

#undef _ES
#undef _EC

    return (size_t)(p - buf);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * eld_print_help() — unified help text for CLI front-ends
 *
 * show_threads != 0: include the -t / --threads option (eld_mt only).
 * ═══════════════════════════════════════════════════════════════════════════ */
static void eld_print_help(const char *prog, int show_threads)
{
    printf(
        "Usage: %s [options] [text]\n\n"
        "Options:\n"
        "  -h, --help              This message\n"
        "      --list-languages    Print all supported codes and exit\n"
        "  -v, --verbose           Loading info, timing, throughput\n"
        "  -l, --languages CODES   Restrict to a subset, e.g. -l \"es,en,de,fr\"\n"
        "                          Accepts ISO 639-1 or ISO 639-2/T codes.\n"
        "  -s, --scores [N]        Output compact JSON with top-N normalised [0,1] scores\n"
        "                          N must be 1..%d; omit N to get all %d.\n"
        "                          Example: {\"language\":\"en\",\"scores\":{\"en\":0.9234,...}}\n"
        "  -r, --reliable          Add \"reliable\" boolean to JSON output\n"
        "      --scheme NAME       iso639-1 (default) | iso639-2t\n"
        "%s"
        "\nModes:\n"
        "  %s [text]        Detect a single string (flags must come before text)\n"
        "  %s               Read from stdin; one result per line\n\n"
        "Examples:\n"
        "  %s \"Bonjour le monde\"\n"
        "  %s -l \"es,en,fr,de\" \"Hola mundo\"\n"
        "  %s --scores 3 --scheme iso639-2t \"Привет мир\"\n"
        "  %s --scores 5 --reliable \"Hello world\"\n"
        "  %s -v < corpus.txt > results.txt\n",
        prog,
        ELD_MAX_SCORES, ELD_MAX_SCORES,
        show_threads
            ? "  -t, --threads N         Number of worker threads (default: logical CPU count)\n"
            : "",
        prog, prog, prog, prog, prog, prog, prog);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * EldChunkReader — robust, chunk-based stdin line reader
 *
 * Reads stdin in large fread() calls for I/O efficiency.  Correctly handles:
 *   - Leftover data at chunk boundaries (incomplete lines)
 *   - CRLF (\r\n) and LF (\n) line endings, mixed in the same file
 *   - Files with no trailing newline on the last line
 *   - Lines of any length (the buffer doubles if a single line exceeds it)
 *
 * The caller owns the output pointers only until the next eld_cr_next() call;
 * each call may memmove() or realloc() the underlying buffer.
 *
 * Typical usage (single-threaded):
 *   EldChunkReader cr;
 *   char *ptrs[4096];
 *   if (eld_cr_init(&cr) < 0) { perror("malloc"); exit(1); }
 *   int n;
 *   while ((n = eld_cr_next(&cr, stdin, ptrs, 4096)) > 0)
 *       for (int i = 0; i < n; i++) process(ptrs[i]);
 *   if (n < 0) { perror("realloc"); exit(1); }   // out of memory
 *   eld_cr_free(&cr);
 *
 * Typical usage (multi-threaded, larger batch):
 *   char *ptrs[65536]; EldResult results[65536];
 *   while ((n = eld_cr_next(&cr, stdin, ptrs, 65536)) > 0)
 *       dispatch_to_pool(ptrs, n, results);
 *   if (n < 0) { perror("realloc"); exit(1); }
 * ═══════════════════════════════════════════════════════════════════════════ */

/* 2 MB read granularity.  Adjust with -DELD_CHUNK_SIZE=N at compile time. */
#ifndef ELD_CHUNK_SIZE
#  define ELD_CHUNK_SIZE (2 * 1024 * 1024)
#endif

typedef struct {
    char  *buf;
    size_t buf_sz;
    size_t leftover;
} EldChunkReader;

static int eld_cr_init(EldChunkReader *cr)
{
    cr->buf_sz   = ELD_CHUNK_SIZE;
    cr->buf      = (char *)malloc(ELD_CHUNK_SIZE + 1); /* +1 for sentinel '\0' */
    cr->leftover = 0;
    return cr->buf ? 0 : -1;
}

static void eld_cr_free(EldChunkReader *cr)
{
    free(cr->buf);
    cr->buf      = NULL;
    cr->leftover = 0;
}

/* Fill ptrs[] with pointers to null-terminated, stripped lines read from f.
 * Returns the number of complete lines found, 0 at true EOF with no data
 * left, or -1 if a buffer growth (realloc) failed (out of memory).
 * ptrs[] entries point into cr->buf; they are invalidated by the next call. */
static int eld_cr_next(EldChunkReader *cr, FILE *f,
                        char **ptrs, int max_ptrs)
{
    for (;;) {
        /* Append new data after any leftover from the previous call. */
        size_t nr    = fread(cr->buf + cr->leftover, 1,
                              cr->buf_sz - cr->leftover, f);
        size_t total = cr->leftover + nr;
        if (total == 0) return 0;
        cr->buf[total] = '\0';   /* sentinel: safe because buf has +1 extra byte */

        int    n          = 0;
        char  *scan       = cr->buf;
        char  *end        = cr->buf + total;
        char  *line_start = cr->buf;

        while (scan < end && n < max_ptrs) {
            if (*scan == '\n') {
                /* Strip \r from CRLF — only the char immediately before \n. */
                if (scan > line_start && scan[-1] == '\r') scan[-1] = '\0';
                *scan = '\0';
                if (line_start[0] != '\0') ptrs[n++] = line_start;
                line_start = scan + 1;
            }
            scan++;
        }

        /* Handle last line with no trailing newline, but only at EOF. */
        if (feof(f) && line_start < end && n < max_ptrs) {
            char *e = end;
            while (e > line_start && (e[-1] == '\r' || e[-1] == '\n')) *--e = '\0';
            if (line_start[0] != '\0') ptrs[n++] = line_start;
            line_start = cr->buf + total;  /* all consumed */
        }

        /* Save the unfinished tail for the next call. */
        cr->leftover = (size_t)(cr->buf + total - line_start);

        /* No complete line was found and the buffer is entirely full of
         * leftover data, with more input still available: a single line
         * is at least as long as the buffer.  Grow it and retry instead of
         * returning 0 (which callers treat as EOF) and silently dropping
         * the rest of the stream. */
        if (n == 0 && cr->leftover == cr->buf_sz && !feof(f)) {
            size_t new_sz = cr->buf_sz * 2;
            char  *nb     = (char *)realloc(cr->buf, new_sz + 1);
            if (!nb) return -1;
            cr->buf    = nb;
            cr->buf_sz = new_sz;
            continue;
        }

        if (cr->leftover > 0) memmove(cr->buf, line_start, cr->leftover);
        return n;
    }
}

#endif /* ELD_CLI_C_INCLUDED */
