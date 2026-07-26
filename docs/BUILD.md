# Building Phy-nspire

There are two builds and they are deliberately separate.

| Build | Entry point | Produces | Purpose |
| --- | --- | --- | --- |
| Host | `CMakeLists.txt` | `phy-host`, test binaries | Compile, test, and profile the portable core on a desktop |
| Device | `Makefile` | `dist/phy-nspire.tns` | The calculator application |

The host build never produces a calculator binary, and the device build never
compiles the host backend. Exactly one platform backend is linked into any
given binary; see `include/phy/platform.h`. Both builds require the pinned
nMarkdown submodule:

```sh
git submodule update --init --recursive
```

## Host build

Needs C11 and C++17 compilers and CMake 3.16 or newer. FreeType and HarfBuzz
are built from the pinned submodule; no system font libraries are required.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build -j
ctest --test-dir build --output-on-failure
```

Warnings are errors by default. Pass `-DPHY_WERROR=OFF` if a new compiler
introduces warnings you need to work through incrementally.

Useful binaries:

```sh
./build/phy-host --frames 2          # run the shell headless and print telemetry
./build/phy-host --print-digest      # baseline frame digest
./build/phy-dump-font                # render the built-in font as ASCII art
./build/phy-dump-font "some text"    # render one string
```

### Framebuffer fixture

`tests/fixtures/baseline_frame.digest` pins the preserved hardware diagnostic.
`tests/fixtures/notebook_frame.digest` separately pins the production notebook
viewport. `test_smoke` and `test_notebook` write an actual PPM beside the host
build when either rendering drifts, so the difference can be inspected.

After an intentional change to the baseline frame, regenerate it:

```sh
./build/phy-host --print-digest > tests/fixtures/baseline_frame.digest
```

The fixture records the *host* rendering. The device draws the same frame from
the same code, except that the title bar reports `ndless` instead of `host`.

## Device build

### Prerequisites

A Linux or WSL environment with `git`, `curl`, `make`, and a host C/C++
compiler. On Windows, use WSL; the Ndless SDK does not build natively under
PowerShell.

### Bootstrap the SDK

```sh
tools/bootstrap-ndless.sh
eval "$(tools/bootstrap-ndless.sh --env-only)"
```

This clones Ndless at the commit pinned in `research/upstreams.lock.json`,
verifies the checkout, obtains an ARM cross toolchain, and builds the SDK. It
writes only to `$PHY_SDK_ROOT` (default `~/.phy-nspire`) and never into the
repository.

Two toolchain modes:

- `--toolchain=prebuilt` (default) downloads the pinned Arm GNU Toolchain
  release. Needs no root. This is the mode Phase 0 was verified with.
- `--toolchain=source` runs Ndless's own `build_toolchain.sh`. This is the
  upstream-supported path, but it needs root once to install `libgmp-dev`,
  `libmpfr-dev`, `libmpc-dev`, `texinfo`, `flex`, and `bison`, and takes far
  longer.

The toolchain pin is not arbitrary. `ndless-sdk/system/ldscript` uses the
linker-script function `REVERSE()`, which requires **binutils 2.44 or newer**.
An older prebuilt toolchain fails with `ldscript:11: syntax error`.

The SDK's `genzehn` also needs `boost::program_options` on the host. If the
distribution package is missing, the bootstrap script stages Boost into
`$PHY_SDK_ROOT/deps` and links it statically, so no root is needed there
either. Installing `libboost-program-options-dev` system-wide works too and
takes precedence.

### Build

```sh
make                # dist/phy-nspire.tns, then a size report
make size-report
make symbol-report
make cas-smoke      # dist/phy-cas-smoke.tns, visible CAS acceptance test
make clean
make DEBUG=TRUE     # -O0 -g instead of -Os
```

`make` fails with a clear message if `nspire-gcc`/`nspire-g++` is not on
`PATH`, or if the nMarkdown submodule has not been initialized.

### Reports

`tools/size-report.sh` measures the `.tns` — the file that actually occupies
calculator flash — against the 5 MB target and the 6 MB ceiling from
`docs/ARCHITECTURE.md`. It exits non-zero above the ceiling, so it can gate a
release.

`tools/symbol-report.sh` lists the largest symbols in the ELF, so size growth
can be attributed rather than argued about.

## Installing and verifying on the calculator

Requires a TI-Nspire CX II CAS running OS 6.4.0.74 with Ndless r2022 already
installed.

1. Create this tree in Documents:

   ```text
   phy-nspire/
   ├── phy-nspire.tns
   ├── notebooks/
   ├── assets/
   └── examples/
   ```

2. Copy `dist/phy-nspire.tns` to `phy-nspire/phy-nspire.tns` with TI-Nspire
   Computer Link, `n-link`, or another libnspire client.
3. Open it from the Documents browser.
4. Confirm startup shows an empty `Untitled` notebook.
5. Use `+MD`, put `LaTeX` in the heading, press `TAB`, and enter
   `$$R=g^{\mu\nu}R_{\mu\nu}$$`. Press `ESC` and confirm a centered,
   two-dimensional equation with Greek indices and raised/lowered scripts.
6. Move a finger, lift it, then touch a different part of the touchpad. The
   pointer must continue from its last screen position; a new contact must not
   jump to an absolute mapped position.
7. Touch an input body and edit it with letters, digits, arithmetic keys,
   parentheses, arrows, and `DEL`. `RUN`/`ENTER` must execute the visible
   source, and a parse failure must preserve it.
8. Use the footer `+MD` and `+Math` buttons. Confirm the new cell is selected
   and enters edit mode; insert enough cells to make selection scroll.
9. Open `FILE`, save a notebook, create a new blank notebook, then open the
   saved document. Confirm the source, cell kinds, outputs, and selection
   round-trip.
10. Press `ESC` once to leave edit mode and again to return to Documents.

The exit is the part that matters most. After `ESC` the Documents browser must
come back rendering normally. The separate Phase 0 RGB/pointer diagnostic is
still available through the preserved `phy_app_draw_baseline` host fixture;
record its physical channel-order acceptance before changing `PHY_RGB565`.

### Native symbolic CAS acceptance test

The production shell now calls the CAS. The separate
`dist/phy-cas-smoke.tns` remains a denser regression screen for all seven
physical-device acceptance cases:

```sh
make cas-link-check
make cas-smoke
```

Open `phy-cas-smoke.tns` from Documents. It performs seven calculations on the
calculator itself: exact rational addition, coefficient collection,
differentiation, expansion, a trigonometric identity, a general-relativity
identity, and an inexact-real boundary check. The last case must report
`Unknown`, demonstrating that an inexact real atom was not guessed about
numerically. Accept the run only when all seven rows are green and the footer
reads `7/7 PASS`. Press `ESC` or `ENTER` to restore the Documents browser.
