BUILD_DIR ?= ../build
BUILD_TYPE ?= RelWithDebInfo
CMAKE_PREFIX_PATH ?=
JOBS ?= 8
QT_DISABLE_HTTP3 ?= ON
DISABLE_HTTP3_VALUE := $(if $(filter ON on 1 TRUE true YES yes,$(QT_DISABLE_HTTP3)),1,0)
export DISABLE_HTTP3 := $(DISABLE_HTTP3_VALUE)

UNAME_S := $(shell uname -s)

.PHONY: check_tools
check_tools:
	@command -v cmake >/dev/null 2>&1 || { echo "cmake not found"; exit 1; }
	@command -v conan >/dev/null 2>&1 || { echo "conan not found. Install with: pip install conan"; exit 1; }

.PHONY: configure
configure: check_tools
	cmake -S .. -B $(BUILD_DIR) \
		-Uquiche_DIR \
		-UDISABLE_HTTP3 \
		-DCMAKE_BUILD_TYPE=$(BUILD_TYPE) \
		-DBUILD_TRUSTTUNNEL_QT=ON \
		-DDISABLE_HTTP3:BOOL=$(QT_DISABLE_HTTP3) \
		-DCMAKE_DISABLE_FIND_PACKAGE_quiche=ON \
		$(if $(CMAKE_PREFIX_PATH),-DCMAKE_PREFIX_PATH=$(CMAKE_PREFIX_PATH),)

.PHONY: build
build: configure
	cmake --build $(BUILD_DIR) --target FreeTunnel -j$(JOBS)

.PHONY: run
run: build
ifeq ($(UNAME_S),Darwin)
	$(BUILD_DIR)/FreeTunnel/FreeTunnel.app/Contents/MacOS/FreeTunnel
else
	$(BUILD_DIR)/FreeTunnel/FreeTunnel
endif

.PHONY: clean
clean:
	rm -rf $(BUILD_DIR)

.PHONY: distclean
distclean:
	rm -rf ../build ../build-* ./build ./build-local

.PHONY: rebuild
rebuild: clean build
