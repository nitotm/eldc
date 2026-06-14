/**
 * demo_eldc_lib.ts — ELDC shared library demo for Node.js / TypeScript (koffi).
 *
 * npm install koffi
 * Run: npx ts-node demo_eldc_lib.ts
 *  or: node --loader ts-node/esm demo_eldc_lib.ts
 *
 * Also works with plain JS: rename to .js and remove the type annotations.
 *
 * Note: Deno users can use Deno.dlopen() with --allow-ffi for detect() and
 * config functions; detect_details() requires manual struct byte parsing.
 */

import koffi from 'koffi';
import { join } from 'path';

const libName = process.platform === 'win32'  ? 'eldc.dll'
              : process.platform === 'darwin' ? join(__dirname, 'libeldc.dylib')
              :                                 join(__dirname, 'libeldc.so');

const lib = koffi.load(libName);

// ── Struct definitions ────────────────────────────────────────────────────────
const MAX_SCORES = 20;

const EldcScoreItem = koffi.struct('EldcScoreItem', {
    language: 'const char *',
    score:    'float',
});

const EldcDetectResult = koffi.struct('EldcDetectResult', {
    language: 'const char *',
    reliable: 'int',
    n_scores: 'int',
    scores:   koffi.array(EldcScoreItem, MAX_SCORES),
});

// ── Function bindings ─────────────────────────────────────────────────────────
const eldc_init           = lib.func('void eldc_init()');
const eldc_close          = lib.func('void eldc_close()');
const eldc_detect         = lib.func('const char *eldc_detect(const char *text)');
const eldc_detect_details = lib.func('const char *eldc_detect_details(const char *text, _Out_ EldcDetectResult *result)');
const eldc_set_languages  = lib.func('const char *eldc_set_languages(const char *codes)');
const eldc_set_scheme     = lib.func('void eldc_set_scheme(const char *scheme)');
const eldc_set_scores     = lib.func('void eldc_set_scores(int n)');
const eldc_set_faster     = lib.func('void eldc_set_faster(int flag)');

// ── 0. set_faster (Not worth it. Call before init()) ─────────────────────────
// eldc_set_faster(1);  // 64 MB table (32MB default) Minimal speedup.

// ── 1. init ──────────────────────────────────────────────────────────────────
eldc_init();
console.log('=== eldc TypeScript/Node.js demo ===\n');

// ── 2. detect ────────────────────────────────────────────────────────────────
console.log('-- detect --');
console.log(eldc_detect('Bonjour le monde'));  // fr
console.log(eldc_detect('12345 !@#'));          // und

// ── 3. detect_details ────────────────────────────────────────────────────────
console.log('\n-- detect_details --');
const r: any = {};
eldc_detect_details('Bonjour le monde', r);
console.log(`language : ${r.language}  reliable: ${r.reliable === 1}`);
for (let i = 0; i < r.n_scores; i++)
    console.log(`  ${r.scores[i].language}: ${r.scores[i].score.toFixed(4)}`);

// ── 4. set_scores ─────────────────────────────────────────────────────────────
console.log('\n-- set_scores(2) --');
eldc_set_scores(2); // Default 3, Max 20, Min 1.
const r2: any = {};
eldc_detect_details('Was ist das?', r2);
console.log(`language: ${r2.language}  n_scores: ${r2.n_scores}`);  // de, 2
eldc_set_scores(3);

// ── 5. set_languages ──────────────────────────────────────────────────────────
console.log('\n-- set_languages --');
console.log(eldc_set_languages('en,fr,es'));    // en,fr,es
console.log(eldc_set_languages('eng,fra,xyz')); // en,fr  (xyz → stderr)
console.log(eldc_detect('Bonjour le monde'));   // fr
eldc_set_languages('');                         // reset
console.log(eldc_detect('Hola mundo'));         // es

// ── 6. set_scheme ─────────────────────────────────────────────────────────────
console.log('\n-- set_scheme --');
eldc_set_scheme('iso639-2t');
console.log(eldc_detect('Bonjour'));            // fra
eldc_set_scheme('iso639-1');
console.log(eldc_detect('Bonjour'));            // fr

// ── 7. cleanup ────────────────────────────────────────────────────────────────
eldc_close();
