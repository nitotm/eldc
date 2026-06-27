/**
 * demo_eldc_lib.java — ELDC shared library demo for Java (JNA).
 *
 * Dependency: jna-5.x.jar  (https://github.com/java-native-access/jna)
 * Compile & run:
 *   javac -cp jna.jar demo_eldc_lib.java
 *   java  -cp .:jna.jar -Djna.library.path=. demo_eldc_lib
 */

import com.sun.jna.*;
import java.util.Arrays;

public class demo_eldc_lib {

    static final int MAX_SCORES = 20;

    @Structure.FieldOrder({"language", "score"})
    public static class EldcScoreItem extends Structure {
        public Pointer language;   // const char* — read with .getString(0)
        public float   score;
    }

    @Structure.FieldOrder({"language", "reliable", "n_scores", "scores"})
    public static class EldcDetectResult extends Structure {
        public Pointer        language;
        public int            reliable;
        public int            n_scores;
        public EldcScoreItem[] scores = (EldcScoreItem[]) new EldcScoreItem().toArray(MAX_SCORES);
    }

    public interface EldcLib extends Library {
        EldcLib LIB = Native.load("eldc", EldcLib.class);

        // ── Global API ───────────────────────────────────────────────────────
        void    eldc_init();
        void    eldc_close();
        String  eldc_detect(String text);
        void    eldc_detect_details(String text, EldcDetectResult result);
        String  eldc_set_languages(String codes);
        void    eldc_set_scheme(String scheme);
        void    eldc_set_scores(int n);

        // ── Instance (cfg) API — EldcConfig is opaque, use Pointer ──────────
        Pointer eldc_config_create();
        void    eldc_config_free(Pointer cfg);
        String  eldc_detect_cfg(Pointer cfg, String text);
        void    eldc_detect_details_cfg(Pointer cfg, String text, EldcDetectResult result);
        String  eldc_set_languages_cfg(Pointer cfg, String codes);
        void    eldc_set_scheme_cfg(Pointer cfg, String scheme);
        void    eldc_set_scores_cfg(Pointer cfg, int n);
    }

    static String lang(Pointer p) { return p != null ? p.getString(0) : "und"; }

    public static void main(String[] args) {
        var lib = EldcLib.LIB;

        // ── 1. init ──────────────────────────────────────────────────────────
        lib.eldc_init();
        System.out.println("=== eldc Java demo ===\n");
	     // ── Optional: Isolated configuration instance ────────────────────────
		  var i_config = lib.eldc_config_create();

        // ── 2. detect ────────────────────────────────────────────────────────
        System.out.println("-- detect --");
        System.out.println(lib.eldc_detect("Bonjour le monde"));  // fr
        System.out.println(lib.eldc_detect("12345 !@#"));         // und
		  // Same behavior for instance protected configuration detect
		  System.out.println(lib.eldc_detect_cfg(i_config , "Bonjour"));  // fr

        // ── 3. detect_details ────────────────────────────────────────────────
        // eldc_detect_details fills the struct and returns void; read from r.
        System.out.println("\n-- detect_details --");
        var r = new EldcDetectResult();
        lib.eldc_detect_details("Bonjour le monde", r);
        System.out.printf("language : %s  reliable: %b%n", lang(r.language), r.reliable == 1);
        for (int i = 0; i < r.n_scores; i++)
            System.out.printf("  %s: %.4f%n", lang(r.scores[i].language), r.scores[i].score);

		  lib.eldc_detect_details_cfg(i_config, "Bonjour", r);  // Same behavior

        // ── 4. set_scores ─────────────────────────────────────────────────────
        System.out.println("\n-- set_scores(2) --");
        lib.eldc_set_scores(2);  // Default 3, Max 20, Min 1. Global setter
        var r2 = new EldcDetectResult();
        lib.eldc_detect_details("Was ist das?", r2);
        System.out.printf("language: %s  n_scores: %d%n", lang(r2.language), r2.n_scores);  // de, 2
        lib.eldc_set_scores(3);  // reset

		  lib.eldc_set_scores_cfg(i_config, 2);  // instance setter

        // ── 5. set_languages ──────────────────────────────────────────────────
        System.out.println("\n-- set_languages --");
        System.out.println(lib.eldc_set_languages("en,fr,es"));    // en,fr,es  Global setter
        System.out.println(lib.eldc_set_languages("eng,fra,xyz")); // en,fr (xyz → stderr)
        System.out.println(lib.eldc_detect("Bonjour le monde"));   // fr
        lib.eldc_set_languages("");                                // reset
        System.out.println(lib.eldc_detect("Hola doctor"));        // es

		  lib.eldc_set_languages_cfg(i_config, "en,fr,es");          // instance setter

        // ── 6. set_scheme ─────────────────────────────────────────────────────
        System.out.println("\n-- set_scheme --");
        lib.eldc_set_scheme("iso639-2t");                // Global setter
        System.out.println(lib.eldc_detect("Bonjour"));  // fra
        lib.eldc_set_scheme("iso639-1");
        System.out.println(lib.eldc_detect("Bonjour"));  // fr

		  lib.eldc_set_scheme_cfg(i_config, "iso639-2t");  // instance setteR

        // ── 7. cleanup ────────────────────────────────────────────────────────
        lib.eldc_config_free(i_config);
        // Global unload
        lib.eldc_close();
    }
}
