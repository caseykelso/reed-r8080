BUILD_DIR ?= build
WINEPREFIX ?= $(HOME)/.wine-r8080
WINEARCH ?= win32

BINARY := $(BUILD_DIR)/reed-r8080
CONFIG_STAMP := $(BUILD_DIR)/.cmake-configured
ARGS ?= --raw --csv samples.csv

.PHONY: build configure run list clean vendor-download vendor-run

DOWNLOAD_DIR := downloads
VENDOR_ZIP := $(DOWNLOAD_DIR)/R8080-software.zip
VENDOR_ROOT := $(DOWNLOAD_DIR)/vendor
VENDOR_STAMP := $(VENDOR_ROOT)/.unpacked
VENDOR_EXE := $(VENDOR_ROOT)/R8080-software/R8080-software/data/OFFLINE/E5CB2A27/704529CA/R8080.exe
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

$(VENDOR_STAMP): $(VENDOR_ZIP)
	rm -rf $(VENDOR_ROOT)
	mkdir -p $(VENDOR_ROOT)
	unzip -q $< -d $(VENDOR_ROOT)
	touch $@

vendor-download: $(VENDOR_STAMP)
	@echo "Vendor app unpacked under $(VENDOR_ROOT)"

vendor-run: vendor-download
	@if [ ! -f "$(WINEPREFIX)/system.reg" ]; then \
		echo "Initializing Wine prefix $(WINEPREFIX) (WINEARCH=$(WINEARCH))"; \
		WINEPREFIX=$(WINEPREFIX) WINEARCH=$(WINEARCH) wineboot -i >/dev/null 2>&1 || exit $$?; \
	fi
	WINEDEBUG=-all WINEPREFIX=$(WINEPREFIX) WINEARCH=$(WINEARCH) wine $(VENDOR_EXE)

clean:
	rm -rf $(BUILD_DIR)
