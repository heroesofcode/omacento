#ifndef _OMACENTO_OMACENTO_H_
#define _OMACENTO_OMACENTO_H_

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include <fcitx-config/configuration.h>
#include <fcitx-config/iniparser.h>
#include <fcitx-config/option.h>
#include <fcitx-utils/event.h>
#include <fcitx-utils/eventloopinterface.h>
#include <fcitx-utils/key.h>
#include <fcitx/addonfactory.h>
#include <fcitx/addoninstance.h>
#include <fcitx/addonmanager.h>
#include <fcitx/candidatelist.h>
#include <fcitx/inputcontext.h>
#include <fcitx/inputcontextmanager.h>
#include <fcitx/inputcontextproperty.h>
#include <fcitx/inputpanel.h>
#include <fcitx/instance.h>

#include "presets.h"

namespace omacento {

FCITX_CONFIGURATION(
    OmacentoConfig,
    fcitx::Option<int, fcitx::IntConstrain> holdTime{
        this, "HoldTime", "Milliseconds to hold a key before the popup opens",
        250, fcitx::IntConstrain(100, 1000)};
    fcitx::Option<bool> enabled{this, "Enabled",
                                "Open the accent popup on long press", true};
    fcitx::Option<std::vector<std::string>> blocklist{
        this, "Blocklist",
        "Window classes where a held key should just repeat", {}};
    fcitx::Option<std::string> language{
        this, "Language",
        "Which accent set to use: ptbr, es, fr, de, it, pl, tr, nordic, en",
        "ptbr"};
    fcitx::Option<std::vector<std::string>> table{
        this, "Table",
        "Overrides the language: one entry per key, the base character then "
        "its variants, space separated. Lowercase only -- uppercase is "
        "derived. Empty means use the language preset.",
        {}};);

enum class Phase { Idle, Pending, Picking };

using Variants = std::vector<std::string>;

class OmacentoState : public fcitx::InputContextProperty {
public:
    Phase phase = Phase::Idle;
    uint32_t heldSym = 0;
    std::string base;
    Variants variants;
    bool preeditShown = false;
    std::unique_ptr<fcitx::EventSourceTime> timer;

    void disarm() { timer.reset(); }
};

class Omacento final : public fcitx::AddonInstance {
public:
    explicit Omacento(fcitx::Instance *instance);
    ~Omacento() override;

    const fcitx::Configuration *getConfig() const override { return &config_; }
    void setConfig(const fcitx::RawConfig &raw) override;
    void reloadConfig() override;

private:
    void applyConfig();
    void onKeyEvent(fcitx::KeyEvent &event);

    const Variants *lookup(uint32_t sym) const;
    bool blocked(fcitx::InputContext *ic) const;

    void arm(fcitx::InputContext *ic, OmacentoState *state);
    void openPicker(fcitx::InputContext *ic, OmacentoState *state);
    void setPreedit(fcitx::InputContext *ic, const std::string &text);
    // By value on purpose: every caller passes a reference into the state that
    // reset() is about to clear.
    void commitAndReset(fcitx::InputContext *ic, OmacentoState *state,
                        std::string text);
    void reset(fcitx::InputContext *ic, OmacentoState *state);

    fcitx::Instance *instance_;
    OmacentoConfig config_;
    std::unordered_map<uint32_t, Variants> table_;
    uint64_t holdUsec_ = 250 * 1000ULL;

    fcitx::FactoryFor<OmacentoState> factory_;
    std::unique_ptr<fcitx::HandlerTableEntry<fcitx::EventHandler>> keyHandler_;
};

class OmacentoFactory : public fcitx::AddonFactory {
public:
    fcitx::AddonInstance *create(fcitx::AddonManager *manager) override {
        return new Omacento(manager->instance());
    }
};

} // namespace omacento

#endif // _OMACENTO_OMACENTO_H_
