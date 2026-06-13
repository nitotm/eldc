# demo_eldc_lib.rb — ELDC shared library demo for Ruby (ffi gem).
#
# gem install ffi
# Run: ruby demo_eldc_lib.rb

require 'ffi'

module ELD
  extend FFI::Library

  lib = case RUBY_PLATFORM
        when /mswin|mingw/ then 'eldc'
        when /darwin/      then File.join(__dir__, 'libeldc.dylib')
        else                    File.join(__dir__, 'libeldc.so')
        end
  ffi_lib lib

  MAX_SCORES = 20

  class ScoreItem < FFI::Struct
    layout :lang,  :pointer,
           :score, :float
    # 4-byte padding added automatically for 64-bit alignment
  end

  class DetectResult < FFI::Struct
    layout :lang,     :pointer,
           :reliable, :int,
           :n_scores, :int,
           :scores,   [ScoreItem, MAX_SCORES]
  end

  attach_function :eldc_init,           [],                           :void
  attach_function :eldc_close,          [],                           :void
  attach_function :eldc_detect,         [:string],                    :string
  attach_function :eldc_detect_details, [:string, DetectResult.by_ref], :string
  attach_function :eldc_set_languages,  [:string],                    :string
  attach_function :eldc_set_scheme,     [:string],                    :void
  attach_function :eldc_set_scores,     [:int],                       :void
  attach_function :eldc_set_faster,     [:int],                       :void

  def self.lang(ptr) = ptr.null? ? 'und' : ptr.read_string
end

# ── 0. set_faster (Not worth it. Call before init) ─────────────────────────
# ELD.eldc_set_faster(1)  # 64 MB table

# ── 1. init ──────────────────────────────────────────────────────────────────
ELD.eldc_init
puts "=== eldc Ruby demo ===\n\n"

# ── 2. detect ────────────────────────────────────────────────────────────────
puts "-- detect --"
puts ELD.eldc_detect("Bonjour le monde")  # fr
puts ELD.eldc_detect("12345 !@#")          # und

# ── 3. detect_details ────────────────────────────────────────────────────────
puts "\n-- detect_details --"
r = ELD::DetectResult.new
ELD.eldc_detect_details("Bonjour le monde", r)
puts "language : #{ELD.lang(r[:lang])}  reliable: #{r[:reliable] == 1}"
r[:n_scores].times { |i| puts "  #{ELD.lang(r[:scores][i][:lang])}: #{r[:scores][i][:score].round(4)}" }

# ── 4. set_scores ─────────────────────────────────────────────────────────────
puts "\n-- set_scores(2) --"
ELD.eldc_set_scores(2)  # Default 3, Max 20, Min 1.
r2 = ELD::DetectResult.new
ELD.eldc_detect_details("Was ist das?", r2)
puts "language: #{ELD.lang(r2[:lang])}  n_scores: #{r2[:n_scores]}"  # de, 2
ELD.eldc_set_scores(3)

# ── 5. set_languages ──────────────────────────────────────────────────────────
puts "\n-- set_languages --"
puts ELD.eldc_set_languages("en,fr,es")    # en,fr,es
puts ELD.eldc_set_languages("eng,fra,xyz") # en,fr  (xyz → stderr)
puts ELD.eldc_detect("Bonjour le monde")   # fr
ELD.eldc_set_languages("")                 # reset
puts ELD.eldc_detect("Hola mundo")         # es

# ── 6. set_scheme ─────────────────────────────────────────────────────────────
puts "\n-- set_scheme --"
ELD.eldc_set_scheme("iso639-2t")
puts ELD.eldc_detect("Bonjour")            # fra
ELD.eldc_set_scheme("iso639-1")
puts ELD.eldc_detect("Bonjour")            # fr

# ── 7. cleanup ────────────────────────────────────────────────────────────────
ELD.eldc_close
