//! demo_eldc_lib.rs — ELDC shared library demo for Rust.
//!
//! Cargo.toml dependency:  libloading = "0.8"
//! Run: cargo run  (place libeldc.so / eldc.dll next to the binary)

use libloading::{Library, Symbol};
use std::ffi::{CStr, CString};
use std::os::raw::{c_char, c_float, c_int, c_void};

const MAX_SCORES: usize = 20;

#[repr(C)]
struct EldcScoreItem {
    language: *const c_char,
    score:    c_float,
}

#[repr(C)]
struct EldcDetectResult {
    language: *const c_char,
    reliable: c_int,
    n_scores: c_int,
    scores:   [EldcScoreItem; MAX_SCORES],
}

fn c(s: &str) -> CString { CString::new(s).unwrap() }
unsafe fn r(p: *const c_char) -> &'static str {
    if p.is_null() { "und" } else { CStr::from_ptr(p).to_str().unwrap_or("?") }
}

fn main() {
    unsafe {
        let lib_name = if cfg!(windows)              { "eldc.dll" }
                       else if cfg!(target_os="macos") { "./libeldc.dylib" }
                       else                            { "./libeldc.so" };

        let lib = Library::new(lib_name).expect("could not load library");

        // ── Global API ────────────────────────────────────────────────────────
        let init:    Symbol<unsafe extern "C" fn()>                                          = lib.get(b"eldc_init\0").unwrap();
        let close:   Symbol<unsafe extern "C" fn()>                                          = lib.get(b"eldc_close\0").unwrap();
        let detect:  Symbol<unsafe extern "C" fn(*const c_char) -> *const c_char>            = lib.get(b"eldc_detect\0").unwrap();
        let details: Symbol<unsafe extern "C" fn(*const c_char, *mut EldcDetectResult)>      = lib.get(b"eldc_detect_details\0").unwrap();
        let set_lang:   Symbol<unsafe extern "C" fn(*const c_char) -> *const c_char>         = lib.get(b"eldc_set_languages\0").unwrap();
        let set_scheme: Symbol<unsafe extern "C" fn(*const c_char)>                          = lib.get(b"eldc_set_scheme\0").unwrap();
        let set_scores: Symbol<unsafe extern "C" fn(c_int)>                                  = lib.get(b"eldc_set_scores\0").unwrap();

        // ── Instance (cfg) API — EldcConfig is opaque, use *mut c_void ────────
        let cfg_create:   Symbol<unsafe extern "C" fn() -> *mut c_void>                                          = lib.get(b"eldc_config_create\0").unwrap();
        let cfg_free:     Symbol<unsafe extern "C" fn(*mut c_void)>                                              = lib.get(b"eldc_config_free\0").unwrap();
        let detect_cfg:   Symbol<unsafe extern "C" fn(*mut c_void, *const c_char) -> *const c_char>              = lib.get(b"eldc_detect_cfg\0").unwrap();
        let details_cfg:  Symbol<unsafe extern "C" fn(*mut c_void, *const c_char, *mut EldcDetectResult)>        = lib.get(b"eldc_detect_details_cfg\0").unwrap();
        let set_lang_cfg:   Symbol<unsafe extern "C" fn(*mut c_void, *const c_char) -> *const c_char>            = lib.get(b"eldc_set_languages_cfg\0").unwrap();
        let set_scheme_cfg: Symbol<unsafe extern "C" fn(*mut c_void, *const c_char)>                             = lib.get(b"eldc_set_scheme_cfg\0").unwrap();
        let set_scores_cfg: Symbol<unsafe extern "C" fn(*mut c_void, c_int)>                                     = lib.get(b"eldc_set_scores_cfg\0").unwrap();

        // ── 1. init ──────────────────────────────────────────────────────────
        init();
		  // ── Optional: Isolated configuration instance ────────────────────────
		  let i_config = cfg_create();

        println!("=== eldc Rust demo ===\n");

        // ── 2. detect ────────────────────────────────────────────────────────
        println!("-- detect --");
        println!("{}", r(detect(c("Bonjour le monde").as_ptr())));   // fr
        println!("{}", r(detect(c("12345 !@#").as_ptr())));          // und
		  // Same behavior for instance protected configuration detect
		  println!("{}", r(detect_cfg(i_config, c("Bonjour").as_ptr())));  // fr

        // ── 3. detect_details ────────────────────────────────────────────────
        // eldc_detect_details fills the struct and returns void; read from res.
        println!("\n-- detect_details --");
        let mut res: EldcDetectResult = std::mem::zeroed();
        details(c("Bonjour le monde").as_ptr(), &mut res);
        println!("language : {}  reliable: {}", r(res.language), res.reliable == 1);
        for i in 0..res.n_scores as usize {
            println!("  {}: {:.4}", r(res.scores[i].language), res.scores[i].score);
        }

		  details_cfg(i_config, c("Bonjour").as_ptr(), &mut res);  // Same behavior

        // ── 4. set_scores ────────────────────────────────────────────────────
        println!("\n-- set_scores(2) --");
        set_scores(2);   // Default 3, Max 20, Min 1.  Global setter
        let mut res2: EldcDetectResult = std::mem::zeroed();
        details(c("Was ist das?").as_ptr(), &mut res2);
        println!("language: {}  n_scores: {}", r(res2.language), res2.n_scores);  // de, 2
        set_scores(3);   // reset

		  set_scores_cfg(i_config, 2);  // instance setter

        // ── 5. set_languages ─────────────────────────────────────────────────
        println!("\n-- set_languages --");
        println!("{}", r(set_lang(c("en,fr,es").as_ptr())));        // en,fr,es  Global setter
        println!("{}", r(set_lang(c("eng,fra,xyz").as_ptr())));     // en,fr (xyz → stderr)
        println!("{}", r(detect(c("Bonjour le monde").as_ptr())));  // fr
        set_lang(c("").as_ptr());                                   // reset
        println!("{}", r(detect(c("Hola doctor").as_ptr())));       // es

		  set_lang_cfg(i_config, c("en,fr,es").as_ptr());             // instance setter

        // ── 6. set_scheme ────────────────────────────────────────────────────
        println!("\n-- set_scheme --");
        set_scheme(c("iso639-2t").as_ptr());                // Global setter
        println!("{}", r(detect(c("Bonjour").as_ptr())));   // fra
        set_scheme(c("iso639-1").as_ptr());
        println!("{}", r(detect(c("Bonjour").as_ptr())));   // fr

		  set_scheme_cfg(i_config, c("iso639-2t").as_ptr());  // instance setter

        // ── 7. cleanup ────────────────────────────────────────────────────────
        cfg_free(i_config);
        // Global unload
        close();
    }
}
