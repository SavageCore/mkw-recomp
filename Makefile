# Shortcuts for the flows of tools/build/README.md. Each target runs the same
# commands the README and the CI run; this file only names them so a build is
# one line. `make help` lists them; variables override the defaults, e.g.
# `make build EXEFS=~/dump/exefs` or `make appimage VERSION=0.1.12`.
#
# What each target needs: build/generated from your own dump (generated), the
# shader tools' inputs (shaders), clang/lld/ninja and the libraries of
# tools/build/README.md. The release targets fetch their own sysroot and
# setup tools, pinned by sha256 as tools/build/dist.py.

EXEFS ?= game/exefs
GEN ?= build/generated
PATCHES ?= recomp/patches-mkw.txt
# The release tag without its v (0.1.11), or tag-commits-hash between tags: the same source as the binary's own
# version (port_version in tools/build/build.py). A release passes its version: `make appimage VERSION=0.1.12`.
VERSION ?= $(shell git describe --tags --always | sed 's/^v//')

.PHONY: help build generated shaders selftest check dist appimage windows clean

help:
	@echo "build      dev binary build/mkw (needs generated C, see generated)"
	@echo "generated  AArch64 to C from your dump (EXEFS, GEN and PATCHES)"
	@echo "shaders    extract and translate the shaders"
	@echo "selftest   build, then build/mkw --selftest"
	@echo "check      what the CI checks without a GPU (ruff, docs, cfg rows, pytest)"
	@echo "dist       player's folder build/dist/linux (runtime and setup/)"
	@echo "appimage   dist packed as build/release/mkw-recomp-VERSION-x86_64.AppImage"
	@echo "windows    player's zip build/release/mkw-recomp-VERSION-windows-x64.zip"
	@echo "clean      remove build objects and packed releases, keep sources and caches"

build:
	uv run tools/build/build.py --gen $(GEN) --exefs $(EXEFS)

generated:
	uv run python recomp/gen.py --exefs $(EXEFS) --out $(GEN) --jobs 8 --patches $(PATCHES)

shaders:
	uv run tools/shaders/extract.py && uv run tools/shaders/translate.py

selftest: build
	./build/mkw --selftest

check:
	uv run ruff check tools sm86
	uv run tools/docs/check.py
	uv run tools/git/cfg_rows.py
	uv run pytest tests -q

build/sysroot:
	uv run tools/build/sysroot.py

build/dist/linux/mkw: build/sysroot
	uv run tools/build/build.py --dist --runtime-only --sysroot build/sysroot
	uv run tools/build/dist.py --target linux

dist: build/dist/linux/mkw

build/release/mkw-recomp-$(VERSION)-x86_64.AppImage: build/dist/linux/mkw
	uv run tools/build/release.py --target linux --version $(VERSION)

appimage: build/release/mkw-recomp-$(VERSION)-x86_64.AppImage

windows:
	uv run tools/build/windeps.py
	uv run tools/build/build.py --target windows --dist --runtime-only
	uv run tools/build/dist.py --target windows
	uv run tools/build/release.py --target windows --version $(VERSION)

clean:
	rm -rf build/obj build/obj-dist build/obj-sysroot build/obj-win build/release
