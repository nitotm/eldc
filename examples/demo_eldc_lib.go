/*
demo_eldc_lib.go — ELDC shared library demo for Go (cgo).

Build:
  CGO_LDFLAGS="-L." go run demo_eldc_lib.go
  # or: CGO_LDFLAGS="-L." go build -o demo_eldc demo_eldc_lib.go
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

// EldcConfig is opaque — only ever used as a pointer
struct EldcConfig;

extern void                eldc_init(void);
extern void                eldc_close(void);
extern const char         *eldc_detect(const char *text);
extern void                eldc_detect_details(const char *text, EldcDetectResult *r);
extern const char         *eldc_set_languages(const char *codes);
extern void                eldc_set_scheme(const char *scheme);
extern void                eldc_set_scores(int n);

extern struct EldcConfig  *eldc_config_create(void);
extern void                eldc_config_free(struct EldcConfig *cfg);
extern const char         *eldc_detect_cfg(struct EldcConfig *cfg, const char *text);
extern void                eldc_detect_details_cfg(struct EldcConfig *cfg, const char *text, EldcDetectResult *r);
extern const char         *eldc_set_languages_cfg(struct EldcConfig *cfg, const char *codes);
extern void                eldc_set_scheme_cfg(struct EldcConfig *cfg, const char *scheme);
extern void                eldc_set_scores_cfg(struct EldcConfig *cfg, int n);
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

	// ── 1. init ──────────────────────────────────────────────────────────────
	C.eldc_init()
	defer C.eldc_close()   // Global unload
	fmt.Println("=== eldc Go demo ===\n")
	// ── Optional: Isolated configuration instance ────────────────────────────
	i_config := C.eldc_config_create()
	defer C.eldc_config_free(i_config)

	// ── 2. detect ────────────────────────────────────────────────────────────
	t := cs("Bonjour le monde"); defer free(t)
	fmt.Println("-- detect --")
	fmt.Println(gs(C.eldc_detect(t)))               // fr
	u := cs("12345 !@#"); defer free(u)
	fmt.Println(gs(C.eldc_detect(u)))               // und
	
	fmt.Println(gs(C.eldc_detect_cfg(i_config, t))) // fr, Same behavior

	// ── 3. detect_details ────────────────────────────────────────────────────
	// eldc_detect_details fills the struct and returns void; read result from r.
	fmt.Println("\n-- detect_details --")
	var r C.EldcDetectResult
	C.eldc_detect_details(t, &r)
	fmt.Printf("language : %s  reliable: %v\n", gs(r.language), r.reliable == 1)
	for i := 0; i < int(r.n_scores); i++ {
		fmt.Printf("  %s: %.4f\n", gs(r.scores[i].language), float32(r.scores[i].score))
	}
	
	C.eldc_detect_details_cfg(i_config, t, &r)  // Same behavior

	// ── 4. set_scores ─────────────────────────────────────────────────────────
	fmt.Println("\n-- set_scores(2) --")
	C.eldc_set_scores(2)   // Default 3, Max 20, Min 1. Global setter
	var r2 C.EldcDetectResult
	w := cs("Was ist das?"); defer free(w)
	C.eldc_detect_details(w, &r2)
	fmt.Printf("language: %s  n_scores: %d\n", gs(r2.language), r2.n_scores)  // de, 2
	C.eldc_set_scores(3)   // reset
	
	C.eldc_set_scores_cfg(i_config, 2) // instance setter

	// ── 5. set_languages ──────────────────────────────────────────────────────
	fmt.Println("\n-- set_languages --")
	l := cs("en,fr,es"); defer free(l)
	fmt.Println(gs(C.eldc_set_languages(l)))   // en,fr,es  Global setter
	l2 := cs("eng,fra,xyz"); defer free(l2)
	fmt.Println(gs(C.eldc_set_languages(l2)))  // en,fr (xyz → stderr)
	fmt.Println(gs(C.eldc_detect(t)))          // fr
	empty := cs(""); defer free(empty)
	C.eldc_set_languages(empty)                // reset
	hd := cs("Hola doctor"); defer free(hd)
	fmt.Println(gs(C.eldc_detect(hd)))         // es
	
	C.eldc_set_languages_cfg(i_config, l)      // instance setter

	// ── 6. set_scheme ─────────────────────────────────────────────────────────
	fmt.Println("\n-- set_scheme --")
	iso2 := cs("iso639-2t"); defer free(iso2)
	C.eldc_set_scheme(iso2)                    // Global setter
	fmt.Println(gs(C.eldc_detect(t)))          // fra
	iso1 := cs("iso639-1"); defer free(iso1)
	C.eldc_set_scheme(iso1)
	fmt.Println(gs(C.eldc_detect(t)))          // fr
	
	C.eldc_set_scheme_cfg(i_config, iso2)      // instance setter

}
