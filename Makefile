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
#   tensor-link-check  prove the component tensor core links on device
#   clean

DEBUG ?= FALSE

GCC := nspire-gcc
LD := nspire-ld
GENZEHN := genzehn

EXE := phy-nspire
DISTDIR := dist
BUILDDIR := build/arm

# Ndless links a C++ runtime regardless, so -lstdc++ comes from nspire-ld.
GCCFLAGS := -Wall -Wextra -Wshadow -Wpointer-arith -std=c11 -marm \
            -ffunction-sections -fdata-sections -Iinclude -Isrc/gfx -Isrc/ir \
            -Isrc/tensor
# The Ndless ldscript intentionally produces a single RWX load segment, which
# binutils 2.39+ warns about. Ndless's own toolchain build disables that
# warning at configure time; we silence it at link time instead.
LDFLAGS := -Wl,--gc-sections -Wl,--no-warn-rwx-segments
ZEHNFLAGS := --name "Phy-nspire" --version 1

ifeq ($(DEBUG),FALSE)
    GCCFLAGS += -Os -DNDEBUG
else
    GCCFLAGS += -O0 -g
endif

# Explicit source list. A wildcard would silently pull in the host backend,
# which must never reach the device binary.
SOURCES := \
    src/core/status.c \
    src/gfx/gfx.c \
    src/app/app.c \
    src/ir/ir.c \
    src/ir/order.c \
    src/ir/text.c \
    src/tensor/chart.c \
    src/tensor/symmetry.c \
    src/tensor/tensor.c \
    src/app/main_ndless.c \
    src/platform/ndless/platform_ndless.c \
    src/platform/ndless/crt_compat.c

OBJECTS := $(patsubst %.c,$(BUILDDIR)/%.o,$(SOURCES))
ELF := $(DISTDIR)/$(EXE).elf
TNS := $(DISTDIR)/$(EXE).tns

.PHONY: all clean size-report symbol-report ir-link-check tensor-link-check \
        check-sdk

all: $(TNS)

check-sdk:
	@command -v $(GCC) >/dev/null 2>&1 || { \
	    echo "error: $(GCC) not found."; \
	    echo "       run tools/bootstrap-ndless.sh, then"; \
	    echo "       eval \"\$$(tools/bootstrap-ndless.sh --env-only)\""; \
	    exit 1; }

$(BUILDDIR)/%.o: %.c | check-sdk
	@mkdir -p $(dir $@)
	$(GCC) $(GCCFLAGS) -c $< -o $@

$(ELF): $(OBJECTS)
	@mkdir -p $(DISTDIR)
	$(LD) $^ -o $@ $(LDFLAGS)

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

# Deliberately not a dependency of `all`. The probe must never be linked into
# the product, or the size report would measure the probe instead.
ir-link-check: check-sdk
	@tools/ir-link-check.sh

tensor-link-check: check-sdk
	@tools/tensor-link-check.sh

clean:
	rm -rf $(BUILDDIR) $(DISTDIR) build/arm-linkcheck \
	    build/arm-tensor-linkcheck
