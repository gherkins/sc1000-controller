# SC1000 Midi Controller — top-level tasks for both halves of the project:
#   * the controller firmware (firmware/ + build/, Docker buildroot)
#   * the SC1000 plugin       (vst/, JUCE AU + Standalone)
#
# Tinker locally, commit source to main as usual, then `make publish` to ship the
# built binaries to the orphan `artifacts` branch (keeps main's history binary-free).
# Run `make help` for the list.

CONFIG  ?= Release
VST     := vst
ART     := $(VST)/build/SC1000_artefacts/$(CONFIG)
SHOT    := $(VST)/build/ScratchShot_artefacts/$(CONFIG)/ScratchShot
AU      := $(ART)/AU/SC1000.component
APP     := $(ART)/Standalone/SC1000.app
DIST    := dist
SHOTPNG := docs/screenshot.png
APPBIN  := $(APP)/Contents/MacOS/SC1000
TRACE   ?= $(CURDIR)/trace.csv

# Binaries are published to an orphan branch (force-pushed, single commit) instead
# of main's history, so cloning main stays lean and the repo size stays bounded.
ARTBRANCH := artifacts
ARTFILES  := SC1000-AU-latest.zip SC1000-Standalone-latest.zip sc1000-firmware-latest.zip

.DEFAULT_GOAL := help
.PHONY: help vst vst-debug auval shot standalone touchtest trace trace-analyze trace-replay firmware stock dist publish clean

help: ## Show this help
	@echo "SC1000 Midi Controller — make targets:"
	@grep -E '^[a-zA-Z0-9_-]+:.*?## ' $(MAKEFILE_LIST) \
		| awk 'BEGIN{FS=":.*?## "}{printf "  \033[36m%-12s\033[0m %s\n", $$1, $$2}'

## ---- plugin (vst/) ----

vst: ## Build the SC1000 plugin (AU + Standalone, Release)
	cd $(VST) && ./build.sh $(CONFIG)

vst-debug: ## Build the plugin in Debug
	$(MAKE) vst CONFIG=Debug

auval: ## Validate the AU with Apple's auval
	auval -v aumu Scr1 Scvt

shot: vst ## Render a fresh plugin screenshot to docs/screenshot.png
	$(SHOT) $(SHOTPNG) play
	@echo "wrote $(SHOTPNG)"

standalone: vst ## Launch the Standalone app
	open "$(APP)"

trace: vst ## Capture a debug trace: run Standalone w/ logging → trace.csv (load a sample, scratch, then QUIT to flush)
	@echo "==> recording to $(TRACE)"
	@echo "    Load a sample, run the scenarios (let it run + grab/back-spin; let it run + don't touch;"
	@echo "    stop + nudge by hand), then QUIT the app (Cmd-Q) to flush the CSV."
	@SC1000_TRACE="$(TRACE)" "$(APPBIN)" || true
	@echo "==> wrote $(TRACE)"

trace-analyze: ## Analyse a captured trace for touch mis-decisions (TRACE=path, default trace.csv)
	@python3 $(VST)/tools/trace_analyze.py "$(TRACE)"

trace-replay: ## Replay a capture through the CURRENT gate + re-analyse (TRACE=path, RELEASE_HOLD=secs)
	@SDK="$$(xcrun --show-sdk-path)"; WRAP=""; \
	printf '#include <algorithm>\nint main(){return 0;}\n' | clang++ -x c++ -std=c++17 -fsyntax-only - 2>/dev/null \
		|| WRAP="-nostdinc++ -isystem $$SDK/usr/include/c++/v1"; \
	clang++ -std=c++17 $$WRAP -I$(VST)/src $(VST)/test/trace_replay.cpp -o $(VST)/build/trace_replay \
		&& $(VST)/build/trace_replay "$(TRACE)" "$(TRACE).new.csv" $(RELEASE_HOLD) \
		&& python3 $(VST)/tools/trace_analyze.py "$(TRACE).new.csv"

touchtest: ## Build + run the TouchGate unit test (no JUCE, scratch-feel regression guard)
	@SDK="$$(xcrun --show-sdk-path)"; \
	WRAP=""; \
	printf '#include <algorithm>\nint main(){return 0;}\n' | clang++ -x c++ -std=c++17 -fsyntax-only - 2>/dev/null \
		|| WRAP="-nostdinc++ -isystem $$SDK/usr/include/c++/v1"; \
	clang++ -std=c++17 $$WRAP -I$(VST)/src $(VST)/test/touchgate_test.cpp -o $(VST)/build/touchgate_test \
		&& $(VST)/build/touchgate_test

## ---- controller firmware ----

firmware: ## Build flashable firmware via Docker buildroot (-> build/out/stick/)
	./build/run.sh

stock: ## Build a factory-restore tarball (sc-stock.tar)
	./build/make-stock.sh

## ---- publish ----

dist: vst ## Package latest artifacts into dist/ (plugin always; firmware if built)
	@mkdir -p $(DIST)
	@echo "==> packaging plugin"
	@rm -f $(DIST)/SC1000-AU-latest.zip $(DIST)/SC1000-Standalone-latest.zip
	ditto -c -k --keepParent "$(AU)"  "$(DIST)/SC1000-AU-latest.zip"
	ditto -c -k --keepParent "$(APP)" "$(DIST)/SC1000-Standalone-latest.zip"
	@echo "==> packaging firmware"
	@if [ -d build/out/stick ]; then \
		rm -f $(DIST)/sc1000-firmware-latest.zip; \
		( cd build/out/stick && zip -q -r -X "$(CURDIR)/$(DIST)/sc1000-firmware-latest.zip" xwax sc.tar ); \
		echo "   wrote $(DIST)/sc1000-firmware-latest.zip"; \
	else \
		echo "   SKIP firmware: build/out/stick missing — run 'make firmware' first"; \
	fi
	@echo && ls -lh $(DIST)

publish: dist ## Force-push dist/ artifacts to the orphan '$(ARTBRANCH)' branch (main stays binary-free)
	@set -e; \
	idx="$(CURDIR)/.git/artifacts.index"; rm -f "$$idx"; \
	export GIT_INDEX_FILE="$$idx"; \
	echo "==> building '$(ARTBRANCH)' tree (carrying forward any artifact not rebuilt this run)"; \
	if git fetch -q origin $(ARTBRANCH) 2>/dev/null; then git read-tree FETCH_HEAD; fi; \
	for f in $(ARTFILES); do \
		if [ -f "$(DIST)/$$f" ]; then \
			blob=$$(git hash-object -w "$(DIST)/$$f"); \
			git update-index --add --cacheinfo 100644,$$blob,$$f; \
			echo "   + $$f"; \
		fi; \
	done; \
	tree=$$(git write-tree); \
	if [ -z "$$(git ls-tree $$tree)" ]; then echo "   nothing to publish"; rm -f "$$idx"; exit 1; fi; \
	commit=$$(git commit-tree $$tree -m "Artifacts $$(date -u +%Y-%m-%dT%H:%M:%SZ)"); \
	rm -f "$$idx"; \
	git update-ref refs/heads/$(ARTBRANCH) $$commit; \
	git push -fq origin $(ARTBRANCH); \
	echo "==> published $(ARTBRANCH) @ $$commit — main history untouched (commit/push source separately)"

clean: ## Remove the plugin build dir
	rm -rf $(VST)/build
