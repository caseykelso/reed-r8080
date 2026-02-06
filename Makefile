BUILD_DIR ?= build
BINARY := $(BUILD_DIR)/reed-r8080
CONFIG_STAMP := $(BUILD_DIR)/.cmake-configured
ARGS ?= --raw --csv samples.csv

.PHONY: build configure run list clean vendor-download vendor-run

DOWNLOAD_DIR := downloads
VENDOR_ZIP := $(DOWNLOAD_DIR)/R8080-software.zip
VENDOR_EXE := $(DOWNLOAD_DIR)/R8080.exe
VENDOR_URL := https://www.reedinstruments.com/files/softwares/R8080-software.zip

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

$(VENDOR_ZIP):
	@mkdir -p $(DOWNLOAD_DIR)
	curl -L -o $@ $(VENDOR_URL)

$(VENDOR_EXE): $(VENDOR_ZIP)
	unzip -j -o $< "R8080-software/R8080-software/data/OFFLINE/E5CB2A27/704529CA/R8080.exe" -d $(DOWNLOAD_DIR)

vendor-download: $(VENDOR_EXE)
	@echo "Vendor app extracted to $(VENDOR_EXE)"

vendor-run: vendor-download
	WINEDEBUG=-all wine $(VENDOR_EXE)

clean:
	rm -rf $(BUILD_DIR)
