// demo_eldc_lib.cs — ELDC shared library demo for .NET / C# (P/Invoke).
//
// Run: dotnet script demo_eldc_lib.cs
//  or: dotnet run  (in a project with <AllowUnsafeBlocks>true</AllowUnsafeBlocks>)
//
// On Linux/macOS set DllMap or NativeLibrary.SetDllImportResolver if needed.

using System;
using System.Runtime.InteropServices;

const int MAX_SCORES = 20;
const string LIB     = "eldc";   // resolves to eldc.dll / libeldc.so / libeldc.dylib

[StructLayout(LayoutKind.Sequential)]
struct EldcScoreItem {
    public IntPtr Language;   // const char* — read with Marshal.PtrToStringAnsi
    public float  Score;
}

[StructLayout(LayoutKind.Sequential)]
struct EldcDetectResult {
    public IntPtr Language;
    public int    Reliable;
    public int    NScores;
    [MarshalAs(UnmanagedType.ByValArray, SizeConst = MAX_SCORES)]
    public EldcScoreItem[] Scores;
}

static class Eldc {
    // ── Global API ───────────────────────────────────────────────────────────
    [DllImport(LIB, EntryPoint="eldc_init")]           public static extern void   Init();
    [DllImport(LIB, EntryPoint="eldc_close")]          public static extern void   Close();
    [DllImport(LIB, EntryPoint="eldc_detect")]         public static extern IntPtr Detect(string text);
    [DllImport(LIB, EntryPoint="eldc_detect_details")] public static extern void   DetectDetails(string text, ref EldcDetectResult r);
    [DllImport(LIB, EntryPoint="eldc_set_languages")]  public static extern IntPtr SetLanguages(string codes);
    [DllImport(LIB, EntryPoint="eldc_set_scheme")]     public static extern void   SetScheme(string scheme);
    [DllImport(LIB, EntryPoint="eldc_set_scores")]     public static extern void   SetScores(int n);

    // ── Instance (cfg) API — EldcConfig is opaque, use IntPtr ────────────────
    [DllImport(LIB, EntryPoint="eldc_config_create")]       public static extern IntPtr ConfigCreate();
    [DllImport(LIB, EntryPoint="eldc_config_free")]         public static extern void   ConfigFree(IntPtr cfg);
    [DllImport(LIB, EntryPoint="eldc_detect_cfg")]          public static extern IntPtr DetectCfg(IntPtr cfg, string text);
    [DllImport(LIB, EntryPoint="eldc_detect_details_cfg")]  public static extern void   DetectDetailsCfg(IntPtr cfg, string text, ref EldcDetectResult r);
    [DllImport(LIB, EntryPoint="eldc_set_languages_cfg")]   public static extern IntPtr SetLanguagesCfg(IntPtr cfg, string codes);
    [DllImport(LIB, EntryPoint="eldc_set_scheme_cfg")]      public static extern void   SetSchemeCfg(IntPtr cfg, string scheme);
    [DllImport(LIB, EntryPoint="eldc_set_scores_cfg")]      public static extern void   SetScoresCfg(IntPtr cfg, int n);
}

static string S(IntPtr p) => p != IntPtr.Zero ? Marshal.PtrToStringAnsi(p)! : "und";

// ── 1. init ──────────────────────────────────────────────────────────────────
Eldc.Init();
Console.WriteLine("=== eldc .NET demo ===\n");
// ── Optional: Isolated configuration instance ─────────────────────────────────
var iConfig = Eldc.ConfigCreate();

// ── 2. detect ────────────────────────────────────────────────────────────────
Console.WriteLine("-- detect --");
Console.WriteLine(S(Eldc.Detect("Bonjour le monde")));  // fr
Console.WriteLine(S(Eldc.Detect("12345 !@#")));         // und

Console.WriteLine(S(Eldc.DetectCfg(iConfig, "Bonjour")));  // fr, Same behavior

// ── 3. detect_details ────────────────────────────────────────────────────────
// DetectDetails fills the struct and returns void; read result from r.
Console.WriteLine("\n-- detect_details --");
var r = new EldcDetectResult();
Eldc.DetectDetails("Bonjour le monde", ref r);
Console.WriteLine($"language : {S(r.Language)}  reliable: {r.Reliable == 1}");
for (int i = 0; i < r.NScores; i++)
    Console.WriteLine($"  {S(r.Scores[i].Language)}: {r.Scores[i].Score:F4}");
 
Eldc.DetectDetailsCfg(iConfig, "Bonjour", ref r);  // Same behavior

// ── 4. set_scores ─────────────────────────────────────────────────────────────
Console.WriteLine("\n-- set_scores(2) --");
Eldc.SetScores(2);  // Default 3, Max 20, Min 1. Global setter
var r2 = new EldcDetectResult();
Eldc.DetectDetails("Was ist das?", ref r2);
Console.WriteLine($"language: {S(r2.Language)}  n_scores: {r2.NScores}");  // de, 2
Eldc.SetScores(3);  // reset

Eldc.SetScoresCfg(iConfig, 2); // instance setter

// ── 5. set_languages ──────────────────────────────────────────────────────────
Console.WriteLine("\n-- set_languages --")
Console.WriteLine(S(Eldc.SetLanguages("en,fr,es")));    // en,fr,es  Global setter
Console.WriteLine(S(Eldc.SetLanguages("eng,fra,xyz"))); // en,fr  (xyz → stderr)
Console.WriteLine(S(Eldc.Detect("Bonjour le monde")));  // fr
Eldc.SetLanguages("");                                  // reset
Console.WriteLine(S(Eldc.Detect("Hola doctor")));       // es

Console.WriteLine(S(Eldc.SetLanguagesCfg(iConfig, "en,fr,es"))); // instance setter

// ── 6. set_scheme ─────────────────────────────────────────────────────────────
Console.WriteLine("\n-- set_scheme --");
Eldc.SetScheme("iso639-2t");                    // global setter
Console.WriteLine(S(Eldc.Detect("Bonjour")));   // fra
Eldc.SetScheme("iso639-1");
Console.WriteLine(S(Eldc.Detect("Bonjour")));   // fr

Console.WriteLine(S(Eldc.SetSchemeCfg(iConfig, "iso639-2t"))); // instance setter

// ── 7. cleanup ───────────────────────────────────────────────────────────────
Eldc.ConfigFree(iConfig);
// Global unload
Eldc.Close();
