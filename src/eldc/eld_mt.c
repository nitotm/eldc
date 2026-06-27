/* eld_mt.c — Multi-threaded CLI for the ELD language detector.
 *
 * Unity-builds eld_core.c to pull in the detector engine without any CLI
 * entry point.  Adds a persistent thread pool, chunk-based stdin reading,
 * and -t / --threads.  All existing output options (--scores [N], --reliable,
 * --scheme, --languages) work unchanged.  Output order is always preserved.
 *
 * Build (Linux / macOS):
 *   gcc -O3 -march=native -o eldc_mt eld_mt.c -lm -lpthread
 *
 * Build (Windows, MinGW-w64):
 *   gcc -O3 -o eldc_mt.exe eld_mt.c -lm -lpthread -static
 *
 * Custom DB or hash-table size (same flags as eld.c):
 *   gcc ... -DELD_HT_BITS=22 -DELD_DB_INCLUDE='"my_db.h"' ...
 */

/* _POSIX_C_SOURCE must be the very first definition so strnlen, strtok_r,
 * clock_gettime, etc. are declared before any system header is pulled in. */
#ifndef _WIN32
#  ifndef _POSIX_C_SOURCE
#    define _POSIX_C_SOURCE 200809L
#  endif
#endif

/* ── Platform-specific CPU-count headers ─────────────────────────────────── */
#ifdef _WIN32
#  include <windows.h>
#endif
#ifdef __linux__
#  include <sys/sysinfo.h>
#endif

#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>

#ifdef _WIN32
#  include <io.h>
#  define isatty _isatty
#  define fileno _fileno
#else
#  include <unistd.h>
#endif

#if defined(_WIN32) && !defined(__MINGW32__)
#  ifndef CLOCK_MONOTONIC
#    define CLOCK_MONOTONIC 1
#  endif
static int clock_gettime(int clk_id, struct timespec *spec)
{
    LARGE_INTEGER count, freq;
    if (clk_id != CLOCK_MONOTONIC) return -1;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&count);
    spec->tv_sec  = (time_t)(count.QuadPart / freq.QuadPart);
    spec->tv_nsec = (long)((count.QuadPart % freq.QuadPart) * 1000000000LL / freq.QuadPart);
    return 0;
}
#endif

/* ── Unity-build: detector engine, no CLI entry point ───────────────────── */
#include "eld_core.c"
#include "eld_cli_utils.c"

/* ═══════════════════════════════════════════════════════════════════════════
 * Tuning constants
 * ═══════════════════════════════════════════════════════════════════════════ */
#define MAX_BATCH_LINES   65536             /* max lines buffered per round    */
#define OUT_BUF_SIZE     (1 * 1024 * 1024)  /* output flush granularity (bytes)*/

/* ═══════════════════════════════════════════════════════════════════════════
 * Thread pool — persistent workers + atomic work queue
 * ═══════════════════════════════════════════════════════════════════════════ */
typedef struct {
    pthread_mutex_t mu;
    pthread_cond_t  cv_work;
    pthread_cond_t  cv_done;
    int             phase;
    int             done_count;
    int             n_workers;
    int             shutdown;

    char      **texts;
    int         count;
    atomic_int  next_idx;
    EldResult  *results;

    EldConfig   cfg;
} Pool;

static void *worker_fn(void *arg)
{
    Pool *p     = arg;
    int   phase = 0;

    for (;;) {
        pthread_mutex_lock(&p->mu);
        while (p->phase == phase && !p->shutdown)
            pthread_cond_wait(&p->cv_work, &p->mu);
        if (p->shutdown) { pthread_mutex_unlock(&p->mu); return NULL; }
        phase = p->phase;
        pthread_mutex_unlock(&p->mu);

        for (;;) {
            int i = atomic_fetch_add_explicit(&p->next_idx, 1, memory_order_relaxed);
            if (i >= p->count) break;
            eld_process_line(p->texts[i], &p->cfg, &p->results[i]);
        }

        pthread_mutex_lock(&p->mu);
        if (++p->done_count == p->n_workers)
            pthread_cond_signal(&p->cv_done);
        pthread_mutex_unlock(&p->mu);
    }
    return NULL;
}

/* Dispatch a batch; main thread participates alongside workers.
 * Blocks until every line is processed. */
static void run_batch(Pool       *p,
                      char      **texts,
                      int         count,
                      EldResult  *results)
{
    p->texts   = texts;
    p->count   = count;
    p->results = results;
    atomic_store_explicit(&p->next_idx, 0, memory_order_relaxed);

    pthread_mutex_lock(&p->mu);
    p->done_count = 0;
    p->phase++;
    pthread_cond_broadcast(&p->cv_work);
    pthread_mutex_unlock(&p->mu);

    for (;;) {
        int i = atomic_fetch_add_explicit(&p->next_idx, 1, memory_order_relaxed);
        if (i >= count) break;
        eld_process_line(p->texts[i], &p->cfg, &p->results[i]);
    }

    if (p->n_workers > 0) {
        pthread_mutex_lock(&p->mu);
        while (p->done_count < p->n_workers)
            pthread_cond_wait(&p->cv_done, &p->mu);
        pthread_mutex_unlock(&p->mu);
    }
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Logical CPU count
 * ═══════════════════════════════════════════════════════════════════════════ */
static int get_nprocs_mt(void)
{
    int n = 1;
#ifdef _WIN32
    SYSTEM_INFO si; GetSystemInfo(&si); n = (int)si.dwNumberOfProcessors;
#else
#  ifdef __linux__
    n = get_nprocs();
#  else
#    if defined(_SC_NPROCESSORS_ONLN)
    { long v = sysconf(_SC_NPROCESSORS_ONLN); if (v > 0) n = (int)v; }
#    elif defined(_SC_NPROCESSORS_CONF)
    { long v = sysconf(_SC_NPROCESSORS_CONF); if (v > 0) n = (int)v; }
#    endif
#  endif
#endif
    return n;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * main
 * ═══════════════════════════════════════════════════════════════════════════ */
int main(int argc, char **argv)
{
    EldConfig   cfg       = { 0, 0, SCHEME_ISO639_1, (uint64_t)-1, 0 };
    int         n_threads = get_nprocs_mt();
    int         verbose   = 0;

    const char *text_args[1024];
    int         n_text_args = 0;

    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];

        if (!strcmp(a,"-h")||!strcmp(a,"--help")) {
            eld_print_help(argv[0], 1); return 0;

        } else if (!strcmp(a,"--list-languages")) {
            for (int j = 0; j < MAX_LANGUAGES; j++)
                printf("%s\t%s\n", ELD_langCodes[j], ELD_ISO639_2T[j]);
            return 0;

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

        } else if (!strcmp(a,"-r")||!strcmp(a,"--reliable")) {
            cfg.reliable = 1;

        } else if (!strcmp(a,"-v")||!strcmp(a,"--verbose")) {
            verbose = 1;

        } else if ((!strcmp(a,"-t")||!strcmp(a,"--threads")) && i+1 < argc) {
            n_threads = atoi(argv[++i]);
            if (n_threads < 1) n_threads = 1;

        } else if (!strcmp(a,"--scheme") && i+1 < argc) {
            const char *s = argv[++i];
            if      (!strcmp(s,"iso639-2t")||!strcmp(s,"iso639_2t")) {
					cfg.scheme = SCHEME_ISO639_2T; g_config.scheme = SCHEME_ISO639_2T;
            } else if ( strcmp(s,"iso639-1") && strcmp(s,"iso639_1")) {
                fprintf(stderr,"Unknown scheme '%s'\n",s); return 1;
            }

        } else if (!strcmp(a,"--languages") && i+1 < argc) {
            uint64_t mask = parse_lang_mask(argv[++i]);
            if (!mask) {
                fprintf(stderr,"Warning: no valid languages; using all\n");
            } else {
                cfg.lang_mask = mask; cfg.subset = 1;
                g_config.lang_mask = mask; g_config.subset = 1;
            }

        } else if (a[0] != '-') {
            if (n_text_args < (int)(sizeof text_args / sizeof *text_args))
                text_args[n_text_args++] = a;

        } else {
            fprintf(stderr,"Unknown option '%s' (try --help)\n",a); return 1;
        }
    }

    init_detector();

    struct timespec ts0 = {0};
    if (verbose) clock_gettime(CLOCK_MONOTONIC, &ts0);

    /* Output buffer shared by both modes. */
    char *out_buf = (char *)malloc(OUT_BUF_SIZE + ELD_EMIT_BUFSIZE);
    if (!out_buf) { perror("malloc"); return 1; }

    /* ── Text-argument mode ─────────────────────────────────────────────── */
    if (n_text_args > 0) {
        char *op = out_buf;
        for (int i = 0; i < n_text_args; i++) {
            EldResult r = {0};
            eld_process_line(text_args[i], &cfg, &r);
            op += eld_emit_result(op, &r, &cfg);
        }
        fwrite(out_buf, 1, (size_t)(op - out_buf), stdout);
        free(out_buf);
        return 0;
    }

    /* ── Stdin mode ─────────────────────────────────────────────────────── */
#ifdef _WIN32
    int is_tty = _isatty(_fileno(stdin));
#else
    int is_tty = isatty(STDIN_FILENO);
#endif

    /* Interactive: single-threaded, flush after every line. */
    if (is_tty) {
        char line[4096];
        while (fgets(line, (int)sizeof line, stdin)) {
            int len = (int)strlen(line);
            while (len > 0 && (line[len-1] == '\n' || line[len-1] == '\r'))
                line[--len] = '\0';
            if (!len) continue;

            EldResult r = {0};
            eld_process_line(line, &cfg, &r);
            char *op = out_buf;
            op += eld_emit_result(op, &r, &cfg);
            fwrite(out_buf, 1, (size_t)(op - out_buf), stdout);
            fflush(stdout);
        }
        free(out_buf);
        return 0;
    }

    /* Non-interactive: batch mode with thread pool. */
    setvbuf(stdout, NULL, _IOFBF, 1 << 20);

    Pool pool;
    memset(&pool, 0, sizeof pool);
    pthread_mutex_init(&pool.mu,      NULL);
    pthread_cond_init (&pool.cv_work, NULL);
    pthread_cond_init (&pool.cv_done, NULL);
    pool.cfg       = cfg;
    pool.n_workers = n_threads - 1;

    pthread_t *threads = NULL;
    if (pool.n_workers > 0) {
        threads = (pthread_t *)malloc((size_t)pool.n_workers * sizeof(pthread_t));
        if (!threads) { perror("malloc"); return 1; }
        for (int i = 0; i < pool.n_workers; i++)
            if (pthread_create(&threads[i], NULL, worker_fn, &pool) != 0) {
                perror("pthread_create");
                pool.n_workers = i;
                break;
            }
    }

    char      **line_ptrs = (char **)malloc(MAX_BATCH_LINES * sizeof(char *));
    EldResult  *results   = (EldResult *)malloc(MAX_BATCH_LINES * sizeof(EldResult));
    if (!line_ptrs || !results) { perror("malloc"); return 1; }

    EldChunkReader cr;
    if (eld_cr_init(&cr) < 0) { perror("malloc"); return 1; }

    long total_lines = 0;
	 char *op        = out_buf;

    for (;;) {
        int n = eld_cr_next(&cr, stdin, line_ptrs, MAX_BATCH_LINES);
        if (n < 0) { fprintf(stderr,"eldc: out of memory while reading stdin\n"); return 1; }
        if (n == 0) break;
        total_lines += n;
        run_batch(&pool, line_ptrs, n, results);


        char *const lim = out_buf + OUT_BUF_SIZE;
        for (int i = 0; i < n; i++) {
            if (op >= lim) {
                fwrite(out_buf, 1, (size_t)(op - out_buf), stdout);
                op = out_buf;
            }
            op += eld_emit_result(op, &results[i], &cfg);
        }
    }
	 if (op > out_buf) fwrite(out_buf, 1, (size_t)(op - out_buf), stdout);

    /* Shutdown pool. */
    if (pool.n_workers > 0) {
        pthread_mutex_lock(&pool.mu);
        pool.shutdown = 1;
        pthread_cond_broadcast(&pool.cv_work);
        pthread_mutex_unlock(&pool.mu);
        for (int i = 0; i < pool.n_workers; i++)
            pthread_join(threads[i], NULL);
        free(threads);
    }
    pthread_mutex_destroy(&pool.mu);
    pthread_cond_destroy(&pool.cv_work);
    pthread_cond_destroy(&pool.cv_done);

    eld_cr_free(&cr);
    free(line_ptrs);
    free(results);
    free(out_buf);

    if (verbose) {
        struct timespec ts1;
        clock_gettime(CLOCK_MONOTONIC, &ts1);
        double elapsed = (ts1.tv_sec  - ts0.tv_sec)
                       + (ts1.tv_nsec - ts0.tv_nsec) * 1e-9;
        fprintf(stderr,
                "%ld lines in %.3f s  (%.0f lines/s)  %d thread%s\n",
                total_lines, elapsed,
                total_lines > 0 ? (double)total_lines / elapsed : 0.0,
                n_threads, n_threads == 1 ? "" : "s");
    }

    return 0;
}
