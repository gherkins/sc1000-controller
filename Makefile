# scratch-vst — top-level convenience wrapper around build.sh / ship.sh.
# (CMake generates its own Makefile under build/; this one is the dev UX.)
#
#   make            # show this help
#   make build      # build AU + Standalone (Debug)
#   make release    # build optimized (Release)
#   make start      # build, then launch the Standalone app
#   make ship m="…" # build Release, commit, push, install the AU
#
# Override the build config with CONFIG=Release, e.g. `make start CONFIG=Release`.

CONFIG ?= Debug
APP    := build/ScratchVST_artefacts/$(CONFIG)/Standalone/Scratch VST.app

.PHONY: help build release start ship auval clean

help:
	@echo "Targets:"
	@echo "  make build               build AU + Standalone (CONFIG=$(CONFIG))"
	@echo "  make release             build optimized (CONFIG=Release)"
	@echo "  make start               build, then launch the Standalone app"
	@echo "  make ship m=\"message\"    build Release, commit, push, install the AU"
	@echo "  make auval               validate the installed AU"
	@echo "  make clean               remove the build/ directory"

build:
	./build.sh $(CONFIG)

release:
	$(MAKE) build CONFIG=Release

start: build
	open "$(APP)"

ship:
	@if [ -z "$(m)" ]; then echo 'usage: make ship m="commit message"' >&2; exit 1; fi
	./ship.sh "$(m)"

auval:
	auval -v aumu Scr1 Scvt

clean:
	rm -rf build
