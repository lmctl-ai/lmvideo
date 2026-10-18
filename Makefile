CXX ?= g++
CXXFLAGS ?= -std=c++17 -O2
BUILD ?= build

CUR_DIR := $(shell pwd)
SKIA_DIR := third_party/skia
SKIA_OUT := $(SKIA_DIR)/out/Release-linux-x64

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
    $(SKIA_OUT)/libwebp_sse41.a \
    $(SKIA_OUT)/libzlib.a \
    $(SKIA_OUT)/libfreetype2.a \
    $(SKIA_OUT)/libharfbuzz.a \
    $(SKIA_OUT)/libexpat.a \
    $(SKIA_OUT)/libicu.a \
    $(SKIA_OUT)/libskcms.a

LINK_SKIA := -Wl,--start-group $(SKIA_LIBS) -Wl,--end-group -lpthread -ldl

.PHONY: all test clean fetch_skia

all: fetch_skia $(BUILD)/liblmvideo.a $(BUILD)/lmvideo $(BUILD)/skia_test $(BUILD)/encoder_test $(BUILD)/lmvideohost

fetch_skia:
	@scripts/fetch-skia.sh --quiet

$(BUILD):
	@mkdir -p $(BUILD)

$(BUILD)/lmvideo.o: cpp/lmvideo.cpp cpp/lmvideo.hpp | $(BUILD) fetch_skia
	$(CXX) $(CXXFLAGS) $(SKIA_INCLUDES) -c cpp/lmvideo.cpp -o $@

# The encoder (ffmpeg CLI pipe) is Skia-free; it is part of the library archive.
$(BUILD)/encoder.o: cpp/encoder.cpp cpp/encoder.hpp cpp/lmvideo.hpp | $(BUILD)
	$(CXX) $(CXXFLAGS) -Icpp -c cpp/encoder.cpp -o $@

$(BUILD)/liblmvideo.a: $(BUILD)/lmvideo.o $(BUILD)/encoder.o
	ar rcs $@ $^

$(BUILD)/lmvideo_cli.o: cpp/lmvideo_cli.cpp cpp/lmvideo.hpp | $(BUILD)
	$(CXX) $(CXXFLAGS) -Icpp -c cpp/lmvideo_cli.cpp -o $@

$(BUILD)/lmvideo: $(BUILD)/lmvideo_cli.o $(BUILD)/liblmvideo.a | $(BUILD) fetch_skia
	$(CXX) $(CXXFLAGS) -o $@ $(BUILD)/lmvideo_cli.o $(BUILD)/liblmvideo.a $(LINK_SKIA)

$(BUILD)/skia_test.o: tests/skia_test.cpp cpp/lmvideo.hpp | $(BUILD)
	$(CXX) $(CXXFLAGS) -Icpp -c tests/skia_test.cpp -o $@

$(BUILD)/skia_test: $(BUILD)/skia_test.o $(BUILD)/liblmvideo.a | $(BUILD) fetch_skia
	$(CXX) $(CXXFLAGS) -o $@ $(BUILD)/skia_test.o $(BUILD)/liblmvideo.a $(LINK_SKIA)

$(BUILD)/encoder_test.o: tests/encoder_test.cpp cpp/encoder.hpp cpp/lmvideo.hpp | $(BUILD)
	$(CXX) $(CXXFLAGS) -Icpp -c tests/encoder_test.cpp -o $@

$(BUILD)/encoder_test: $(BUILD)/encoder_test.o $(BUILD)/liblmvideo.a | $(BUILD) fetch_skia
	$(CXX) $(CXXFLAGS) -o $@ $(BUILD)/encoder_test.o $(BUILD)/liblmvideo.a $(LINK_SKIA)

# The DSL host: implements schema/lmvideo.dsl.json over the library (dslvm.hpp is vendored, pinned by sdk.lock).
$(BUILD)/host.o: cpp/host.cpp cpp/dslvm.hpp cpp/encoder.hpp cpp/lmvideo.hpp | $(BUILD)
	$(CXX) $(CXXFLAGS) -Wall -Wextra -Icpp -c cpp/host.cpp -o $@

$(BUILD)/lmvideohost: $(BUILD)/host.o $(BUILD)/liblmvideo.a | $(BUILD) fetch_skia
	$(CXX) $(CXXFLAGS) -o $@ $(BUILD)/host.o $(BUILD)/liblmvideo.a $(LINK_SKIA)

test: $(BUILD)/skia_test $(BUILD)/encoder_test
	@$(BUILD)/skia_test && $(BUILD)/encoder_test

clean:
	rm -rf $(BUILD)
