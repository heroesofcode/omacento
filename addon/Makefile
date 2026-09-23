# Builds the fcitx5 module addon. Deliberately plain make + pkg-config: a single
# shared object does not need cmake/ninja/extra-cmake-modules, and not needing
# them is three fewer packages between someone cloning this and a working popup.

FCITX_VERSION := $(shell pkg-config --modversion Fcitx5Core)
PREFIX        ?= $(HOME)/.local
LIBDIR        := $(PREFIX)/lib/fcitx5
CONFDIR       := $(PREFIX)/share/fcitx5/addon

CXXFLAGS ?= -O2 -std=c++20 -Wall -Wextra -fPIC -fvisibility=hidden
CXXFLAGS += $(shell pkg-config --cflags Fcitx5Core Fcitx5Utils Fcitx5Config)
LDLIBS   := $(shell pkg-config --libs Fcitx5Core Fcitx5Utils Fcitx5Config)

BUILD := build
TARGET := $(BUILD)/libomacento.so

all: $(TARGET) $(BUILD)/omacento.conf

$(BUILD):
	@mkdir -p $(BUILD)

SRC := src/omacento.cpp src/presets.cpp

$(TARGET): $(SRC) src/omacento.h src/presets.h | $(BUILD)
	$(CXX) $(CXXFLAGS) -shared -o $@ $(SRC) $(LDLIBS)

# The addon metadata must name the exact fcitx5 it was built against, so a
# version bump shows up as "addon did not load" rather than as a crash.
$(BUILD)/omacento.conf: omacento.conf.in | $(BUILD)
	sed 's/@FCITX_VERSION@/$(FCITX_VERSION)/g' $< > $@

install: all
	@install -Dm755 $(TARGET) $(LIBDIR)/libomacento.so
	@install -Dm644 $(BUILD)/omacento.conf $(CONFDIR)/omacento.conf
	@echo "installed into $(LIBDIR) and $(CONFDIR) (built against fcitx5 $(FCITX_VERSION))"

uninstall:
	@rm -f $(LIBDIR)/libomacento.so $(CONFDIR)/omacento.conf
	@echo "removed libomacento.so and omacento.conf"

# Runs the addon inside a real fcitx5 Instance with fcitx5's own TestFrontend.
# The .conf files go into a throwaway data dir so the test exercises the object
# just built here, never whatever happens to be installed in ~/.local.
TESTDATA := $(BUILD)/testdata

test: $(TARGET) $(BUILD)/omacento.conf
	@mkdir -p $(TESTDATA)/addon
	@cp $(BUILD)/omacento.conf $(TESTDATA)/addon/omacento.conf
	@cp test/addon/testfrontend.conf $(TESTDATA)/addon/testfrontend.conf
	$(CXX) $(CXXFLAGS) -I/usr/include/Fcitx5/Module \
	  -DTESTING_BINARY_DIR=\"$(CURDIR)/$(BUILD)\" \
	  -DTESTING_ADDON_DIR=\"$(CURDIR)/$(BUILD)\" \
	  -DTESTING_DATA_DIR=\"$(CURDIR)/$(TESTDATA)\" \
	  -o $(BUILD)/test_omacento test/test_omacento.cpp src/presets.cpp $(LDLIBS)
	@$(BUILD)/test_omacento

clean:
	@rm -rf $(BUILD)

.PHONY: all install uninstall clean test
