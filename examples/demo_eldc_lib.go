/*
demo_eldc_lib.go — ELDC shared library demo for Go (cgo).

Build:
  CGO_LDFLAGS="-L." go run demo_eldc_lib.go
  # or to compile: CGO_LDFLAGS="-L." go build -o demo_eldc demo_eldc_lib.go
*/
package main

/*
#cgo LDFLAGS: -leldc
#cgo linux   LDFLAGS: -Wl,-rpath,.
#cgo darwin  LDFLAGS: -Wl,-rpath,.
#include <stdlib.h>

#define MAX_SCORES 20
typedef struct { const char *language; float score; } EldcScoreItem;
typedef struct {
    const char   *language;
    int           reliable;
    int           n_scores;
    EldcScoreItem scores[MAX_SCORES];
} EldcDetectResult;

extern void        eldc_init(void);
extern void        eldc_close(void);
extern const char *eldc_detect(const char *text);
extern const char *eldc_detect_details(const char *text, EldcDetectResult *r);
extern const char *eldc_set_languages(const char *codes);
extern void        eldc_set_scheme(const char *scheme);
extern void        eldc_set_scores(int n);
extern void        eldc_set_faster(int flag);
*/
import "C"
import (
	"fmt"
	"unsafe"
)

func cs(s string) *C.char { return C.CString(s) }
func gs(p *C.char) string { return C.GoString(p) }
func free(p *C.char)      { C.free(unsafe.Pointer(p)) }

func main() {
   // ── 0. set_faster (Not worth it. Call before init()) ─────────────────────
	// C.eldc_set_faster(1) // 64 MB table (32MB default) Minimal speedup.

	// ── 1. init ──────────────────────────────────────────────────────────────
	C.eldc_init()
	defer C.eldc_close()
	fmt.Println("=== eldc Go demo ===\n")

	// ── 2. detect ────────────────────────────────────────────────────────────
	t := cs("Bonjour le monde"); defer free(t)
	fmt.Println("-- detect --")
	fmt.Println(gs(C.eldc_detect(t)))               // fr
	u := cs("12345"); defer free(u)
	fmt.Println(gs(C.eldc_detect(u)))               // und

	// ── 3. detect_details ────────────────────────────────────────────────────
	fmt.Println("\n-- detect_details --")
	var r C.EldcDetectResult
	C.eldc_detect_details(t, &r)
	fmt.Printf("language : %s  reliable: %v\n", gs(r.language), r.reliable == 1)
	for i := 0; i < int(r.n_scores); i++ {
		fmt.Printf("  %s: %.4f\n", gs(r.scores[i].language), float32(r.scores[i].score))
	}

	// ── 4. set_scores ─────────────────────────────────────────────────────────
	fmt.Println("\n-- set_scores(2) --")
	C.eldc_set_scores(2) // Default 3, Max 20, Min 1.
	var r2 C.EldcDetectResult
	w := cs("Was ist das?"); defer free(w)
	C.eldc_detect_details(w, &r2)
	fmt.Printf("language: %s  n_scores: %d\n", gs(r2.language), r2.n_scores) // de, 2
	C.eldc_set_scores(3)

	// ── 5. set_languages ──────────────────────────────────────────────────────
	fmt.Println("\n-- set_languages --")
	l := cs("en,fr,es"); defer free(l)
	fmt.Println(gs(C.eldc_set_languages(l)))        // en,fr,es
	l2 := cs("eng,fra,xyz"); defer free(l2)
	fmt.Println(gs(C.eldc_set_languages(l2)))       // en,fr (xyz → stderr)
	fmt.Println(gs(C.eldc_detect(t)))               // fr
	empty := cs(""); defer free(empty)
	C.eldc_set_languages(empty)                     // reset
	h := cs("Hola mundo"); defer free(h)
	fmt.Println(gs(C.eldc_detect(h)))               // es

	// ── 6. set_scheme ─────────────────────────────────────────────────────────
	fmt.Println("\n-- set_scheme --")
	iso2 := cs("iso639-2t"); defer free(iso2)
	C.eldc_set_scheme(iso2)
	fmt.Println(gs(C.eldc_detect(t)))               // fra
	iso1 := cs("iso639-1"); defer free(iso1)
	C.eldc_set_scheme(iso1)
	fmt.Println(gs(C.eldc_detect(t)))               // fr
}
