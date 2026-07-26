# Building Phy-nspire

There are two builds and they are deliberately separate.

| Build | Entry point | Produces | Purpose |
| --- | --- | --- | --- |
| Host | `CMakeLists.txt` | `phy-host`, test binaries | Compile, test, and profile the portable core on a desktop |
| Device | `Makefile` | `dist/phy-nspire.tns` | The calculator application |

The host build never produces a calculator binary, and the device build never
compiles the host backend. Exactly one platform backend is linked into any
given binary; see `include/phy/platform.h`.

## Host build

Needs a C11 compiler and CMake 3.16 or newer. Nothing else.

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

`tests/fixtures/baseline_frame.digest` pins the baseline frame. `test_smoke`
fails if the rendered frame drifts, and writes
`build/baseline_frame_actual.ppm` so the difference can be inspected.

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
make clean
make DEBUG=TRUE     # -O0 -g instead of -Os
```

`make` fails with a clear message if `nspire-gcc` is not on `PATH`, which
means the `eval` line above was skipped.

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

1. Copy `dist/phy-nspire.tns` to the calculator with TI-Nspire Computer Link
   or `n-link`.
2. Open it from the Documents browser.
3. Confirm the baseline frame: a title bar reading `Phy-nspire 0.1.0` on the
   left and `ndless` on the right, a panel of framebuffer facts, and a
   red/green/blue strip.
4. Confirm the strip really is red, then green, then blue, left to right. Any
   other order means the panel is wired BGR and `PHY_RGB565` needs to change.
5. Move the pointer with the touchpad and confirm the crosshair tracks it.
6. Press `ESC`.

The exit is the part that matters most. After `ESC` the Documents browser must
come back rendering normally. Leftover garbage or a wrong-looking display
means `phy_platform_shutdown` did not restore the panel mode, which is the
defect Phase 0 exists to rule out.
