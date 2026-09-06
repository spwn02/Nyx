JOBS ?= 16

.PHONY: config config-tests config-release build build-nproc build-tests release run tests ctest pack ci clean

config:
	cmake --preset debug

config-tests:
	cmake --preset development-tests

config-release:
	cmake --preset release

build: config
	cmake --build --preset debug --parallel $(JOBS) --target NyxEngine

build-nproc: config
	cmake --build --preset debug --parallel $$(nproc) --target NyxEngine

build-tests: config-tests
	cmake --build --preset development-tests --parallel $(JOBS) --target unit_tests

release: config-release
	cmake --build --preset release --parallel $(JOBS)

run: build
	./build/debug/Engine/Source/Runtime/Launch/NyxEngine

tests: build-tests
	./build/development-tests/tests/unit_tests

ctest: build-tests
	ctest --preset development-tests

pack: release
	rm -rf build/release/package
	cpack --config build/release/CPackConfig.cmake

ci:
	./.github/scripts/run-validation.sh

clean:
	rm -rf build
