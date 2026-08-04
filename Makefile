JOBS ?= 4
CLANGD ?= $(HOME)/.local/opt/clang-p2996/bin/clangd

.PHONY: config build build-nproc clangd-check run tests pack ci clean

config:
	cmake --preset debug

build: config
	cmake --build --preset default --parallel $(JOBS) --target NyxEngine

build-nproc: config
	cmake --build --preset default --parallel $$(nproc) --target NyxEngine

clangd-check: config
	$(CLANGD) --check=Engine/Source/Runtime/Launch/Private/Linux/main.cpp --compile-commands-dir=build

run: build
	./build/Engine/Source/Runtime/Launch/NyxEngine

# Tests
tests: config
	cmake --build --preset default --parallel $(JOBS) --target unit_tests
	./build/tests/unit_tests

# Packing
pack: build
	cpack --config ./build/CPackConfig.cmake -C Debug

ci:
	$(MAKE) config
	$(MAKE) build
	$(MAKE) tests
	$(MAKE) pack

clean:
	rm -rf build
