# SC1000 Midi Controller — top-level tasks for both halves of the project:
#   * the controller firmware (firmware/ + build/, Docker buildroot)
#   * the SC1000 plugin       (vst/, JUCE AU + Standalone)
#
# Tinker locally, then `make publish` to ship all artifacts + source.
# Run `make help` for the list.

CONFIG  ?= Release
VST     := vst
ART     := $(VST)/build/SC1000_artefacts/$(CONFIG)
SHOT    := $(VST)/build/ScratchShot_artefacts/$(CONFIG)/ScratchShot
AU      := $(ART)/AU/SC1000.component
APP     := $(ART)/Standalone/SC1000.app
DIST    := dist
SHOTPNG := docs/screenshot.png

.DEFAULT_GOAL := help
.PHONY: help vst vst-debug auval shot standalone firmware stock dist publish clean

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

publish: dist ## Commit + push the dist/ artifacts (one-command publish)
	git add $(DIST)
	git commit -m "Publish latest artifacts"
	git push

clean: ## Remove the plugin build dir
	rm -rf $(VST)/build
