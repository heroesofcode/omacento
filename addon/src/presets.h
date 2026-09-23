#ifndef _OMACENTO_PRESETS_H_
#define _OMACENTO_PRESETS_H_

#include <cstdint>
#include <string>
#include <vector>

namespace omacento {

struct Preset {
    std::string id;
    std::string label;
    // One entry per key: the base character, then its variants, space
    // separated. Lowercase only -- the uppercase half of the table is derived,
    // so adding a language cannot leave Shift half-working.
    std::vector<std::string> entries;
};

const std::vector<Preset> &presets();
const Preset *presetFor(const std::string &id);

/// Uppercase a single codepoint. Covers Latin-1 Supplement and Latin Extended-A
/// by rule, with an explicit table for the pairs those rules get wrong. Returns
/// the input unchanged when the character has no uppercase form.
uint32_t upperCodepoint(uint32_t cp);

} // namespace omacento

#endif // _OMACENTO_PRESETS_H_
