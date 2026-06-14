// demo_eldc_lib.cs — ELDC shared library demo for .NET / C# (P/Invoke).
//
// Run: dotnet script demo_eldc_lib.cs
//  or: dotnet run  (in a project with <AllowUnsafeBlocks>true</AllowUnsafeBlocks>)
//
// On Linux/macOS the library must be named libeldc.so / libeldc.dylib;
// set the DLL map in runtimes/ or use NativeLibrary.SetDllImportResolver if needed.

using System;
using System.Runtime.InteropServices;

const int MAX_SCORES = 20;
const string LIB     = "eldc";   // resolves to eldc.dll / libeldc.so / libeldc.dylib

[StructLayout(LayoutKind.Sequential)]
struct EldcScoreItem {
    public IntPtr Language;   // const char* — decode with Marshal.PtrToStringAnsi
    public float  Score;
    // 4-byte padding added automatically to align the next array element
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
    [DllImport(LIB, EntryPoint="eldc_init")]          public static extern void   Init();
    [DllImport(LIB, EntryPoint="eldc_close")]         public static extern void   Close();
    [DllImport(LIB, EntryPoint="eldc_detect")]        public static extern IntPtr Detect(string text);
    [DllImport(LIB, EntryPoint="eldc_detect_details")]public static extern IntPtr DetectDetails(string text, ref EldcDetectResult r);
    [DllImport(LIB, EntryPoint="eldc_set_languages")] public static extern IntPtr SetLanguages(string codes);
    [DllImport(LIB, EntryPoint="eldc_set_scheme")]    public static extern void   SetScheme(string scheme);
    [DllImport(LIB, EntryPoint="eldc_set_scores")]    public static extern void   SetScores(int n);
    [DllImport(LIB, EntryPoint="eldc_set_faster")]    public static extern void   SetFaster(int flag);
}

static string S(IntPtr p) => p != IntPtr.Zero ? Marshal.PtrToStringAnsi(p)! : "und";

// ── 0. set_faster (Not worth it. Call before init()) ─────────────────────────
// Eldc.SetFaster(1);  // 64 MB table (32MB default) Minimal speedup.

// ── 1. init ──────────────────────────────────────────────────────────────────
Eldc.Init();
Console.WriteLine("=== eldc .NET demo ===\n");

// ── 2. detect ────────────────────────────────────────────────────────────────
Console.WriteLine("-- detect --");
Console.WriteLine(S(Eldc.Detect("Bonjour le monde")));  // fr
Console.WriteLine(S(Eldc.Detect("12345 !@#")));          // und

// ── 3. detect_details ────────────────────────────────────────────────────────
Console.WriteLine("\n-- detect_details --");
var r = new EldcDetectResult();
Eldc.DetectDetails("Bonjour le monde", ref r);
Console.WriteLine($"language : {S(r.Language)}  reliable: {r.Reliable == 1}");
for (int i = 0; i < r.NScores; i++)
    Console.WriteLine($"  {S(r.Scores[i].Language)}: {r.Scores[i].Score:F4}");

// ── 4. set_scores ─────────────────────────────────────────────────────────────
Console.WriteLine("\n-- set_scores(2) --");
Eldc.SetScores(2); // Default 3, Max 20, Min 1.
var r2 = new EldcDetectResult();
Eldc.DetectDetails("Was ist das?", ref r2);
Console.WriteLine($"language: {S(r2.Language)}  n_scores: {r2.NScores}");  // de, 2
Eldc.SetScores(3);

// ── 5. set_languages ──────────────────────────────────────────────────────────
Console.WriteLine("\n-- set_languages --");
Console.WriteLine(S(Eldc.SetLanguages("en,fr,es")));    // en,fr,es
Console.WriteLine(S(Eldc.SetLanguages("eng,fra,xyz"))); // en,fr  (xyz → stderr)
Console.WriteLine(S(Eldc.Detect("Bonjour le monde")));  // fr
Eldc.SetLanguages("");                                  // reset
Console.WriteLine(S(Eldc.Detect("Hola mundo")));        // es

// ── 6. set_scheme ─────────────────────────────────────────────────────────────
Console.WriteLine("\n-- set_scheme --");
Eldc.SetScheme("iso639-2t");
Console.WriteLine(S(Eldc.Detect("Bonjour")));           // fra
Eldc.SetScheme("iso639-1");
Console.WriteLine(S(Eldc.Detect("Bonjour")));           // fr

// ── 7. cleanup ────────────────────────────────────────────────────────────────
Eldc.Close();
