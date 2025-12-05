.PHONY: all cmake build clean debug release check fix help

SHELL=/bin/bash
BUILD_BASE_DIR=_build
BUILD_DIR=release
BUILD_TYPE=Release

# Python configuration
PYTHON_EXECUTABLE ?= $(shell which python3)

# CMake Flags
CMAKE_FLAGS := -DCMAKE_BUILD_TYPE=$(BUILD_TYPE)
CMAKE_FLAGS += -DCMAKE_EXPORT_COMPILE_COMMANDS=ON

# Allow passing LLVM_DIR externally if needed (e.g. make debug LLVM_DIR=/opt/...)
ifdef LLVM_DIR
CMAKE_FLAGS += -DLLVM_DIR=$(LLVM_DIR)
endif

# Use Ninja if available. If Ninja is used, pass through parallelism control flags.
USE_NINJA ?= 1
ifeq ($(USE_NINJA), 1)
ifneq ($(shell which ninja), )
GENERATOR := -GNinja
# Ninja makes compilers disable colored output by default.
GENERATOR += -DVELOX_FORCE_COLORED_OUTPUT=ON
endif
endif

# Detect number of threads for parallel build
NUM_THREADS ?= $(shell getconf _NPROCESSORS_CONF 2>/dev/null || echo 4)

# ==========================================
# Targets
# ==========================================

all: release          #: Build the release version (Default)

clean:              #: Delete all build artifacts
	rm -rf $(BUILD_BASE_DIR)

cmake:              #: Use CMake to create a Makefile build system
	mkdir -p $(BUILD_BASE_DIR)/$(BUILD_DIR) && \
	cmake -B \
	   "$(BUILD_BASE_DIR)/$(BUILD_DIR)" \
	   ${CMAKE_FLAGS} \
	   $(GENERATOR) \
	   ${EXTRA_CMAKE_FLAGS} \
	   .

build:              #: Build the software based in BUILD_DIR and BUILD_TYPE variables
	cmake --build $(BUILD_BASE_DIR)/$(BUILD_DIR) -j $(NUM_THREADS)

debug:              #: Build with debugging symbols
	$(MAKE) cmake BUILD_DIR=debug BUILD_TYPE=Debug
	$(MAKE) build BUILD_DIR=debug

release:            #: Build the release version
	$(MAKE) cmake BUILD_DIR=release BUILD_TYPE=Release && \
	$(MAKE) build BUILD_DIR=release

# ==========================================
# Format Tools
# ==========================================

check:              #: Check code format using scripts/check_format.py
	$(PYTHON_EXECUTABLE) scripts/check_format.py

fix:                #: Apply code format using scripts/apply_format.py
	$(PYTHON_EXECUTABLE) scripts/apply_format.py
doc-fix:
	mdformat README.md

# ==========================================
# Help
# ==========================================

help:               #: Show this help message
	@grep -E '^[a-zA-Z_-]+:.*?#: .*$$' $(MAKEFILE_LIST) | sort | awk 'BEGIN {FS = ":.*?#: "}; {printf "\033[36m%-20s\033[0m %s\n", $$1, $$2}'
