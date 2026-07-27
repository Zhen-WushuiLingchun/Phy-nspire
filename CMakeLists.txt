# Host build for Phy-nspire.
#
# This build never produces a calculator binary. It exists so that the portable
# core -- drawing, the platform contract, the application shell -- can be
# compiled, tested, and profiled on a desktop. The device build is driven by
# the top-level Makefile through the Ndless SDK.

cmake_minimum_required(VERSION 3.16)
project(phy-nspire C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "" FORCE)
endif()

option(PHY_WERROR "Treat warnings as errors" ON)

add_library(phy_warnings INTERFACE)
if(MSVC)
    target_compile_options(phy_warnings INTERFACE /W4 /utf-8)
    if(PHY_WERROR)
        target_compile_options(phy_warnings INTERFACE /WX)
    endif()
else()
    target_compile_options(phy_warnings INTERFACE
        -Wall -Wextra -Wshadow -Wconversion -Wsign-conversion -Wpointer-arith
        -Wcast-qual
        $<$<COMPILE_LANGUAGE:C>:-Wstrict-prototypes>
        $<$<COMPILE_LANGUAGE:C>:-Wmissing-prototypes>)
    if(PHY_WERROR)
        target_compile_options(phy_warnings INTERFACE -Werror)
    endif()
endif()

set(NMARKDOWN_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/third_party/nmarkdown")
if(NOT EXISTS "${NMARKDOWN_ROOT}/include/nmarkdown/math/math_system.h")
    message(FATAL_ERROR
        "nMarkdown submodule is missing. Run: git submodule update --init --recursive")
endif()

# The notebook reuses only nMarkdown's mathematical typesetter and the
# transitive font/rendering closure. Reader, browser, search, MD4C, and
# platform adapters are deliberately excluded.
add_library(nmarkdown_freetype STATIC
    ${NMARKDOWN_ROOT}/third_party/freetype/src/base/ftsystem.c
    ${NMARKDOWN_ROOT}/third_party/freetype/src/base/ftdebug.c
    ${NMARKDOWN_ROOT}/third_party/freetype/src/base/ftinit.c
    ${NMARKDOWN_ROOT}/third_party/freetype/src/base/ftbase.c
    ${NMARKDOWN_ROOT}/third_party/freetype/src/base/ftbitmap.c
    ${NMARKDOWN_ROOT}/third_party/freetype/src/base/ftmm.c
    ${NMARKDOWN_ROOT}/third_party/freetype/src/sfnt/sfnt.c
    ${NMARKDOWN_ROOT}/third_party/freetype/src/truetype/truetype.c
    ${NMARKDOWN_ROOT}/third_party/freetype/src/cff/cff.c
    ${NMARKDOWN_ROOT}/third_party/freetype/src/psaux/psaux.c
    ${NMARKDOWN_ROOT}/third_party/freetype/src/pshinter/pshinter.c
    ${NMARKDOWN_ROOT}/third_party/freetype/src/psnames/psnames.c
    ${NMARKDOWN_ROOT}/third_party/freetype/src/smooth/smooth.c
    ${NMARKDOWN_ROOT}/third_party/freetype/src/autofit/autofit.c)
target_include_directories(nmarkdown_freetype PUBLIC
    ${NMARKDOWN_ROOT}/third_party/freetype/nmarkdown
    ${NMARKDOWN_ROOT}/third_party/freetype/include)
target_compile_definitions(nmarkdown_freetype PRIVATE FT2_BUILD_LIBRARY)
if(MSVC)
    target_compile_options(nmarkdown_freetype PRIVATE /utf-8)
else()
    target_compile_options(nmarkdown_freetype PRIVATE -Os)
endif()

add_library(nmarkdown_harfbuzz STATIC
    ${NMARKDOWN_ROOT}/third_party/harfbuzz/src/harfbuzz.cc)
target_include_directories(nmarkdown_harfbuzz PUBLIC
    ${NMARKDOWN_ROOT}/third_party/harfbuzz/src)
target_compile_definitions(nmarkdown_harfbuzz PRIVATE
    HB_TINY=1 NMARKDOWN_HARFBUZZ_MATH=1)
if(MSVC)
    target_compile_options(nmarkdown_harfbuzz PRIVATE /utf-8)
else()
    target_compile_options(nmarkdown_harfbuzz PRIVATE -Os)
endif()

add_library(nmarkdown_math STATIC
    ${NMARKDOWN_ROOT}/src/document/unicode.cpp
    ${NMARKDOWN_ROOT}/src/document/utf8.cpp
    ${NMARKDOWN_ROOT}/src/generated/core_font_pack.cpp
    ${NMARKDOWN_ROOT}/src/generated/core_math_font.cpp
    ${NMARKDOWN_ROOT}/src/generated/unicode_tables.cpp
    ${NMARKDOWN_ROOT}/src/math/math_atoms.cpp
    ${NMARKDOWN_ROOT}/src/math/math_lexer.cpp
    ${NMARKDOWN_ROOT}/src/math/math_layout.cpp
    ${NMARKDOWN_ROOT}/src/math/math_macros.cpp
    ${NMARKDOWN_ROOT}/src/math/math_parser.cpp
    ${NMARKDOWN_ROOT}/src/math/math_system.cpp
    ${NMARKDOWN_ROOT}/src/render/primitives.cpp
    ${NMARKDOWN_ROOT}/src/render/surface565.cpp
    ${NMARKDOWN_ROOT}/src/text/compositor.cpp
    ${NMARKDOWN_ROOT}/src/text/font.cpp
    ${NMARKDOWN_ROOT}/src/text/font_pack.cpp
    ${NMARKDOWN_ROOT}/src/text/glyph_cache.cpp
    ${NMARKDOWN_ROOT}/src/text/harfbuzz_shaper.cpp
    ${NMARKDOWN_ROOT}/src/text/text_renderer.cpp
    ${NMARKDOWN_ROOT}/src/text/text_system.cpp)
target_include_directories(nmarkdown_math PUBLIC ${NMARKDOWN_ROOT}/include)
target_link_libraries(nmarkdown_math PUBLIC
    nmarkdown_freetype nmarkdown_harfbuzz)
target_compile_definitions(nmarkdown_math PRIVATE
    HB_TINY=1 NMARKDOWN_HARFBUZZ_MATH=1)
if(MSVC)
    target_compile_options(nmarkdown_math PRIVATE /utf-8 /EHsc /GR-)
else()
    target_compile_options(nmarkdown_math PRIVATE
        -Os -Wall -Wextra -Wpedantic -fexceptions -fno-rtti)
endif()

# Portable core: no platform backend, no entry point.
add_library(phy_core STATIC
    src/core/status.c
    src/input/modifier.c
    src/input/pointer.c
    src/gfx/gfx.c
    src/render/formula_bridge.cpp
    src/render/ir_math_tree.cpp
    src/render/math_layout.c
    src/notebook/document.c
    src/notebook/palette.c
    src/notebook/source.c
    src/notebook/notebook.c
    src/notebook/workspace.c
    src/eval/env.c
    src/eval/dispatch.c
    src/eval/display.c
    src/storage/storage.c
    src/app/app.c
    src/ir/ir.c
    src/ir/order.c
    src/ir/text.c
    src/tensor/chart.c
    src/tensor/symmetry.c
    src/tensor/tensor.c
    src/tensor/ops.c
    src/gr/gr.c
    src/lie/lie.c
    src/qft/scalar.c
    src/qft/lorentz.c
    src/qft/dirac.c
    src/qft/mandelstam.c
    src/qft/color.c
    src/cas/num.c
    src/cas/engine.c
    src/cas/simplify.c
    src/cas/diff.c
    src/cas/integrate.c
    src/cas/normal.c
    src/geom/manifold.c
    src/geom/form.c
    src/geom/exterior.c
    src/geom/metric.c
    src/qft/yang_mills.c
)
target_include_directories(phy_core PUBLIC include PRIVATE
    src/gfx src/ir src/tensor src/cas src/geom src/eval)
target_link_libraries(phy_core PRIVATE phy_warnings nmarkdown_math)
if(MSVC)
    target_compile_options(phy_core PRIVATE $<$<COMPILE_LANGUAGE:CXX>:/EHsc>
                                            $<$<COMPILE_LANGUAGE:CXX>:/GR->)
else()
    target_compile_options(phy_core PRIVATE
        $<$<COMPILE_LANGUAGE:CXX>:-fexceptions>
        $<$<COMPILE_LANGUAGE:CXX>:-fno-rtti>)
endif()

# Host platform backend: offscreen framebuffer, scripted input, virtual clock.
add_library(phy_platform_host STATIC
    src/platform/host/platform_host.c
    src/platform/host/storage_host.c)
target_include_directories(phy_platform_host PUBLIC include PRIVATE src/storage)
target_link_libraries(phy_platform_host PRIVATE phy_warnings)

add_executable(phy-host src/app/main_host.c)
target_link_libraries(phy-host PRIVATE phy_core phy_platform_host phy_warnings)

add_executable(phy-dump-font src/tools/dump_font.c)
target_link_libraries(phy-dump-font PRIVATE phy_core phy_platform_host phy_warnings)

include(CTest)
if(BUILD_TESTING)
    set(PHY_FIXTURE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures")
    configure_file("${PHY_FIXTURE_DIR}/baseline_frame.digest"
                   "${CMAKE_CURRENT_BINARY_DIR}/baseline_frame.digest"
                   COPYONLY)
    configure_file("${PHY_FIXTURE_DIR}/notebook_frame.digest"
                   "${CMAKE_CURRENT_BINARY_DIR}/notebook_frame.digest"
                   COPYONLY)
    # The golden curvature corpus is read by test_gr_corpus at run time rather
    # than transcribed into C, so the committed artifact is what is compared
    # against. Copied here for the same reason as the frame digests: MSVC's
    # narrow fopen cannot open the absolute source path when the checkout
    # contains non-ASCII characters.
    configure_file("${CMAKE_CURRENT_SOURCE_DIR}/research/corpus/gr_golden.json"
                   "${CMAKE_CURRENT_BINARY_DIR}/gr_golden.json"
                   COPYONLY)

    foreach(suite test_gfx test_formula test_modifier test_pointer test_source test_palette test_platform
                  test_smoke test_document test_storage test_workspace
                  test_notebook test_ir test_tensor test_gr test_lie
                  test_qft_scalar test_yang_mills test_cas test_geom
                  test_geom_metric test_eval test_lorentz test_mandelstam
                  test_color)
        add_executable(${suite} tests/${suite}.c)
        target_link_libraries(${suite} PRIVATE phy_core phy_platform_host phy_warnings)
        target_include_directories(${suite} PRIVATE tests)
        target_compile_definitions(${suite} PRIVATE
            # Keep runtime paths ASCII-only. MSVC's narrow fopen cannot open
            # the absolute UTF-8 source path when the checkout contains
            # Chinese characters, while CTest has already established the
            # correct build working directory for these relative names.
            PHY_FIXTURE_DIR="."
            PHY_ARTIFACT_DIR=".")
        if(MSVC)
            # The portable tests intentionally exercise ISO C fopen paths.
            # MSVC's C4996 is a vendor deprecation, not a safety diagnostic;
            # keep all other warnings fatal without rewriting the tests around
            # the non-portable fopen_s signature.
            target_compile_definitions(
                ${suite} PRIVATE _CRT_SECURE_NO_WARNINGS)
        endif()
        add_test(NAME ${suite} COMMAND ${suite})
    endforeach()

    # The general-relativity corpus suite. It has its own reader for
    # research/corpus/gr_golden.json and for the SymPy subset the corpus is
    # written in; both are test-only and are kept out of phy_core.
    add_executable(test_gr_corpus
        tests/test_gr_corpus.c
        tests/corpus/corpus_json.c
        tests/corpus/corpus_expr.c)
    target_link_libraries(test_gr_corpus
        PRIVATE phy_core phy_platform_host phy_warnings)
    target_include_directories(test_gr_corpus PRIVATE tests)
    target_compile_definitions(test_gr_corpus PRIVATE PHY_FIXTURE_DIR=".")
    add_test(NAME test_gr_corpus COMMAND test_gr_corpus)

    # End-to-end: the shipped entry point must run and exit 0 unattended.
    add_test(NAME host_app_runs COMMAND phy-host --frames 2)

    # tools/symbol-report.sh used to exit 141 on any binary with more sized
    # symbols than the requested count. The test stubs nm, so it needs no
    # cross-compiler and runs wherever bash does.
    find_program(PHY_BASH bash)
    # Windows' System32/bash.exe is the WSL launcher, not a shell that can
    # consume CMake's native F:/... paths. The same script is covered by the
    # Linux/WSL suite; do not register a guaranteed-broken Windows test.
    if(PHY_BASH AND NOT WIN32)
        add_test(NAME symbol_report
            COMMAND ${PHY_BASH}
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_symbol_report.sh"
                "${CMAKE_CURRENT_SOURCE_DIR}")
        add_test(NAME bootstrap_env
            COMMAND ${PHY_BASH}
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_bootstrap_env.sh"
                "${CMAKE_CURRENT_SOURCE_DIR}")
    endif()

    # QFT/gauge golden cases. The oracle deliberately links against nothing
    # else: it certifies the identities in docs/references/QFT_GAUGE.md from an
    # explicit matrix representation and must not acquire a dependency on the
    # portable core, which would let a core regression mask a physics one.
    add_executable(test_qo_golden
        tests/oracle/test_qo_golden.c
        tests/oracle/qo_dirac.c
        tests/oracle/qo_color.c
    )
    target_include_directories(test_qo_golden PRIVATE tests tests/oracle)
    target_link_libraries(test_qo_golden PRIVATE phy_warnings)
    if(NOT MSVC)
        target_link_libraries(test_qo_golden PRIVATE m)
    endif()
    add_test(NAME test_qo_golden COMMAND test_qo_golden)

    # The native Dirac engine is checked against the oracle rather than
    # against transcribed expected values, so this one suite links both. The
    # dependency runs one way only: the oracle target above still links
    # nothing from phy_core, so a core regression cannot mask a physics one.
    add_executable(test_dirac tests/test_dirac.c tests/oracle/qo_dirac.c)
    target_include_directories(test_dirac PRIVATE tests tests/oracle)
    target_link_libraries(test_dirac PRIVATE phy_core phy_platform_host
                                             phy_warnings)
    if(NOT MSVC)
        target_link_libraries(test_dirac PRIVATE m)
    endif()
    add_test(NAME test_dirac COMMAND test_dirac)
endif()

# Host size report. The device budget is enforced by tools/size-report.sh
# against the .tns; this target keeps the host build honest about core growth.
add_custom_target(size-report
    COMMAND ${CMAKE_COMMAND} -E echo "== host artifact sizes =="
    COMMAND ${CMAKE_COMMAND} -E env size $<TARGET_FILE:phy-host>
    DEPENDS phy-host
    VERBATIM)
