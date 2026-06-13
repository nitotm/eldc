/**
 * demo_eldc_lib.java — ELDC shared library demo for Java (JNA).
 *
 * Dependency: jna-5.x.jar  (https://github.com/java-native-access/jna)
 * Compile & run:
 *   javac -cp jna.jar demo_eldc_lib.java
 *   java  -cp .:jna.jar -Djna.library.path=. demo_eldc_lib
 */

import com.sun.jna.*;
import com.sun.jna.ptr.*;
import java.util.Arrays;
import java.util.List;

public class demo_eldc_lib {

    static final int MAX_SCORES = 20;

    @Structure.FieldOrder({"lang", "score"})
    public static class EldcScoreItem extends Structure {
        public Pointer lang;   // const char* — static storage, read with .getString(0)
        public float   score;
    }

    @Structure.FieldOrder({"lang", "reliable", "n_scores", "scores"})
    public static class EldcDetectResult extends Structure {
        public Pointer        lang;
        public int            reliable;
        public int            n_scores;
        public EldcScoreItem[] scores = (EldcScoreItem[]) new EldcScoreItem().toArray(MAX_SCORES);
    }

    public interface EldcLib extends Library {
        EldcLib LIB = Native.load("eldc", EldcLib.class);

        void    eldc_init();
        void    eldc_close();
        String  eldc_detect(String text);
        String  eldc_detect_details(String text, EldcDetectResult result);
        String  eldc_set_languages(String codes);
        void    eldc_set_scheme(String scheme);
        void    eldc_set_scores(int n);
        void    eldc_set_faster(int flag);
    }

    static String lang(Pointer p) { return p != null ? p.getString(0) : "und"; }

    public static void main(String[] args) {
        var lib = EldcLib.LIB;

        // ── 0. set_faster (Not worth it. Call before init()) ─────────────────
        // lib.eldc_set_faster(1); // 64 MB table (32MB default) Minimal speedup

        // ── 1. init ──────────────────────────────────────────────────────────
        lib.eldc_init();
        System.out.println("=== eldc Java demo ===\n");

        // ── 2. detect ────────────────────────────────────────────────────────
        System.out.println("-- detect --");
        System.out.println(lib.eldc_detect("Bonjour le monde"));  // fr
        System.out.println(lib.eldc_detect("12345 !@#"));          // und

        // ── 3. detect_details ────────────────────────────────────────────────
        System.out.println("\n-- detect_details --");
        var r = new EldcDetectResult();
        lib.eldc_detect_details("Bonjour le monde", r);
        System.out.printf("language : %s  reliable: %b%n", lang(r.lang), r.reliable == 1);
        for (int i = 0; i < r.n_scores; i++)
            System.out.printf("  %s: %.4f%n", lang(r.scores[i].lang), r.scores[i].score);

        // ── 4. set_scores ─────────────────────────────────────────────────────
        System.out.println("\n-- set_scores(2) --");
        lib.eldc_set_scores(2); // Default 3, Max 20, Min 1.
		  // We could reuse r, if we make sure to only read up to n_scores, but not thread-safe
        var r2 = new EldcDetectResult();
        lib.eldc_detect_details("Was ist das?", r2);
        System.out.printf("language: %s  n_scores: %d%n", lang(r2.lang), r2.n_scores); // de, 2
        lib.eldc_set_scores(3);

        // ── 5. set_languages ──────────────────────────────────────────────────
        System.out.println("\n-- set_languages --");
        System.out.println(lib.eldc_set_languages("en,fr,es"));   // en,fr,es
        System.out.println(lib.eldc_set_languages("eng,fra,xyz")); // en,fr (xyz→stderr)
        System.out.println(lib.eldc_detect("Bonjour le monde"));   // fr
        lib.eldc_set_languages("");                                 // reset
        System.out.println(lib.eldc_detect("Hola mundo"));         // es

        // ── 6. set_scheme ─────────────────────────────────────────────────────
        System.out.println("\n-- set_scheme --");
        lib.eldc_set_scheme("iso639-2t");
        System.out.println(lib.eldc_detect("Bonjour"));            // fra
        lib.eldc_set_scheme("iso639-1");
        System.out.println(lib.eldc_detect("Bonjour"));            // fr

        // ── 7. cleanup ────────────────────────────────────────────────────────
        lib.eldc_close();
    }
}
