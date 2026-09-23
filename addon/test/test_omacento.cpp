// End-to-end tests for the omacento addon.
//
// These drive a real fcitx5 Instance with the freshly built libomacento.so and
// fcitx5's own TestFrontend, so what is exercised is the addon as fcitx5 loads
// it — key events in, commits out — rather than a reimplementation of its
// logic in the test.
//
// TestFrontend::pushCommitExpectation queues the commits a step must produce,
// in order, and aborts on a mismatch. That is what makes "the base letter must
// not be committed before the accent" testable at all: a stray commit fails
// against the next expectation instead of passing unnoticed.

#include <functional>
#include <string>
#include <vector>

#include <fcitx-module/testfrontend/testfrontend_public.h>
#include <fcitx-utils/event.h>
#include <fcitx-utils/eventdispatcher.h>
#include <fcitx-utils/log.h>
#include <fcitx-utils/testing.h>
#include <fcitx/addoninstance.h>
#include <fcitx/addonmanager.h>
#include <fcitx/instance.h>

using namespace fcitx;

namespace {

// These are wall-clock waits against the addon's real timer, so the margins
// have to survive a loaded machine. An earlier 120/40 split was only 3x and
// failed about one run in five: the "release before the timer" step slipped
// past the threshold, the tap became a hold, and every later expectation was
// off by one. 15x is dull but stable.
constexpr uint64_t kHoldMs = 300;   // set on the addon below
constexpr uint64_t kBeyond = 500;   // comfortably past kHoldMs
constexpr uint64_t kWithin = 20;    // comfortably before it

// A list of (delay, action) run in order on the event loop, so a test can wait
// out the addon's real timer instead of mocking it away.
class Script {
public:
    Script(Instance *instance) : instance_(instance) {}

    void add(uint64_t delayMs, std::function<void()> step) {
        steps_.emplace_back(delayMs, std::move(step));
    }

    void run() { next(); }

private:
    void next() {
        if (index_ >= steps_.size()) {
            instance_->exit();
            return;
        }
        auto [delayMs, step] = steps_[index_++];
        timer_ = instance_->eventLoop().addTimeEvent(
            CLOCK_MONOTONIC, now(CLOCK_MONOTONIC) + delayMs * 1000ULL, 0,
            [this, step](EventSourceTime *, uint64_t) {
                step();
                next();
                return false;
            });
    }

    Instance *instance_;
    std::vector<std::pair<uint64_t, std::function<void()>>> steps_;
    size_t index_ = 0;
    std::unique_ptr<EventSourceTime> timer_;
};

} // namespace

int main() {
    setupTestingEnvironment(TESTING_BINARY_DIR, {TESTING_ADDON_DIR, "/usr/lib/fcitx5"},
                            {TESTING_DATA_DIR, "/usr/share/fcitx5"});

    char arg0[] = "test_omacento";
    char arg1[] = "--disable=all";
    char arg2[] = "--enable=testfrontend,omacento";
    char *argv[] = {arg0, arg1, arg2};
    Instance instance(FCITX_ARRAY_SIZE(argv), argv);
    instance.addonManager().registerDefaultLoader(nullptr);

    // pushCommitExpectation aborts at the moment of a wrong commit, which is
    // where you want the stack trace. It cannot see a commit that never
    // happens, though, so record them independently and check the whole
    // sequence at the end -- otherwise an addon that commits nothing at all
    // would sail through every expectation still queued.
    std::vector<std::string> commits;
    auto commitWatcher = instance.watchEvent(
        EventType::InputContextCommitString, EventWatcherPhase::Default,
        [&commits](Event &event) {
            commits.push_back(static_cast<CommitStringEvent &>(event).text());
        });

    const std::vector<std::string> wanted = {"a", "ã", "o", "e",  "Ã", "i", "u",
                                             "õ", "c", "e", "à",  "Ł", "á",
                                             "ć"};

    int failures = 0;
    instance.eventDispatcher().schedule([&instance, &failures]() {
        auto *frontend = instance.addonManager().addon("testfrontend");
        auto *addon = instance.addonManager().addon("omacento");
        if (!frontend || !addon) {
            FCITX_ERROR() << "missing addon: testfrontend=" << (void *)frontend
                          << " omacento=" << (void *)addon;
            ++failures;
            instance.exit();
            return;
        }

        // Shorten the hold so the suite does not spend seconds waiting.
        RawConfig cfg;
        cfg.setValueByPath("HoldTime", std::to_string(kHoldMs));
        cfg.setValueByPath("Enabled", "True");
        addon->setConfig(cfg);

        auto ic = frontend->call<ITestFrontend::createInputContext>("test-app");
        auto press = [frontend, ic](const char *k) {
            frontend->call<ITestFrontend::keyEvent>(ic, Key(k), false);
        };
        auto release = [frontend, ic](const char *k) {
            frontend->call<ITestFrontend::keyEvent>(ic, Key(k), true);
        };
        auto expect = [frontend](const std::string &s) {
            frontend->call<ITestFrontend::pushCommitExpectation>(s);
        };

        auto *script = new Script(&instance);

        // 1. A tap commits the plain letter. This is the regression that once
        //    committed an empty string and made vowels untypable.
        script->add(0, [=] { expect("a"); press("a"); });
        script->add(kWithin, [=] { release("a"); });

        // 2. A hold opens the popup; a digit commits that accent and the base
        //    letter is never committed on its own.
        script->add(10, [=] { expect("ã"); press("a"); });
        script->add(kBeyond, [=] { press("2"); release("2"); });
        script->add(10, [=] { release("a"); });

        // 3. Escape out of the popup keeps the plain letter.
        script->add(10, [=] { expect("o"); press("o"); });
        script->add(kBeyond, [=] { press("Escape"); release("Escape"); });
        script->add(10, [=] { release("o"); });

        // 4. A second key while waiting flushes the first letter.
        script->add(10, [=] { expect("e"); press("e"); });
        script->add(kWithin, [=] { press("k"); release("k"); release("e"); });

        // 5. Shift reaches the uppercase table.
        script->add(10, [=] { expect("Ã"); press("A"); });
        script->add(kBeyond, [=] { press("2"); release("2"); });
        script->add(10, [=] { release("A"); });

        // 6. Modified keys belong to the application. If this ever regresses,
        //    Ctrl+A stops selecting all, everywhere.
        script->add(10, [=] { press("Control+a"); release("Control+a"); });
        script->add(10, [=] { expect("i"); press("i"); });
        script->add(kWithin, [=] { release("i"); });

        // 7. A blocklisted program gets its key back untouched, popup or not.
        script->add(10, [=] {
            RawConfig blocked;
            blocked.setValueByPath("Blocklist/0", "test-app");
            addon->setConfig(blocked);
            press("a");
        });
        script->add(kBeyond, [=] { release("a"); });
        script->add(10, [=] {
            RawConfig unblocked;
            unblocked.setValueByPath("Blocklist", "");
            addon->setConfig(unblocked);
            expect("u");
            press("u");
        });
        script->add(kWithin, [=] { release("u"); });

        // 8. Arrow then Enter takes the second candidate.
        script->add(10, [=] { expect("õ"); press("o"); });
        script->add(kBeyond, [=] { press("Right"); release("Right"); });
        script->add(10, [=] { press("Return"); release("Return"); });
        script->add(10, [=] { release("o"); });

        // 9. A digit past the end of the list is not a selection: keep the
        //    plain letter rather than indexing off the end.
        script->add(10, [=] { expect("c"); press("c"); });
        script->add(kBeyond, [=] { press("9"); release("9"); });
        script->add(10, [=] { release("c"); });

        // 10. Switched off, the addon is out of the way entirely.
        script->add(10, [=] {
            RawConfig off;
            off.setValueByPath("Enabled", "False");
            addon->setConfig(off);
            press("a");
        });
        script->add(kBeyond, [=] { release("a"); });
        script->add(10, [=] {
            RawConfig on;
            on.setValueByPath("Enabled", "True");
            addon->setConfig(on);
            expect("e");
            press("e");
        });
        script->add(kWithin, [=] { release("e"); });

        auto setLanguage = [addon](const char *id) {
            RawConfig c;
            c.setValueByPath("Language", id);
            addon->setConfig(c);
        };

        // 11. The language decides the order, not just the contents. French
        //     puts the grave first on "a" where Portuguese puts the acute.
        script->add(10, [=] { setLanguage("fr"); expect("à"); press("a"); });
        script->add(kBeyond, [=] { press("1"); release("1"); });
        script->add(10, [=] { release("a"); });

        // 12. Uppercase is derived, never written out by hand. Polish l -> ł
        //     sits in the half of Latin Extended-A where the case pairs flip
        //     parity, which a naive rule gets wrong while leaving ć and š
        //     looking fine.
        script->add(10, [=] { setLanguage("pl"); expect("Ł"); press("L"); });
        script->add(kBeyond, [=] { press("1"); release("1"); });
        script->add(10, [=] { release("L"); });

        // 13. An unknown language must not leave someone with no accents.
        script->add(10, [=] { setLanguage("klingon"); expect("á"); press("a"); });
        script->add(kBeyond, [=] { press("1"); release("1"); });
        script->add(10, [=] { release("a"); });

        // 14. A custom table overrides the language, and its base character
        //     may be outside ASCII -- the lookup is by the character the key
        //     produces, not by a single byte.
        script->add(10, [=] {
            RawConfig c;
            c.setValueByPath("Table/0", "ç ć č");
            addon->setConfig(c);
            expect("ć");
            press("ccedilla");
        });
        script->add(kBeyond, [=] { press("1"); release("1"); });
        script->add(10, [=] { release("ccedilla"); });

        script->run();
    });

    instance.exec();

    if (commits != wanted) {
        FCITX_ERROR() << "commit sequence mismatch";
        FCITX_ERROR() << "  wanted: " << wanted;
        FCITX_ERROR() << "  got:    " << commits;
        ++failures;
    }
    if (failures) {
        FCITX_ERROR() << failures << " failure(s)";
        return 1;
    }
    FCITX_INFO() << commits.size() << " commits, all as expected";
    return 0;
}
