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
    layout :language, :pointer,
           :score,    :float
  end

  class DetectResult < FFI::Struct
    layout :language, :pointer,
           :reliable, :int,
           :n_scores, :int,
           :scores,   [ScoreItem, MAX_SCORES]
  end

  # ── Global API ──────────────────────────────────────────────────────────────
  attach_function :eldc_init,           [],                              :void
  attach_function :eldc_close,          [],                              :void
  attach_function :eldc_detect,         [:string],                       :string
  attach_function :eldc_detect_details, [:string, DetectResult.by_ref],  :void
  attach_function :eldc_set_languages,  [:string],                       :string
  attach_function :eldc_set_scheme,     [:string],                       :void
  attach_function :eldc_set_scores,     [:int],                          :void

  # ── Instance (cfg) API — EldcConfig is opaque, use :pointer ─────────────────
  attach_function :eldc_config_create,       [],                                       :pointer
  attach_function :eldc_config_free,         [:pointer],                               :void
  attach_function :eldc_detect_cfg,          [:pointer, :string],                      :string
  attach_function :eldc_detect_details_cfg,  [:pointer, :string, DetectResult.by_ref], :void
  attach_function :eldc_set_languages_cfg,   [:pointer, :string],                      :string
  attach_function :eldc_set_scheme_cfg,      [:pointer, :string],                      :void
  attach_function :eldc_set_scores_cfg,      [:pointer, :int],                         :void

  def self.lang(ptr) = ptr.null? ? 'und' : ptr.read_string
end

# ── 1. init ──────────────────────────────────────────────────────────────────
ELD.eldc_init
# ── Optional: Isolated configuration instance ────────────────────────────────
i_config = ELD.eldc_config_create

puts "=== eldc Ruby demo ===\n\n"

# ── 2. detect ────────────────────────────────────────────────────────────────
puts "-- detect --"
puts ELD.eldc_detect("Bonjour le monde")   # fr
puts ELD.eldc_detect("12345 !@#")          # und

puts ELD.eldc_detect_cfg(i_config, "Bonjour") # fr, Same behavior

# ── 3. detect_details ────────────────────────────────────────────────────────
# eldc_detect_details fills the struct and returns void; read result from r.
puts "\n-- detect_details --"
r = ELD::DetectResult.new
ELD.eldc_detect_details("Bonjour le monde", r)
puts "language : #{ELD.lang(r[:language])}  reliable: #{r[:reliable] == 1}"
r[:n_scores].times { |i| puts "  #{ELD.lang(r[:scores][i][:language])}: #{r[:scores][i][:score].round(4)}" }

ELD.eldc_detect_details_cfg(i_config, "Bonjour") # Same behavior

# ── 4. set_scores ─────────────────────────────────────────────────────────────
puts "\n-- set_scores(2) --"
ELD.eldc_set_scores(2)   # Default 3, Max 20, Min 1. Global setter
r2 = ELD::DetectResult.new
ELD.eldc_detect_details("Was ist das?", r2)
puts "language: #{ELD.lang(r2[:language])}  n_scores: #{r2[:n_scores]}"   # de, 2
ELD.eldc_set_scores(3)   # reset

ELD.eldc_set_scores_cfg(i_config, 2) # instance setter

# ── 5. set_languages ──────────────────────────────────────────────────────────
puts "\n-- set_languages --"
puts ELD.eldc_set_languages("en,fr,es")    # en,fr,es  Global setter
puts ELD.eldc_set_languages("eng,fra,xyz") # en,fr  (xyz → stderr)
puts ELD.eldc_detect("Bonjour le monde")   # fr
ELD.eldc_set_languages("")                 # reset
puts ELD.eldc_detect("Hola doctor")        # es

ELD.eldc_set_languages_cfg(i_config, "en,fr,es") # instance setter

# ── 6. set_scheme ─────────────────────────────────────────────────────────────
puts "\n-- set_scheme --"
ELD.eldc_set_scheme("iso639-2t")               # Global setter
puts ELD.eldc_detect("Bonjour")                # fra
ELD.eldc_set_scheme("iso639-1")
puts ELD.eldc_detect("Bonjour")                # fr

ELD.eldc_set_scheme_cfg(i_config, "iso639-2t") # instance setter

# ── 7. cleanup ───────────────────────────────────────────────────────────────
ELD.eldc_config_free(i_config)
# Global unload
ELD.eldc_close
