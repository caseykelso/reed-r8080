BUILD_DIR ?= build
BINARY := $(BUILD_DIR)/reed-r8080
CONFIG_STAMP := $(BUILD_DIR)/.cmake-configured
ARGS ?= --raw --csv samples.csv

.PHONY: build configure run list clean

$(CONFIG_STAMP): CMakeLists.txt $(wildcard src/*.cpp) $(wildcard include/**/*.hpp)
	@mkdir -p $(BUILD_DIR)
	cmake -S . -B $(BUILD_DIR)
	@touch $(CONFIG_STAMP)

build: $(BINARY)

$(BINARY): $(CONFIG_STAMP)
	cmake --build $(BUILD_DIR)

run: build
	$(BINARY) $(ARGS)

list: build
	$(BINARY) --list

clean:
	rm -rf $(BUILD_DIR)
