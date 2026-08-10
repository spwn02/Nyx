JOBS ?= 16

.PHONY: config config-tests build build-nproc run tests pack ci clean

config:
	cmake --preset debug

config-tests:
	cmake --preset development-tests

build: config
	cmake --build --preset debug --parallel $(JOBS) --target NyxEngine

build-nproc: config
	cmake --build --preset debug --parallel $$(nproc) --target NyxEngine

run: build
	./build/debug/Engine/Source/Runtime/Launch/NyxEngine

# Tests
tests: config-tests
	cmake --build --preset development-tests --parallel $(JOBS) --target unit_tests
	./build/development-tests/tests/unit_tests

# Packing
pack: build
	cpack --config ./build/debug/CPackConfig.cmake -C Debug

ci:
	$(MAKE) config
	$(MAKE) build
	$(MAKE) tests
	$(MAKE) pack

clean:
	rm -rf build
