CXX ?= g++
CXXFLAGS ?= -std=c++17 -O2
BUILD ?= build

CUR_DIR := $(shell pwd)
SKIA_DIR := third_party/skia
SKIA_PLATFORM := $(shell scripts/platform.sh)
SKIA_OS := $(firstword $(subst -, ,$(SKIA_PLATFORM)))
SKIA_OUT := $(SKIA_DIR)/out/Release-$(SKIA_PLATFORM)
LMVIDEO_VERSION := $(shell head -n1 VERSION)

SKIA_INCLUDES := \
    -Icpp \
    -I$(SKIA_DIR) \
    -isystem $(SKIA_DIR)/include \
    -isystem $(SKIA_DIR)/modules/svg/include \
    -isystem $(SKIA_DIR)/modules/skshaper/include \
    -Wa,-I$(CUR_DIR) \
    -w

SKIA_LIBS := \
    $(SKIA_OUT)/libsvg.a \
    $(SKIA_OUT)/libskshaper.a \
    $(SKIA_OUT)/libskunicode_core.a \
    $(SKIA_OUT)/libskunicode_icu.a \
    $(SKIA_OUT)/libskia.a \
    $(SKIA_OUT)/libpng.a \
    $(SKIA_OUT)/libjpeg.a \
    $(SKIA_OUT)/libwebp.a \
    $(wildcard $(SKIA_OUT)/libwebp_sse41.a) \
    $(SKIA_OUT)/libzlib.a \
    $(SKIA_OUT)/libharfbuzz.a \
    $(wildcard $(SKIA_OUT)/libfreetype2.a) \
    $(SKIA_OUT)/libexpat.a \
    $(SKIA_OUT)/libicu.a \
    $(SKIA_OUT)/libskcms.a

# Linux: one static group (STATIC=1 adds -static: no runtime dependency except the user's ffmpeg). macOS: Skia needs the system font and graphics frameworks.
ifeq ($(SKIA_OS),macos)
LINK_SKIA := $(SKIA_LIBS) -lpthread -framework CoreFoundation -framework CoreGraphics -framework CoreText -framework CoreServices -framework Foundation
else
LINK_SKIA := -Wl,--start-group $(SKIA_LIBS) -Wl,--end-group -lpthread -ldl
ifeq ($(STATIC),1)
LINK_SKIA += -static
endif
endif

.PHONY: all test clean fetch_skia

all: fetch_skia $(BUILD)/liblmvideo.a $(BUILD)/lmvideo $(BUILD)/skia_test $(BUILD)/encoder_test

fetch_skia:
	@scripts/fetch-skia.sh --quiet

$(BUILD):
	@mkdir -p $(BUILD)

$(BUILD)/lmvideo.o: cpp/lmvideo.cpp cpp/lmvideo.hpp assets/fonts/DejaVuSans.ttf | $(BUILD) fetch_skia
	$(CXX) $(CXXFLAGS) $(SKIA_INCLUDES) -c cpp/lmvideo.cpp -o $@

# The encoder (ffmpeg CLI pipe) is Skia-free; it is part of the library archive.
$(BUILD)/encoder.o: cpp/encoder.cpp cpp/encoder.hpp cpp/lmvideo.hpp | $(BUILD)
	$(CXX) $(CXXFLAGS) -Icpp -c cpp/encoder.cpp -o $@

$(BUILD)/liblmvideo.a: $(BUILD)/lmvideo.o $(BUILD)/encoder.o
	ar rcs $@ $^

$(BUILD)/lmvideo_cli.o: cpp/lmvideo_cli.cpp cpp/lmvideo.hpp | $(BUILD)
	$(CXX) $(CXXFLAGS) -Icpp -c cpp/lmvideo_cli.cpp -o $@


$(BUILD)/skia_test.o: tests/skia_test.cpp cpp/lmvideo.hpp | $(BUILD)
	$(CXX) $(CXXFLAGS) -Icpp -c tests/skia_test.cpp -o $@

$(BUILD)/skia_test: $(BUILD)/skia_test.o $(BUILD)/liblmvideo.a | $(BUILD) fetch_skia
	$(CXX) $(CXXFLAGS) -o $@ $(BUILD)/skia_test.o $(BUILD)/liblmvideo.a $(LINK_SKIA)

$(BUILD)/encoder_test.o: tests/encoder_test.cpp cpp/encoder.hpp cpp/lmvideo.hpp | $(BUILD)
	$(CXX) $(CXXFLAGS) -Icpp -c tests/encoder_test.cpp -o $@

$(BUILD)/encoder_test: $(BUILD)/encoder_test.o $(BUILD)/liblmvideo.a | $(BUILD) fetch_skia
	$(CXX) $(CXXFLAGS) -o $@ $(BUILD)/encoder_test.o $(BUILD)/liblmvideo.a $(LINK_SKIA)

# The DSL host: implements schema/lmvideo.dsl.json over the library (dslvm.hpp is vendored, pinned by sdk.lock).
# The schema is embedded byte for byte (`lmvideo --dsl-schema --json` prints exactly these bytes). LMVIDEO_SCHEMA exists
# only so test.sh can build a host embedding an edited schema to exercise the registry/schema self-check.
LMVIDEO_SCHEMA ?= schema/lmvideo.dsl.json
$(BUILD)/generated/schema_embedded.hpp: $(LMVIDEO_SCHEMA)
	@mkdir -p $(@D)
	{ printf '#pragma once\n#include <cstddef>\nnamespace lmvideo_dsl { inline constexpr unsigned char embedded_schema[] = {\n'; \
	  od -An -v -tx1 $(LMVIDEO_SCHEMA) | sed 's/ *\([0-9a-f][0-9a-f]\)/0x\1,/g'; \
	  printf '};\ninline constexpr std::size_t embedded_schema_size = sizeof(embedded_schema); }\n'; } >$@.tmp && mv $@.tmp $@

$(BUILD)/host.o: cpp/host.cpp cpp/dslvm.hpp cpp/encoder.hpp cpp/lmvideo.hpp cpp/schema.hpp $(BUILD)/generated/schema_embedded.hpp | $(BUILD)
	$(CXX) $(CXXFLAGS) -Wall -Wextra -Icpp -I$(BUILD)/generated -c cpp/host.cpp -o $@

$(BUILD)/main.o: cpp/main.cpp VERSION | $(BUILD)
	$(CXX) $(CXXFLAGS) -Icpp -DLMVIDEO_VERSION='"$(LMVIDEO_VERSION)"' -c cpp/main.cpp -o $@

# One executable: the DSL host plus the `demo` / `render` library demos.
$(BUILD)/lmvideo: $(BUILD)/main.o $(BUILD)/host.o $(BUILD)/lmvideo_cli.o $(BUILD)/liblmvideo.a | $(BUILD) fetch_skia
	$(CXX) $(CXXFLAGS) -o $@ $(BUILD)/main.o $(BUILD)/host.o $(BUILD)/lmvideo_cli.o $(BUILD)/liblmvideo.a $(LINK_SKIA)

test: $(BUILD)/skia_test $(BUILD)/encoder_test
	@$(BUILD)/skia_test && $(BUILD)/encoder_test

clean:
	rm -rf $(BUILD)
