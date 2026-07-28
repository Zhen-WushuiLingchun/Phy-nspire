# Device build for Phy-nspire (TI-Nspire CX II CAS, Ndless r2022).
#
# The host build lives in CMakeLists.txt and never runs through this file.
# Bootstrap the SDK first:
#
#   tools/bootstrap-ndless.sh
#   eval "$(tools/bootstrap-ndless.sh --env-only)"
#   make
#
# Targets:
#   all            build dist/phy-nspire.tns
#   size-report    installed size against the 5-6 MB budget
#   symbol-report  largest symbols in the ELF
#   ir-link-check  prove the expression IR links on device
#   exact-link-check prove every bigint/bigrat entry point links on device
#   tensor-link-check  prove the component tensor core links on device
#   cas-link-check prove the scalar CAS links on device
#   algebraic-link-check prove certified real algebraic arithmetic links
#   geom-link-check    prove manifolds and differential forms link on device
#   ym-link-check      prove Yang-Mills and all dependencies link on device
#   color-link-check   prove exact SU(N) colour algebra links on device
#   eval-link-check    prove the stateful evaluator and its whole backend
#                      stack link on device
#   cas-smoke      build an observable on-device symbolic CAS acceptance test
#   qft-bench      build the real-CX-II Q-7 timing/allocation acceptance tool
#   clean

DEBUG ?= FALSE

GCC := nspire-gcc
GXX := nspire-g++
LD := nspire-ld
GENZEHN := genzehn

EXE := phy-nspire
DISTDIR := dist
BUILDDIR := build/arm
CAS_SMOKE_EXE := phy-cas-smoke
CAS_SMOKE_BUILDDIR := build/arm-cas-smoke
QFT_BENCH_EXE := phy-qft-bench
QFT_BENCH_BUILDDIR := build/arm-qft-bench
NMARKDOWN_ROOT := third_party/nmarkdown

# The nMarkdown formula slice is compiled with the same reduced FreeType and
# HarfBuzz configuration as upstream. These include paths are harmless for the
# portable C sources and keep the object rules deterministic.
NMARKDOWN_CPPFLAGS := -I$(NMARKDOWN_ROOT)/include \
    -I$(NMARKDOWN_ROOT)/third_party/freetype/nmarkdown \
    -I$(NMARKDOWN_ROOT)/third_party/freetype/include \
    -I$(NMARKDOWN_ROOT)/third_party/harfbuzz/src \
    -DFT2_BUILD_LIBRARY -DHB_TINY=1 -DNMARKDOWN_HARFBUZZ_MATH=1 \
    -DNMARKDOWN_NDLESS=1

GCCFLAGS := -Wall -Wextra -Wshadow -Wpointer-arith -std=c11 -marm \
            -ffunction-sections -fdata-sections -Iinclude -Isrc/gfx -Isrc/ir \
            -Isrc/tensor -Isrc/cas -Isrc/geom -Isrc/eval -Isrc/notebook \
            -Isrc/render -Isrc/storage \
            $(NMARKDOWN_CPPFLAGS)
CXXFLAGS := -Wall -Wextra -Wpedantic -std=c++17 -marm \
            -ffunction-sections -fdata-sections -fexceptions -fno-rtti \
            -Iinclude -Isrc/gfx -Isrc/ir -Isrc/tensor -Isrc/cas -Isrc/geom \
            -Isrc/eval -Isrc/notebook -Isrc/render -Isrc/storage \
            $(NMARKDOWN_CPPFLAGS)
# The Ndless ldscript intentionally produces a single RWX load segment, which
# binutils 2.39+ warns about. Ndless's own toolchain build disables that
# warning at configure time; we silence it at link time instead.
LDFLAGS := -Wl,--gc-sections -Wl,--no-warn-rwx-segments
ZEHNFLAGS := --name "Phy-nspire" --author "Phy-nspire contributors" \
             --version 1 --ndless-min 31 --ndless-rev-min 2022 \
             --clickpad-support true --color-support true \
             --uses-lcd-blit true --240x320-support false --compress

ifeq ($(DEBUG),FALSE)
    GCCFLAGS += -Os -DNDEBUG
    CXXFLAGS += -Os -DNDEBUG
else
    GCCFLAGS += -O0 -g
    CXXFLAGS += -O0 -g
endif

# Explicit source list. A wildcard would silently pull in the host backend,
# which must never reach the device binary.
SOURCES := \
    src/core/status.c \
    src/input/modifier.c \
    src/input/pointer.c \
    src/gfx/gfx.c \
    src/render/math_layout.c \
    src/notebook/document.c \
    src/notebook/palette.c \
    src/notebook/source.c \
    src/notebook/notebook.c \
    src/notebook/workspace.c \
    src/eval/env.c \
    src/eval/dispatch.c \
    src/eval/display.c \
    src/storage/storage.c \
    src/app/app.c \
    src/ir/ir.c \
    src/ir/order.c \
    src/ir/text.c \
    src/tensor/chart.c \
    src/tensor/symmetry.c \
    src/tensor/tensor.c \
    src/tensor/ops.c \
    src/gr/gr.c \
    src/lie/lie.c \
    src/qft/scalar.c \
    src/qft/lorentz.c \
    src/qft/dirac.c \
    src/qft/mandelstam.c \
    src/qft/color.c \
    src/exact/context.c \
    src/exact/integer.c \
    src/exact/rational.c \
    src/exact/algebraic.c \
    src/cas/num.c \
    src/cas/big_num.c \
    src/cas/finite_poly.c \
    src/cas/series.c \
    src/cas/limit.c \
    src/cas/engine.c \
    src/cas/simplify.c \
    src/cas/diff.c \
    src/cas/integrate.c \
    src/cas/normal.c \
    src/cas/reduce.c \
    src/geom/manifold.c \
    src/geom/form.c \
    src/geom/exterior.c \
    src/geom/metric.c \
    src/qft/yang_mills.c \
    src/app/main_ndless.c \
    src/platform/ndless/platform_ndless.c \
    src/platform/ndless/storage_ndless.c \
    src/platform/ndless/crt_compat.c

NMARKDOWN_C_SOURCES := \
    $(NMARKDOWN_ROOT)/third_party/freetype/src/base/ftsystem.c \
    $(NMARKDOWN_ROOT)/third_party/freetype/src/base/ftdebug.c \
    $(NMARKDOWN_ROOT)/third_party/freetype/src/base/ftinit.c \
    $(NMARKDOWN_ROOT)/third_party/freetype/src/base/ftbase.c \
    $(NMARKDOWN_ROOT)/third_party/freetype/src/base/ftbitmap.c \
    $(NMARKDOWN_ROOT)/third_party/freetype/src/base/ftmm.c \
    $(NMARKDOWN_ROOT)/third_party/freetype/src/sfnt/sfnt.c \
    $(NMARKDOWN_ROOT)/third_party/freetype/src/truetype/truetype.c \
    $(NMARKDOWN_ROOT)/third_party/freetype/src/cff/cff.c \
    $(NMARKDOWN_ROOT)/third_party/freetype/src/psaux/psaux.c \
    $(NMARKDOWN_ROOT)/third_party/freetype/src/pshinter/pshinter.c \
    $(NMARKDOWN_ROOT)/third_party/freetype/src/psnames/psnames.c \
    $(NMARKDOWN_ROOT)/third_party/freetype/src/smooth/smooth.c \
    $(NMARKDOWN_ROOT)/third_party/freetype/src/autofit/autofit.c

NMARKDOWN_CXX_SOURCES := \
    $(NMARKDOWN_ROOT)/src/document/unicode.cpp \
    $(NMARKDOWN_ROOT)/src/document/utf8.cpp \
    $(NMARKDOWN_ROOT)/src/generated/core_font_pack.cpp \
    $(NMARKDOWN_ROOT)/src/generated/core_math_font.cpp \
    $(NMARKDOWN_ROOT)/src/generated/unicode_tables.cpp \
    $(NMARKDOWN_ROOT)/src/math/math_atoms.cpp \
    $(NMARKDOWN_ROOT)/src/math/math_lexer.cpp \
    $(NMARKDOWN_ROOT)/src/math/math_layout.cpp \
    $(NMARKDOWN_ROOT)/src/math/math_macros.cpp \
    $(NMARKDOWN_ROOT)/src/math/math_parser.cpp \
    $(NMARKDOWN_ROOT)/src/math/math_system.cpp \
    $(NMARKDOWN_ROOT)/src/render/primitives.cpp \
    $(NMARKDOWN_ROOT)/src/render/surface565.cpp \
    $(NMARKDOWN_ROOT)/src/text/compositor.cpp \
    $(NMARKDOWN_ROOT)/src/text/font.cpp \
    $(NMARKDOWN_ROOT)/src/text/font_pack.cpp \
    $(NMARKDOWN_ROOT)/src/text/glyph_cache.cpp \
    $(NMARKDOWN_ROOT)/src/text/harfbuzz_shaper.cpp \
    $(NMARKDOWN_ROOT)/src/text/text_renderer.cpp \
    $(NMARKDOWN_ROOT)/src/text/text_system.cpp

CXX_SOURCES := src/render/formula_bridge.cpp src/render/ir_math_tree.cpp \
    $(NMARKDOWN_CXX_SOURCES)
CXX_CC_SOURCES := $(NMARKDOWN_ROOT)/third_party/harfbuzz/src/harfbuzz.cc
C_OBJECTS := $(patsubst %.c,$(BUILDDIR)/%.o,$(SOURCES) $(NMARKDOWN_C_SOURCES))
CXX_OBJECTS := $(patsubst %.cpp,$(BUILDDIR)/%.o,$(CXX_SOURCES)) \
               $(patsubst %.cc,$(BUILDDIR)/%.o,$(CXX_CC_SOURCES))
OBJECTS := $(C_OBJECTS) $(CXX_OBJECTS)
ELF := $(DISTDIR)/$(EXE).elf
TNS := $(DISTDIR)/$(EXE).tns

CAS_SMOKE_SOURCES := \
    src/core/status.c \
    src/gfx/gfx.c \
    src/ir/ir.c \
    src/ir/order.c \
    src/ir/text.c \
    src/cas/num.c \
    src/cas/engine.c \
    src/cas/simplify.c \
    src/cas/diff.c \
    src/cas/integrate.c \
    src/cas/normal.c \
    src/cas/reduce.c \
    tests/device/cas_smoke.c \
    src/platform/ndless/platform_ndless.c \
    src/platform/ndless/crt_compat.c
CAS_SMOKE_OBJECTS := \
    $(patsubst %.c,$(CAS_SMOKE_BUILDDIR)/%.o,$(CAS_SMOKE_SOURCES))
CAS_SMOKE_ELF := $(DISTDIR)/$(CAS_SMOKE_EXE).elf
CAS_SMOKE_TNS := $(DISTDIR)/$(CAS_SMOKE_EXE).tns

QFT_BENCH_SOURCES := \
    src/core/status.c \
    src/input/modifier.c \
    src/input/pointer.c \
    src/gfx/gfx.c \
    src/ir/ir.c \
    src/ir/order.c \
    src/ir/text.c \
    src/cas/num.c \
    src/cas/engine.c \
    src/cas/simplify.c \
    src/cas/diff.c \
    src/cas/integrate.c \
    src/cas/normal.c \
    src/cas/reduce.c \
    src/qft/lorentz.c \
    src/qft/dirac.c \
    tests/device/qft_bench.c \
    src/platform/ndless/platform_ndless.c \
    src/platform/ndless/crt_compat.c
QFT_BENCH_OBJECTS := \
    $(patsubst %.c,$(QFT_BENCH_BUILDDIR)/%.o,$(QFT_BENCH_SOURCES))
QFT_BENCH_ELF := $(DISTDIR)/$(QFT_BENCH_EXE).elf
QFT_BENCH_TNS := $(DISTDIR)/$(QFT_BENCH_EXE).tns

.PHONY: all clean size-report symbol-report ir-link-check exact-link-check \
        tensor-link-check \
        cas-link-check algebraic-link-check geom-link-check ym-link-check \
        eval-link-check \
        cas-smoke qft-bench check-sdk

all: $(TNS)

check-sdk:
	@command -v $(GCC) >/dev/null 2>&1 || { \
	    echo "error: $(GCC) not found."; \
	    echo "       run tools/bootstrap-ndless.sh, then"; \
	    echo "       eval \"\$$(tools/bootstrap-ndless.sh --env-only)\""; \
	    exit 1; }
	@command -v $(GXX) >/dev/null 2>&1 || { \
	    echo "error: $(GXX) not found."; \
	    exit 1; }
	@test -f $(NMARKDOWN_ROOT)/include/nmarkdown/math/math_system.h || { \
	    echo "error: nMarkdown submodule is missing."; \
	    echo "       run git submodule update --init --recursive"; \
	    exit 1; }

$(BUILDDIR)/%.o: %.c | check-sdk
	@mkdir -p $(dir $@)
	$(GCC) $(GCCFLAGS) -MMD -MP -c $< -o $@

$(BUILDDIR)/%.o: %.cpp | check-sdk
	@mkdir -p $(dir $@)
	$(GXX) $(CXXFLAGS) -MMD -MP -c $< -o $@

$(BUILDDIR)/%.o: %.cc | check-sdk
	@mkdir -p $(dir $@)
	$(GXX) $(filter-out -Wall -Wextra -Wpedantic,$(CXXFLAGS)) -MMD -MP -c $< -o $@

# A header change must rebuild every object that includes it. An edit to
# notebook_internal.h once shipped a binary whose document codec and renderer
# disagreed about sizeof(notebook_cell): documents loaded, then drew as empty.
-include $(OBJECTS:.o=.d)
-include $(CAS_SMOKE_OBJECTS:.o=.d)
-include $(QFT_BENCH_OBJECTS:.o=.d)

$(ELF): $(OBJECTS)
	@mkdir -p $(DISTDIR)
	$(GXX) $^ -o $@ $(LDFLAGS)

$(TNS): $(ELF)
	$(GENZEHN) --input $< --output $@.zehn $(ZEHNFLAGS)
	make-prg $@.zehn $@
	@rm -f $@.zehn
	@echo
	@$(MAKE) --no-print-directory size-report

size-report: $(TNS)
	@tools/size-report.sh $(TNS) $(ELF)

symbol-report: $(ELF)
	@tools/symbol-report.sh $(ELF)

# Deliberately not dependencies of `all`. The probes must never be linked into
# the product, or the size report would measure a probe instead.
ir-link-check: check-sdk
	@tools/link-check.sh ir

exact-link-check: check-sdk
	@tools/link-check.sh exact

cas-link-check: check-sdk
	@tools/link-check.sh cas

algebraic-link-check: check-sdk
	@tools/link-check.sh algebraic

geom-link-check: check-sdk
	@tools/link-check.sh geom

ym-link-check: check-sdk
	@tools/link-check.sh ym

color-link-check: check-sdk
	@tools/link-check.sh color

eval-link-check: check-sdk
	@tools/link-check.sh eval

tensor-link-check: check-sdk
	@tools/tensor-link-check.sh

$(CAS_SMOKE_BUILDDIR)/%.o: %.c | check-sdk
	@mkdir -p $(dir $@)
	$(GCC) $(GCCFLAGS) -MMD -MP -c $< -o $@

$(CAS_SMOKE_ELF): $(CAS_SMOKE_OBJECTS)
	@mkdir -p $(DISTDIR)
	$(LD) $^ -o $@ $(LDFLAGS)

$(CAS_SMOKE_TNS): $(CAS_SMOKE_ELF)
	$(GENZEHN) --input $< --output $@.zehn \
	    --name "Phy CAS Smoke" --version 1 --ndless-rev-min 2022 \
	    --uses-lcd-blit 1 --240x320-support 0
	make-prg $@.zehn $@
	@rm -f $@.zehn
	@tools/size-report.sh $@ $(CAS_SMOKE_ELF)

$(QFT_BENCH_BUILDDIR)/%.o: %.c | check-sdk
	@mkdir -p $(dir $@)
	$(GCC) $(GCCFLAGS) -MMD -MP -c $< -o $@

$(QFT_BENCH_ELF): $(QFT_BENCH_OBJECTS)
	@mkdir -p $(DISTDIR)
	$(LD) $^ -o $@ $(LDFLAGS)

$(QFT_BENCH_TNS): $(QFT_BENCH_ELF)
	$(GENZEHN) --input $< --output $@.zehn \
	    --name "Phy QFT Bench" --author "Phy-nspire contributors" \
	    --version 1 --ndless-min 31 --ndless-rev-min 2022 \
	    --clickpad-support true --color-support true \
	    --uses-lcd-blit true --240x320-support false --compress
	make-prg $@.zehn $@
	@rm -f $@.zehn
	@tools/size-report.sh $@ $(QFT_BENCH_ELF)

qft-bench: $(QFT_BENCH_TNS)

cas-smoke: $(CAS_SMOKE_TNS)

clean:
	rm -rf $(BUILDDIR) $(CAS_SMOKE_BUILDDIR) $(QFT_BENCH_BUILDDIR) \
	    $(DISTDIR) build/arm-linkcheck \
	    build/arm-tensor-linkcheck
