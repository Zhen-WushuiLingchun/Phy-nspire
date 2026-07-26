# Host build for Phy-nspire.
#
# This build never produces a calculator binary. It exists so that the portable
# core -- drawing, the platform contract, the application shell -- can be
# compiled, tested, and profiled on a desktop. The device build is driven by
# the top-level Makefile through the Ndless SDK.

cmake_minimum_required(VERSION 3.16)
project(phy-nspire C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "" FORCE)
endif()

option(PHY_WERROR "Treat warnings as errors" ON)

add_library(phy_warnings INTERFACE)
if(MSVC)
    target_compile_options(phy_warnings INTERFACE /W4)
    if(PHY_WERROR)
        target_compile_options(phy_warnings INTERFACE /WX)
    endif()
else()
    target_compile_options(phy_warnings INTERFACE
        -Wall -Wextra -Wshadow -Wconversion -Wsign-conversion
        -Wstrict-prototypes -Wmissing-prototypes -Wpointer-arith -Wcast-qual)
    if(PHY_WERROR)
        target_compile_options(phy_warnings INTERFACE -Werror)
    endif()
endif()

# Portable core: no platform backend, no entry point.
add_library(phy_core STATIC
    src/core/status.c
    src/gfx/gfx.c
    src/app/app.c
    src/ir/ir.c
    src/ir/order.c
    src/ir/text.c
    src/tensor/chart.c
    src/tensor/symmetry.c
    src/tensor/tensor.c
    src/cas/num.c
    src/cas/engine.c
    src/cas/simplify.c
    src/cas/diff.c
    src/cas/normal.c
)
target_include_directories(phy_core PUBLIC include PRIVATE
    src/gfx src/ir src/tensor src/cas)
target_link_libraries(phy_core PRIVATE phy_warnings)

# Host platform backend: offscreen framebuffer, scripted input, virtual clock.
add_library(phy_platform_host STATIC src/platform/host/platform_host.c)
target_include_directories(phy_platform_host PUBLIC include)
target_link_libraries(phy_platform_host PRIVATE phy_warnings)

add_executable(phy-host src/app/main_host.c)
target_link_libraries(phy-host PRIVATE phy_core phy_platform_host phy_warnings)

add_executable(phy-dump-font src/tools/dump_font.c)
target_link_libraries(phy-dump-font PRIVATE phy_core phy_platform_host phy_warnings)

include(CTest)
if(BUILD_TESTING)
    set(PHY_FIXTURE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures")

    foreach(suite test_gfx test_platform test_smoke test_ir test_tensor test_cas)
        add_executable(${suite} tests/${suite}.c)
        target_link_libraries(${suite} PRIVATE phy_core phy_platform_host phy_warnings)
        target_include_directories(${suite} PRIVATE tests)
        target_compile_definitions(${suite} PRIVATE
            PHY_FIXTURE_DIR="${PHY_FIXTURE_DIR}"
            PHY_ARTIFACT_DIR="${CMAKE_CURRENT_BINARY_DIR}")
        add_test(NAME ${suite} COMMAND ${suite})
    endforeach()

    # End-to-end: the shipped entry point must run and exit 0 unattended.
    add_test(NAME host_app_runs COMMAND phy-host --frames 2)

    # tools/symbol-report.sh used to exit 141 on any binary with more sized
    # symbols than the requested count. The test stubs nm, so it needs no
    # cross-compiler and runs wherever bash does.
    find_program(PHY_BASH bash)
    if(PHY_BASH)
        add_test(NAME symbol_report
            COMMAND ${PHY_BASH}
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_symbol_report.sh"
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
endif()

# Host size report. The device budget is enforced by tools/size-report.sh
# against the .tns; this target keeps the host build honest about core growth.
add_custom_target(size-report
    COMMAND ${CMAKE_COMMAND} -E echo "== host artifact sizes =="
    COMMAND ${CMAKE_COMMAND} -E env size $<TARGET_FILE:phy-host>
    DEPENDS phy-host
    VERBATIM)
