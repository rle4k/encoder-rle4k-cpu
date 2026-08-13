# Unit tests for rle4k_core
# Google Test: prefer deps/gtest/<pin> (headers + static /MT libs); fall back to
# a developer vcpkg x64-windows-static install when the pin is missing.

set(RLE4K_GTEST_INCLUDE "")
set(RLE4K_GTEST_LIB "")
set(RLE4K_GTEST_MAIN "")

set(_rle4k_deps_root "${CMAKE_SOURCE_DIR}/deps")
if(DEFINED RLE4K_DEPS_ROOT AND NOT "${RLE4K_DEPS_ROOT}" STREQUAL "")
    set(_rle4k_deps_root "${RLE4K_DEPS_ROOT}")
endif()

set(_rle4k_gtest_ver "1.17.0")
if(DEFINED ENV{RLE4K_DEPS_gtest_VERSION} AND NOT "$ENV{RLE4K_DEPS_gtest_VERSION}" STREQUAL "")
    set(_rle4k_gtest_ver "$ENV{RLE4K_DEPS_gtest_VERSION}")
elseif(EXISTS "${_rle4k_deps_root}/versions.json")
    file(READ "${_rle4k_deps_root}/versions.json" _rle4k_versions_raw)
    string(REGEX MATCH "\"gtest\"[ \t]*:[ \t]*\"([^\"]+)\"" _rle4k_gtest_m "${_rle4k_versions_raw}")
    if(CMAKE_MATCH_1)
        set(_rle4k_gtest_ver "${CMAKE_MATCH_1}")
    endif()
endif()

set(_rle4k_gtest_deps_dir "${_rle4k_deps_root}/gtest/${_rle4k_gtest_ver}")
set(_rle4k_gtest_roots "")
if(EXISTS "${_rle4k_gtest_deps_dir}/lib/gtest.lib"
   AND EXISTS "${_rle4k_gtest_deps_dir}/lib/gtest_main.lib")
    list(APPEND _rle4k_gtest_roots "${_rle4k_gtest_deps_dir}")
endif()
if(DEFINED RLE4K_VCPKG_INSTALLED AND EXISTS "${RLE4K_VCPKG_INSTALLED}/lib/gtest.lib")
    list(APPEND _rle4k_gtest_roots "${RLE4K_VCPKG_INSTALLED}")
endif()
if(DEFINED ENV{VCPKG_ROOT} AND EXISTS "$ENV{VCPKG_ROOT}/installed/x64-windows-static/lib/gtest.lib")
    list(APPEND _rle4k_gtest_roots "$ENV{VCPKG_ROOT}/installed/x64-windows-static")
endif()
if(EXISTS "E:/vcpkg/installed/x64-windows-static/lib/gtest.lib")
    list(APPEND _rle4k_gtest_roots "E:/vcpkg/installed/x64-windows-static")
endif()
if(EXISTS "D:/vcpkg/installed/x64-windows-static/lib/gtest.lib")
    list(APPEND _rle4k_gtest_roots "D:/vcpkg/installed/x64-windows-static")
endif()

foreach(_root IN LISTS _rle4k_gtest_roots)
    if(EXISTS "${_root}/lib/gtest.lib" AND EXISTS "${_root}/lib/gtest_main.lib")
        set(RLE4K_GTEST_INCLUDE "${_root}/include")
        set(RLE4K_GTEST_LIB     "${_root}/lib/gtest.lib")
        set(RLE4K_GTEST_MAIN    "${_root}/lib/gtest_main.lib")
        break()
    endif()
endforeach()

if(NOT EXISTS "${RLE4K_GTEST_LIB}" OR NOT EXISTS "${RLE4K_GTEST_MAIN}")
    message(FATAL_ERROR
        "gtest static libs not found under deps/gtest/${_rle4k_gtest_ver} or vcpkg x64-windows-static\n"
        "  Author harvest: .\\scripts\\publish-deps.ps1 -SkipBase\n"
        "  Or install once: vcpkg install gtest:x64-windows-static")
endif()
message(STATUS "Using gtest: ${RLE4K_GTEST_LIB}")
add_executable(redcli_tests
    test_rle4k_codec.cpp
    test_bitmap.cpp
    test_buffer_mgr.cpp
    test_format_encoder.cpp
    test_dif_loader.cpp
    test_rasterizer.cpp
    test_config.cpp
    test_stats_pipeline.cpp
)

target_include_directories(redcli_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/redcli
    ${CMAKE_SOURCE_DIR}/redcli/libcore
    ${CMAKE_CURRENT_SOURCE_DIR}/helpers
    ${RLE4K_GTEST_INCLUDE}
    ${LZO_INCLUDE_DIR}
    "${RLE4K_VCPKG_INSTALLED}/include"
    "${RLE4K_VCPKG_INSTALLED}/include/gperftools"
)

target_link_libraries(redcli_tests PRIVATE
    rle4k_core
    "${RLE4K_GTEST_MAIN}"
    "${RLE4K_GTEST_LIB}"
    ZLIB::ZLIB
    ${LZO_LIBRARY}
    Snappy::snappy
    ${RLE4K_ZSTD_TARGET}
    unofficial::brotli::brotlienc
    unofficial::brotli::brotlidec
    unofficial::brotli::brotlicommon
    "${TCMALLOC_LIBRARY}"
)

if(WIN32)
    target_link_libraries(redcli_tests PRIVATE ws2_32)
    target_compile_definitions(redcli_tests PRIVATE
        RLE4K_TCMALLOC_INCLUDE=1
        USE_TCMALLOC
        PERFTOOLS_DLL_DECL=)
    target_link_options(redcli_tests PRIVATE "/INCLUDE:??0TCMallocGuard@@QEAA@XZ")
endif()

# PDB for coverage tools (OpenCppCoverage / VS CodeCoverage)
if(MSVC)
    target_compile_options(redcli_tests PRIVATE /Zi)
    target_link_options(redcli_tests PRIVATE /DEBUG)
    if(RLE4K_COVERAGE)
        target_compile_options(redcli_tests PRIVATE /Od /Ob0)
    endif()
endif()

add_test(NAME redcli_unit_tests COMMAND redcli_tests --gtest_brief=1)

add_custom_target(check
    COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure -C $<CONFIG>
    DEPENDS redcli_tests
    COMMENT "Run unit tests (ctest)"
)
