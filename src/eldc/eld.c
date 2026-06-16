/*
 * eld.c — Efficient Language Detector (single-threaded CLI)
 *
 * Compile:
 *   gcc  -O3 -march=native -o eldc eld.c -lm
 *   clang -O3 -march=native -o eldc eld.c -lm
 *
 * Do NOT add -std=c11.  That flag prevents GCC from statically proving the
 * safety of memcpy(&k, ngram, 8) inside detect_ex(), leaving a
 * _FORTIFY_SOURCE runtime bounds-check wrapper (__memcpy_chk) in the hot
 * hash-table scan loop.  Without it GCC uses its default (gnu17), eliminates
 * the wrapper, and the binary runs ~7% faster on real workloads.
 *
 * Custom DB or hash-table size:
 *   gcc ... -DELD_DB_INCLUDE='"my_db.h"' ...
 *
 * Thread safety: see eld_core.c header comment.
 *
 * For multi-threaded processing of large stdin streams, use eld_mt.c.
 * This file uses the same detection core but a single-threaded I/O loop.
 */

/* _POSIX_C_SOURCE must precede any system header. eld_core.c carries the
 * same guard; setting it here ensures it is active before any include. */
#ifndef _WIN32
#  ifndef _POSIX_C_SOURCE
#    define _POSIX_C_SOURCE 200809L
#  endif
#endif

/* ── Pull in the shared engine ───────────────────────────────────────────── */
#include "eld_core.c"
#include "eld_cli_utils.c"

/* ═══════════════════════════════════════════════════════════════════════════
 * Tuning constants
 * ═══════════════════════════════════════════════════════════════════════════ */
/* Lines collected per eld_cr_next() call.  65536 keeps the same granularity
 * as eld_mt.c so the 2 MB internal chunk buffer is consumed in few fread()
 * calls.  A smaller value (e.g. 4096) would cause many small fread()s and
 * measurably hurt throughput (~4x on typical corpora). */
#define MAX_BATCH_LINES  65536
#define OUT_BUF_SIZE    (1 * 1024 * 1024)   /* output flush granularity      */

/* ── Guard: allow #define ELD_NO_MAIN; #include "eld.c" for backward compat */
#ifndef ELD_NO_MAIN

/* ═══════════════════════════════════════════════════════════════════════════
 * CLI helpers
 * ═══════════════════════════════════════════════════════════════════════════ */
static void print_lang_list(void) {
    puts("Supported codes (ISO 639-1 | ISO 639-2/T):");
    for (int i = 0; i < MAX_LANGUAGES; i++)
        printf("  %-4s  %s\n", ELD_langCodes[i], ELD_ISO639_2T[i]);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * main()
 * ═══════════════════════════════════════════════════════════════════════════ */
int main(int argc, char *argv[])
{
    EldConfig cfg = { 0, 0, 0, (uint64_t)-1, 0 };
    int   verbose           = 0;
    char *input_text        = NULL;
    int   force_interactive = 0;

    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        if (!strcmp(a,"-h")||!strcmp(a,"--help")) {
            eld_print_help(argv[0], 0); return 0;
        } else if (!strcmp(a,"--list-languages")) {
            print_lang_list(); return 0;
        } else if (!strcmp(a,"-v")||!strcmp(a,"--verbose")) {
            verbose = 1;
        } else if (!strcmp(a,"--interactive")) {
            force_interactive = 1;
        } else if (!strcmp(a,"-r")||!strcmp(a,"--reliable")) {
            cfg.reliable = 1;
        } else if (!strcmp(a,"-s")||!strcmp(a,"--scores")) {
            /* Optional integer N follows; default = ELD_MAX_SCORES. */
            if (i+1 < argc && argv[i+1][0] >= '0' && argv[i+1][0] <= '9') {
                int n = atoi(argv[++i]);
                if (n < 1) n = 1;
                if (n > ELD_MAX_SCORES) n = ELD_MAX_SCORES;
                cfg.scores = n;
            } else {
                cfg.scores = ELD_MAX_SCORES;
            }
        } else if (!strcmp(a,"-l")||!strcmp(a,"--languages")) {
            if (++i >= argc) { fprintf(stderr,"--languages requires a value\n"); return 1; }
            uint64_t mask = parse_lang_mask(argv[i]);
            if (!mask) {
                fprintf(stderr,"Warning: no valid languages; using all\n");
                cfg.lang_mask = (uint64_t)-1; cfg.subset = 0;
            } else {
                cfg.lang_mask = mask; cfg.subset = 1;
                /* Keep globals in sync so detect_ex() fast-path sees the filter. */
                g_lang_mask = mask; g_subset = 1;
            }
        } else if (!strcmp(a,"--scheme")) {
            if (++i >= argc) { fprintf(stderr,"--scheme requires a value\n"); return 1; }
            if (!strcmp(argv[i],"iso639-1")||!strcmp(argv[i],"iso639_1")) {
                cfg.scheme = 0; g_scheme = SCHEME_ISO639_1;
            } else if (!strcmp(argv[i],"iso639-2t")||!strcmp(argv[i],"iso639_2t")) {
                cfg.scheme = 1; g_scheme = SCHEME_ISO639_2T;
            } else {
                fprintf(stderr,"Unknown scheme '%s'\n",argv[i]); return 1;
            }
        } else if (a[0] != '-') {
            input_text = (char *)a;
        } else {
            fprintf(stderr,"Unknown option: %s  (use -h)\n",a); return 1;
        }
    }

    if (verbose) {
        fprintf(stderr,"Loading (scheme=%s",g_scheme==SCHEME_ISO639_2T?"iso639-2t":"iso639-1");
        if (cfg.subset) {
            int cnt=0; for(int i=0;i<MAX_LANGUAGES;i++) if((cfg.lang_mask>>i)&1) cnt++;
            fprintf(stderr,", filter=%d lang%s", cnt, cnt==1?"":"s");
        }
        fprintf(stderr,")... "); fflush(stderr);
    }

    clock_t t0 = clock();
    init_detector();

    if (verbose) {
        fprintf(stderr,"Done! (%.1f ms)\n----------------------------------------\n",
                (double)(clock()-t0)/CLOCKS_PER_SEC*1000.0);
    }

    /* ── Single-text mode ──────────────────────────────────────────────── */
    if (input_text) {
        double start_time = verbose ? get_time_seconds() : 0.0;

        EldResult result;
        eld_process_line(input_text, &cfg, &result);

        if (cfg.scores || cfg.reliable) {
            char buf[ELD_EMIT_BUFSIZE];
            fwrite(buf, 1, eld_emit_result(buf, &result, &cfg), stdout);
        } else if (verbose) {
            double elapsed = get_time_seconds() - start_time;
            printf("%s\n(%.7f ms)\n", result.language ? result.language : "und", elapsed * 1000.0);
        } else {
            puts(result.language ? result.language : "und");
        }
        return 0;
    }

    /* ── Stdin mode ────────────────────────────────────────────────────── */
    if (verbose) { fprintf(stderr,"Stream mode (Ctrl+D to stop):\n\n"); fflush(stderr); }

    double start_time = verbose ? get_time_seconds() : 0.0;

#ifdef _WIN32
    int is_tty = _isatty(_fileno(stdin));
#else
    int is_tty = isatty(STDIN_FILENO);
#endif

    /* Interactive: flush every line so the user sees results immediately. */
    if (is_tty || force_interactive) {
        setvbuf(stdout, NULL, _IONBF, 0);
        char line[4096];
        while (fgets(line, sizeof line, stdin)) {
            size_t len = strlen(line);
            if (len > 0 && line[len-1] == '\n') line[--len] = '\0';
            if (len == 0) continue;
            EldResult result;
            eld_process_line(line, &cfg, &result);
            char buf[ELD_EMIT_BUFSIZE];
            fwrite(buf, 1, eld_emit_result(buf, &result, &cfg), stdout);
        }
        return 0;
    }

    /* ── Non-interactive: large-batch chunk reader ──────────────────────
     *
     * Design rationale vs. obvious alternatives:
     *
     *   MAX_BATCH_LINES = 65536 (not 4096):
     *     EldChunkReader has a 2 MB internal buffer. With 4096 max_ptrs and
     *     ~50 B/line, each eld_cr_next() consumes ~200 KB and leaves ~1.8 MB
     *     as leftover, so the next fread() only reads 200 KB.  That means
     *     ~10 fread() calls per 2 MB chunk — a ~4x throughput hit on large
     *     files.  65536 lines × 50 B ≈ 3.2 MB, so one eld_cr_next() usually
     *     drains the entire 2 MB chunk in a single call.
     *
     *   No EldResult[] array (local result, not results[i]):
     *     An EldResult is ~100 bytes. Storing 65536 of them (≈ 6.4 MB) and
     *     then iterating over them again to format wastes memory bandwidth and
     *     L2/L3 cache.  Processing each line immediately with a stack-local
     *     EldResult keeps the hot data in L1 the whole time.
     *
     *   op declared outside the outer loop:
     *     Declaring op = out_buf inside the loop would reset it each batch
     *     and force a fwrite() at the end of every batch (even if the buffer
     *     is nearly empty).  Keeping op outside lets output accumulate across
     *     batches; we only flush when the 1 MB buffer is genuinely full.
     *     This halves the number of fwrite() calls on typical corpora.
     *
     *   total_lines += n (not total_lines++ inside inner loop):
     *     n is already the exact line count for the batch; incrementing once
     *     per batch is both correct and avoids a branch in the inner loop.
     * ─────────────────────────────────────────────────────────────────── */
    setvbuf(stdout, NULL, _IOFBF, 1 << 20);

    char **line_ptrs = (char **)malloc(MAX_BATCH_LINES * sizeof(char *));
    char  *out_buf   = (char *)malloc(OUT_BUF_SIZE + ELD_EMIT_BUFSIZE);
    if (!line_ptrs || !out_buf) { perror("malloc"); return 1; }

    EldChunkReader cr;
    if (eld_cr_init(&cr) < 0) { perror("malloc"); return 1; }

    long  total_lines = 0;
    char  *op         = out_buf;           /* accumulates across batches     */
    char *const lim   = out_buf + OUT_BUF_SIZE;

    for (;;) {
        int n = eld_cr_next(&cr, stdin, line_ptrs, MAX_BATCH_LINES);
        if (n < 0) { fprintf(stderr,"eldc: out of memory while reading stdin\n"); return 1; }
        if (n == 0) break;
        total_lines += n;
        for (int i = 0; i < n; i++) {
            if (op >= lim) {
                fwrite(out_buf, 1, (size_t)(op - out_buf), stdout);
                op = out_buf;
            }
            EldResult result;                          /* ~100 B, always L1 */
            eld_process_line(line_ptrs[i], &cfg, &result);
            op += eld_emit_result(op, &result, &cfg);
        }
    }
    if (op > out_buf) fwrite(out_buf, 1, (size_t)(op - out_buf), stdout);

    eld_cr_free(&cr);
    free(line_ptrs);
    free(out_buf);

    if (verbose) {
        double elapsed = get_time_seconds() - start_time;
        fprintf(stderr,"\n%ld lines in %.3f s  (%.0f lines/s)\n",
                total_lines, elapsed,
                total_lines > 0 ? (double)total_lines / elapsed : 0.0);
    }
    return 0;
}
#endif /* ELD_NO_MAIN */
