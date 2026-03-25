# Makefile — wrapper for cmake build
# Run ./configure first, then make.

BUILD_DIR ?= build
JOBS      ?= $(shell nproc 2>/dev/null || echo 4)

.PHONY: all clean install

all:
	@if [ ! -f "$(BUILD_DIR)/Makefile" ] && [ ! -f "$(BUILD_DIR)/build.ninja" ]; then \
	    echo "Build directory not configured. Run ./configure first."; \
	    exit 1; \
	fi
	cmake --build $(BUILD_DIR) --parallel $(JOBS)

clean:
	cmake --build $(BUILD_DIR) --target clean 2>/dev/null || rm -rf $(BUILD_DIR)

install:
	cmake --install $(BUILD_DIR)
