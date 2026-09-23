#include "presets.h"

#include <unordered_map>

namespace omacento {
namespace {

// Ordering is the whole point of having more than one of these: every language
// here can reach the same characters, but the ones a writer of that language
// actually reaches for have to land on 1 and 2. fcitx5's own table is ordered
// for English and German, which buries the tilde in sixth place -- unusable
// for Portuguese.
const std::vector<Preset> &table() {
    static const std::vector<Preset> p = {
        {"ptbr",
         "Português",
         {"a á ã â à ä å ā æ", "e é ê è ë ē ę", "i í î ì ï ī",
          "o ó õ ô ò ö ø ō œ", "u ú û ù ü ū", "c ç ć č", "n ñ ń ň", "y ý ÿ",
          "s š ś ş", "z ž ź ż"}},

        {"es",
         "Español",
         {"a á à ä â ã å", "e é è ë ê ē", "i í ì ï î", "o ó ò ö ô õ ø",
          "u ú ü ù û", "n ñ ń", "c ç ć", "y ý ÿ"}},

        {"fr",
         "Français",
         {"a à â á ä ã æ å", "e é è ê ë ē ę", "i î ï í ì ī", "o ô ö ó ò õ œ ø",
          "u ù û ü ú", "c ç ć", "y ÿ ý", "n ñ ń"}},

        {"de",
         "Deutsch",
         {"a ä à á â ã å æ", "o ö ó ò ô õ ø œ", "u ü ù ú û", "e é è ê ë ē",
          "s ß ś š ş", "i í ì î ï", "c ç ć č", "n ñ ń"}},

        {"it",
         "Italiano",
         {"a à á â ä ã", "e è é ê ë", "i ì í î ï", "o ò ó ô ö õ", "u ù ú û ü",
          "c ç ć", "n ñ"}},

        {"pl",
         "Polski",
         {"a ą á à â ä", "c ć ç č", "e ę é è ê ë", "l ł", "n ń ñ ň",
          "o ó ö ô ò õ", "s ś š ş", "z ż ź ž"}},

        {"tr",
         "Türkçe",
         {"a â á à ä ã", "c ç ć č", "g ğ", "i ı í î ï ì", "o ö ó ô ò õ",
          "s ş ś š", "u ü ú û ù", "e é è ê ë"}},

        {"nordic",
         "Nordisk",
         {"a å ä á à â ã æ", "o ø ö ó ò ô õ œ", "e é è ê ë", "u ü ú ù û",
          "i í ì î ï", "s š ś", "n ñ ń", "y ý ÿ"}},

        {"en",
         "English (international)",
         {"a à á â ä ã å ā æ", "e è é ê ë ē ę", "i ì í î ï ī",
          "o ò ó ô ö õ ø ō œ", "u ù ú û ü ū", "c ç ć č", "n ñ ń ň", "y ý ÿ",
          "s š ś ş", "z ž ź ż"}},
    };
    return p;
}

// Where the two rules below give the wrong answer. Keeping this small and
// explicit beats pulling in ICU for a few dozen letters.
const std::unordered_map<uint32_t, uint32_t> &exceptions() {
    static const std::unordered_map<uint32_t, uint32_t> e = {
        {0x00DF, 0x1E9E}, // ß -> ẞ
        {0x00FF, 0x0178}, // ÿ -> Ÿ
        {0x0131, 0x0049}, // ı -> I  (dotless i; Turkish would want İ for i)
        {0x017F, 0x0053}, // ſ -> S
        {0x0138, 0x0138}, // ĸ has no uppercase
        {0x0149, 0x0149}, // ŉ has no single-codepoint uppercase
    };
    return e;
}

} // namespace

const std::vector<Preset> &presets() { return table(); }

const Preset *presetFor(const std::string &id) {
    for (const auto &p : table()) {
        if (p.id == id) {
            return &p;
        }
    }
    return nullptr;
}

uint32_t upperCodepoint(uint32_t cp) {
    if (const auto it = exceptions().find(cp); it != exceptions().end()) {
        return it->second;
    }
    if (cp >= 'a' && cp <= 'z') {
        return cp - 0x20;
    }
    // Latin-1 Supplement: lowercase block sits 0x20 above its uppercase, with
    // ÷ punched out of the middle of it.
    if (cp >= 0x00E0 && cp <= 0x00FE && cp != 0x00F7) {
        return cp - 0x20;
    }
    // Latin Extended-A is laid out in case pairs, but which half of the pair
    // is uppercase flips twice across the block. Getting this wrong is quiet
    // and selective -- a single parity rule uppercases ć and š correctly while
    // leaving ł, ź, ż and ž untouched, so Polish breaks and nothing else does.
    const bool upperIsEven = (cp >= 0x0100 && cp <= 0x0137) ||
                             (cp >= 0x014A && cp <= 0x0177);
    const bool upperIsOdd = (cp >= 0x0139 && cp <= 0x0148) ||
                            (cp >= 0x0179 && cp <= 0x017E);
    if ((upperIsEven && (cp & 1)) || (upperIsOdd && !(cp & 1))) {
        return cp - 1;
    }
    return cp;
}

} // namespace omacento
