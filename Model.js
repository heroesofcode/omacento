.pragma library

// Shared, UI-free logic for Omacento.
//
// Panel.qml owns pixels; this file owns the defaults, the labels and the
// command handed to omacento-apply. The script stays the only thing that
// writes configuration, so the panel and the CLI cannot disagree.

// Mirrors manifest.json's barWidget.defaults and the DEFAULTS block in
// omacento-apply.
//
// Repeated on purpose: the shell hands a freshly enabled widget an empty
// settings object, so reading the manifest alone would write a half-configured
// addon on first run and only come good once the user touched something.
// Mirrors the presets in addon/src/presets.cpp, which is the source of truth.
// If the two ever drift the addon warns and falls back to ptbr rather than
// leaving someone with no accents at all.
var LANGUAGES = [
  { value: "ptbr",   label: "Português" },
  { value: "es",     label: "Español" },
  { value: "fr",     label: "Français" },
  { value: "de",     label: "Deutsch" },
  { value: "it",     label: "Italiano" },
  { value: "pl",     label: "Polski" },
  { value: "tr",     label: "Türkçe" },
  { value: "nordic", label: "Nordisk" },
  { value: "en",     label: "English (intl.)" }
]

var DEFAULTS = {
  enabled: true,

  // Which accent set, and in which order. Every language can reach the same
  // characters; what changes is which ones land on 1 and 2.
  language: "ptbr",

  // The addon runs its own millisecond timer, so this is a real hold time and
  // touches nothing else. macOS sits around 500ms; 250 feels quick without
  // firing during ordinary typing.
  holdTime: 250,

  vertical: false,
  fontSize: 14,
  font: "",          // empty inherits the terminal's family
  themeSync: true,
  blocklist: []
}

function labelForLanguage(value) {
  for (var i = 0; i < LANGUAGES.length; i++) {
    if (LANGUAGES[i].value === value) return LANGUAGES[i].label
  }
  return value
}

function defaultFor(key) {
  return DEFAULTS.hasOwnProperty(key) ? DEFAULTS[key] : ""
}

// Whole settings object with every key resolved. `get` is a one-argument
// function returning the effective value for a key, so this stays free of QML.
function settingsFor(get) {
  var out = {}
  for (var key in DEFAULTS) {
    if (DEFAULTS.hasOwnProperty(key)) out[key] = get(key)
  }
  return out
}

function commandFor(scriptPath, get) {
  return ["bash", scriptPath, JSON.stringify(settingsFor(get))]
}

// One-line description for the bar tooltip.
function summary(get) {
  if (!get("enabled")) return "Accents off"
  var bits = [labelForLanguage(get("language")), get("holdTime") + "ms hold"]
  var blocked = get("blocklist")
  if (blocked && blocked.length) bits.push(blocked.length + " app" + (blocked.length > 1 ? "s" : "") + " blocked")
  return "Hold a vowel · " + bits.join(" · ")
}
