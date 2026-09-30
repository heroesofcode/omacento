#include "omacento.h"

#include <ctime>

#include <fcitx-utils/keysymgen.h>
#include <fcitx-utils/log.h>
#include <fcitx-utils/stringutils.h>
#include <fcitx-utils/utf8.h>
#include <fcitx/userinterface.h>

namespace omacento {
namespace {

constexpr char kConfigPath[] = "conf/omacento.conf";

// Shift and the lock keys are part of producing the letter; anything else means
// the key is a shortcut and is none of our business.
bool modifiersAllow(const fcitx::Key &key) {
    const fcitx::KeyStates blocking = fcitx::KeyStates(fcitx::KeyState::Ctrl) |
                                      fcitx::KeyState::Alt |
                                      fcitx::KeyState::Super |
                                      fcitx::KeyState::Hyper |
                                      fcitx::KeyState::Mod5;
    return (key.states() & blocking).toInteger() == 0;
}

// The key being held, recognised by keycode where the frontend reports one.
// The keysym is not stable across a press: let go of Shift before the letter
// and a key that went down as "A" comes back up as "a".
bool sameKey(const fcitx::Key &held, const fcitx::Key &key) {
    if (held.code() != 0 && key.code() != 0) {
        return held.code() == key.code();
    }
    return held.sym() == key.sym();
}

// Whether the modifiers that decide a letter's case are what they were when it
// went down.
bool sameCase(const fcitx::Key &held, const fcitx::Key &now) {
    const fcitx::KeyStates caseStates =
        fcitx::KeyStates(fcitx::KeyState::Shift) | fcitx::KeyState::CapsLock;
    return (held.states() & caseStates).toInteger() ==
           (now.states() & caseStates).toInteger();
}

fcitx::CommonCandidateList *candidates(fcitx::InputContext *ic) {
    return dynamic_cast<fcitx::CommonCandidateList *>(
        ic->inputPanel().candidateList().get());
}

} // namespace

Omacento::Omacento(fcitx::Instance *instance)
    : instance_(instance),
      factory_([](fcitx::InputContext &) { return new OmacentoState; }) {
    instance_->inputContextManager().registerProperty("omacentoState",
                                                      &factory_);
    reloadConfig();
    keyHandler_ = instance_->watchEvent(
        fcitx::EventType::InputContextKeyEvent,
        fcitx::EventWatcherPhase::PreInputMethod, [this](fcitx::Event &event) {
            onKeyEvent(static_cast<fcitx::KeyEvent &>(event));
        });
    // A focus change cancels whatever is held, with nothing committed and
    // nothing forwarded. The field the letter was meant for is gone, and on
    // Wayland a single input context serves every field, so anything sent now
    // would land in the next one -- and a popup left open would hand its
    // letter to the first key typed there.
    focusOutHandler_ = instance_->watchEvent(
        fcitx::EventType::InputContextFocusOut,
        fcitx::EventWatcherPhase::Default, [this](fcitx::Event &event) {
            auto *ic =
                static_cast<fcitx::InputContextEvent &>(event).inputContext();
            auto *state = ic->propertyFor(&factory_);
            if (state->phase != Phase::Idle) {
                reset(ic, state);
            }
        });
}

Omacento::~Omacento() = default;

void Omacento::reloadConfig() {
    fcitx::readAsIni(config_, kConfigPath);
    applyConfig();
}

void Omacento::setConfig(const fcitx::RawConfig &raw) {
    config_.load(raw, true);
    fcitx::safeSaveAsIni(config_, kConfigPath);
    applyConfig();
}

void Omacento::applyConfig() {
    holdUsec_ = static_cast<uint64_t>(*config_.holdTime) * 1000ULL;

    std::vector<std::string> entries;
    std::string source;
    if (!config_.table->empty()) {
        entries = *config_.table;
        source = "custom";
    } else if (const Preset *p = presetFor(*config_.language)) {
        entries = p->entries;
        source = p->id;
    } else {
        // An unknown language must not leave the user with no accents at all.
        entries = presets().front().entries;
        source = presets().front().id + " (fallback)";
        FCITX_WARN() << "omacento: unknown language '" << *config_.language
                     << "', falling back to " << presets().front().id;
    }

    table_.clear();
    for (const auto &entry : entries) {
        auto parts = fcitx::stringutils::split(entry, " \t");
        if (parts.size() < 2) {
            continue;
        }
        // Keyed by the character the key produces, not by its keysym, so a
        // base character outside ASCII works the same as one inside it.
        const uint32_t base = fcitx::utf8::getChar(parts[0]);
        if (!fcitx::utf8::isValidChar(base)) {
            continue;
        }
        Variants lower(parts.begin() + 1, parts.end());
        table_[base] = lower;

        // Derive the uppercase half rather than asking every preset to repeat
        // itself. A variant with no uppercase form is kept as it is.
        const uint32_t upperBase = upperCodepoint(base);
        if (upperBase == base) {
            continue;
        }
        Variants upper;
        upper.reserve(lower.size());
        for (const auto &v : lower) {
            const uint32_t cp = fcitx::utf8::getChar(v);
            upper.push_back(fcitx::utf8::isValidChar(cp)
                                ? fcitx::utf8::UCS4ToUTF8(upperCodepoint(cp))
                                : v);
        }
        table_.emplace(upperBase, std::move(upper));
    }

    FCITX_INFO() << "omacento: hold " << *config_.holdTime << "ms, "
                 << source << ", " << table_.size() << " keys, "
                 << (*config_.enabled ? "enabled" : "disabled");
}

const Variants *Omacento::lookup(uint32_t sym) const {
    auto it = table_.find(sym);
    return it == table_.end() ? nullptr : &it->second;
}

bool Omacento::blocked(fcitx::InputContext *ic) const {
    const auto &program = ic->program();
    if (program.empty()) {
        return false;
    }
    for (const auto &entry : *config_.blocklist) {
        if (entry == program) {
            return true;
        }
    }
    return false;
}

void Omacento::setPreedit(fcitx::InputContext *ic, const std::string &text) {
    fcitx::Text t(text, fcitx::TextFormatFlag::Underline);
    t.setCursor(static_cast<int>(text.size()));
    auto &panel = ic->inputPanel();
    ic->propertyFor(&factory_)->preeditShown = true;
    if (ic->capabilityFlags().test(fcitx::CapabilityFlag::Preedit)) {
        panel.setClientPreedit(t);
    } else {
        panel.setPreedit(t);
    }
    ic->updatePreedit();
    ic->updateUserInterface(fcitx::UserInterfaceComponent::InputPanel);
}

void Omacento::reset(fcitx::InputContext *ic, OmacentoState *state) {
    state->disarm();
    state->phase = Phase::Idle;
    state->heldKey = fcitx::Key();
    state->base.clear();
    state->variants.clear();
    // Only touch the panel if we actually put something in it. Ordinary typing
    // never opens a popup, and an empty preedit update per keystroke is churn
    // that editors with their own caret model (Google Docs) mishandle.
    if (state->preeditShown) {
        state->preeditShown = false;
        ic->inputPanel().reset();
        ic->updatePreedit();
        ic->updateUserInterface(fcitx::UserInterfaceComponent::InputPanel);
    }
}

void Omacento::commitAndReset(fcitx::InputContext *ic, OmacentoState *state,
                              std::string text) {
    // Commit before withdrawing the preedit, not after. The other order makes
    // the client delete the composition and then receive an unrelated
    // insertion, which is what moves the caret in Google Docs.
    ic->commitString(text);
    reset(ic, state);
}

bool Omacento::deliverTap(fcitx::InputContext *ic, OmacentoState *state,
                          const fcitx::Key &now, int time) {
    // A tap goes back to the application as the key itself, not as the letter
    // it produces, which makes it indistinguishable from a key this addon does
    // not handle at all. That matters wherever the keydown itself means
    // something: a selected Google Sheets cell starts editing on a real
    // keydown, and a committed string arrives without one and is dropped
    // (issue #10).
    //
    // Only the press is forwarded; the release is the user's own and is let
    // through behind it. The D-Bus and XIM frontends forward exactly what they
    // are given, so filtering the real release would leave the key down in the
    // client. The Wayland frontend adds a release to every forwarded press, so
    // there the client gets one release too many -- a release for a key it
    // never saw go down, which is what fcitx5 already sends after every key
    // when PreferKeyEvent is off, so clients cope.
    //
    // The exception is a change of case between press and release. The Wayland
    // frontend replays a forwarded key under the modifiers held now, not the
    // ones it went down with, so let go of Shift before the letter and "A"
    // would arrive as "a". Then the letter is committed as text, as every tap
    // used to be.
    if (sameCase(state->heldKey, now)) {
        const fcitx::Key key = state->heldKey;
        reset(ic, state);
        ic->forwardKey(key, false, time);
        return true;
    }
    commitAndReset(ic, state, state->base);
    return false;
}

void Omacento::arm(fcitx::InputContext *ic, OmacentoState *state) {
    auto ref = ic->watch();
    state->timer = instance_->eventLoop().addTimeEvent(
        CLOCK_MONOTONIC, fcitx::now(CLOCK_MONOTONIC) + holdUsec_, 0,
        [this, ref](fcitx::EventSourceTime *, uint64_t) {
            if (auto *ic = ref.get()) {
                auto *state = ic->propertyFor(&factory_);
                if (state->phase == Phase::Pending) {
                    openPicker(ic, state);
                }
            }
            return false;
        });
}

void Omacento::openPicker(fcitx::InputContext *ic, OmacentoState *state) {
    // Deliberately not disarm(): this runs inside the timer's own callback, and
    // destroying the event source from there is a use-after-free. The source is
    // spent; reset() frees it later, from a key handler.
    state->phase = Phase::Picking;

    auto list = std::make_unique<fcitx::CommonCandidateList>();
    list->setPageSize(static_cast<int>(state->variants.size()));
    list->setLayoutHint(fcitx::CandidateLayoutHint::NotSet);

    list->setLabels({});
    for (size_t i = 0; i < state->variants.size(); ++i) {
        list->append<fcitx::DisplayOnlyCandidateWord>(
            fcitx::Text(state->variants[i] + "\n" + std::to_string(i + 1)));
    }
    list->setGlobalCursorIndex(0);
    ic->inputPanel().setCandidateList(std::move(list));

    setPreedit(ic, state->base);
}

void Omacento::onKeyEvent(fcitx::KeyEvent &event) {
    auto *ic = event.inputContext();
    auto *state = ic->propertyFor(&factory_);
    const fcitx::Key key = event.key();
    const uint32_t sym = static_cast<uint32_t>(key.sym());

    if (state->phase == Phase::Picking) {
        if (event.isRelease()) {
            // The popup outlives the release, as it does on macOS.
            if (sameKey(state->heldKey, event.rawKey())) {
                event.filterAndAccept();
            }
            return;
        }
        if (sameKey(state->heldKey, event.rawKey())) {
            // Still holding. Auto-repeat must neither retype the letter behind
            // the open popup nor close it.
            event.filterAndAccept();
            return;
        }
        auto *list = candidates(ic);
        // Bound on our own vector, never on the candidate list: the list lives
        // in the shared input panel and anything else may have replaced it,
        // which would turn a digit press into an out-of-bounds read here.
        const int total = static_cast<int>(state->variants.size());

        if (sym >= '1' && sym <= '9') {
            const int idx = static_cast<int>(sym - '1');
            if (idx < total) {
                commitAndReset(ic, state, state->variants[idx]);
                event.filterAndAccept();
                return;
            }
        }
        if (key.check(FcitxKey_Escape) || key.check(FcitxKey_BackSpace)) {
            commitAndReset(ic, state, state->base);
            event.filterAndAccept();
            return;
        }
        if (key.check(FcitxKey_Return) || key.check(FcitxKey_KP_Enter) ||
            key.check(FcitxKey_space)) {
            const int cursor = list ? list->globalCursorIndex() : -1;
            commitAndReset(ic, state,
                           (cursor >= 0 && cursor < total)
                               ? state->variants[static_cast<size_t>(cursor)]
                               : state->base);
            event.filterAndAccept();
            return;
        }
        if (list && total > 0 &&
            (key.check(FcitxKey_Right) || key.check(FcitxKey_Tab))) {
            list->setGlobalCursorIndex((list->globalCursorIndex() + 1) % total);
            ic->updateUserInterface(fcitx::UserInterfaceComponent::InputPanel);
            event.filterAndAccept();
            return;
        }
        if (list && total > 0 &&
            (key.check(FcitxKey_Left) || key.check(FcitxKey_ISO_Left_Tab))) {
            list->setGlobalCursorIndex(
                (list->globalCursorIndex() + total - 1) % total);
            ic->updateUserInterface(fcitx::UserInterfaceComponent::InputPanel);
            event.filterAndAccept();
            return;
        }
        // Anything else keeps the plain letter and is then handled as if the
        // popup had never been open.
        commitAndReset(ic, state, state->base);
    } else if (state->phase == Phase::Pending) {
        if (event.isRelease()) {
            if (sameKey(state->heldKey, event.rawKey()) &&
                !deliverTap(ic, state, event.rawKey(), event.time())) {
                // A tap, committed as text: the application never saw the key
                // go down, so it does not get to see it come up either.
                event.filterAndAccept();
            }
            return;
        }
        if (sameKey(state->heldKey, event.rawKey())) {
            // Auto-repeat of the held key. Our timer decides, not the repeat.
            event.filterAndAccept();
            return;
        }
        // Another key before the release: the first one was a tap, and it has
        // to reach the application ahead of this one.
        deliverTap(ic, state, event.rawKey(), event.time());
    }

    if (event.isRelease() || !*config_.enabled) {
        return;
    }
    if (!modifiersAllow(key) || key.states().test(fcitx::KeyState::Repeat)) {
        return;
    }
    const uint32_t cp = fcitx::Key::keySymToUnicode(key.sym());
    const Variants *variants = cp ? lookup(cp) : nullptr;
    if (!variants || variants->empty() || blocked(ic)) {
        return;
    }

    state->phase = Phase::Pending;
    state->heldKey = event.rawKey();
    state->base = fcitx::utf8::UCS4ToUTF8(cp);
    state->variants = *variants;
    // Deliberately no preedit here. A tap is by far the common case and must
    // look like a plain keystroke to the application: the key itself, no
    // composition. openPicker() starts the composition if the hold survives.
    arm(ic, state);
    event.filterAndAccept();
}

} // namespace omacento

FCITX_ADDON_FACTORY(omacento::OmacentoFactory);
