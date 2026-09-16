.PHONY: all unit wasm test website clean

PLUGIN_TARGETS := $(dir $(wildcard plugins/*/targets/*/Makefile))

# Parallelism for unit/wasm/clean. Override with `make unit JOBS=2`.
JOBS ?= $(shell nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)

export GCC_BIN_PATH
export EMCC_BIN_PATH

all: unit

unit:
	@printf '%s\0' $(PLUGIN_TARGETS) | xargs -0 -P $(JOBS) -I{} $(MAKE) -C {} install

wasm:
	@printf '%s\0' $(PLUGIN_TARGETS) | xargs -0 -P $(JOBS) -I{} $(MAKE) -C {} wasm-ci

test:
	node tests/nts1-midi.test.mjs
	node tests/preview-kick-demo.test.mjs
	node tests/preview-dry-input.test.mjs
	node tests/preview-wasm-cache-bust.test.mjs
	node tests/plugin-category.test.mjs

website: unit
	bash scripts/build-website.sh dist/website

clean:
	@printf '%s\0' $(PLUGIN_TARGETS) | xargs -0 -P $(JOBS) -I{} $(MAKE) -C {} clean
	rm -rf dist
