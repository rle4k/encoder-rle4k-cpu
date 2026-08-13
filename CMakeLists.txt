cmake_minimum_required(VERSION 3.16)
project(RLE4K-Reproducibility VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(RLE4K_BUILD_TESTS "Build Google Test unit tests" ON)
option(RLE4K_COVERAGE "Compile library/tests with /Od /Ob0 for accurate line coverage" OFF)

# ============================================================================
# Compiler options
# ============================================================================
if(MSVC)
    add_compile_options(/W3 /wd4819 /EHsc)
    # Match redtt: /MT for all configs (static gperftools is Release-Override /MT).
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded")
    if(RLE4K_COVERAGE)
        # Replace Release /O2 so line coverage maps to source (inlining hides static helpers).
        string(REPLACE "/O2" "/Od" CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE}")
        string(REPLACE "/Ob2" "/Ob0" CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE}")
        string(REPLACE "/O2" "/Od" CMAKE_CXX_FLAGS_RELWITHDEBINFO "${CMAKE_CXX_FLAGS_RELWITHDEBINFO}")
        string(REPLACE "/Ob1" "/Ob0" CMAKE_CXX_FLAGS_RELWITHDEBINFO "${CMAKE_CXX_FLAGS_RELWITHDEBINFO}")
        if(NOT CMAKE_CXX_FLAGS_RELEASE MATCHES "/Od")
            string(APPEND CMAKE_CXX_FLAGS_RELEASE " /Od /Ob0")
        endif()
        message(STATUS "RLE4K_COVERAGE=ON — Release CXX flags: ${CMAKE_CXX_FLAGS_RELEASE}")
    endif()
else()
    add_compile_options(-Wall -Wextra)
endif()

# ============================================================================
# Compression libraries: prefer deps/ vendor (P6); optional vcpkg fallback
# ============================================================================
include(${CMAKE_SOURCE_DIR}/cmake/DepsVendoredCompression.cmake)

if(NOT RLE4K_DEPS_VENDOR_OK)
find_package(ZLIB REQUIRED)
find_package(Snappy CONFIG REQUIRED)
find_package(zstd CONFIG REQUIRED)
find_package(unofficial-brotli CONFIG REQUIRED)

# Helpers: prefer config-specific vcpkg dirs under the build tree.
set(_RLE4K_VCPKG_REL "${CMAKE_BINARY_DIR}/vcpkg_installed/${VCPKG_TARGET_TRIPLET}/lib")
set(_RLE4K_VCPKG_DBG "${CMAKE_BINARY_DIR}/vcpkg_installed/${VCPKG_TARGET_TRIPLET}/debug/lib")

find_path(LZO_INCLUDE_DIR NAMES lzo/lzo1x.h REQUIRED)
find_library(LZO_LIBRARY_RELEASE NAMES lzo2 HINTS "${_RLE4K_VCPKG_REL}" NO_DEFAULT_PATH)
if(NOT LZO_LIBRARY_RELEASE)
    find_library(LZO_LIBRARY_RELEASE NAMES lzo2 REQUIRED)
endif()
find_library(LZO_LIBRARY_DEBUG NAMES lzo2 HINTS "${_RLE4K_VCPKG_DBG}" NO_DEFAULT_PATH)
if(NOT LZO_LIBRARY_DEBUG)
    set(LZO_LIBRARY_DEBUG "${LZO_LIBRARY_RELEASE}")
endif()

find_library(TCMALLOC_LIBRARY_RELEASE NAMES tcmalloc_minimal libtcmalloc_minimal
    HINTS "${_RLE4K_VCPKG_REL}" NO_DEFAULT_PATH)
if(NOT TCMALLOC_LIBRARY_RELEASE)
    find_library(TCMALLOC_LIBRARY_RELEASE NAMES tcmalloc_minimal libtcmalloc_minimal REQUIRED)
endif()
find_library(TCMALLOC_LIBRARY_DEBUG NAMES tcmalloc_minimal libtcmalloc_minimal
    HINTS "${_RLE4K_VCPKG_DBG}" NO_DEFAULT_PATH)
if(NOT TCMALLOC_LIBRARY_DEBUG)
    set(TCMALLOC_LIBRARY_DEBUG "${TCMALLOC_LIBRARY_RELEASE}")
endif()
message(STATUS "lzo2 (release):     ${LZO_LIBRARY_RELEASE}")
message(STATUS "tcmalloc (release): ${TCMALLOC_LIBRARY_RELEASE}")

# tcmalloc — same pattern as encoders/encoder-redtt (redtt.vcxproj / build-redtt.ps1):
#   static gperftools:x64-windows-static (Release-Override), pin full path,
#   USE_TCMALLOC + PERFTOOLS_DLL_DECL=, /INCLUDE:??0TCMallocGuard@@QEAA@XZ
#   (static Override has no __tcmalloc; no DLL deploy).
set(_rle4k_vcpkg_candidates "")
if(DEFINED ENV{VCPKG_LOCAL_ROOT} AND NOT "$ENV{VCPKG_LOCAL_ROOT}" STREQUAL "")
    list(APPEND _rle4k_vcpkg_candidates "$ENV{VCPKG_LOCAL_ROOT}")
endif()
# Repo is <ezdi>/papers/encoder-rle4k-cpu — two parents up to monorepo root.
if(EXISTS "${CMAKE_SOURCE_DIR}/../../vcpkg")
    list(APPEND _rle4k_vcpkg_candidates "${CMAKE_SOURCE_DIR}/../../vcpkg")
endif()
if(DEFINED ENV{VCPKG_ROOT} AND NOT "$ENV{VCPKG_ROOT}" STREQUAL "")
    list(APPEND _rle4k_vcpkg_candidates "$ENV{VCPKG_ROOT}")
endif()
list(APPEND _rle4k_vcpkg_candidates "D:/vcpkg")
# Only probe E: when the drive exists (CI hosts may lack E:).
if(EXISTS "E:/")
    list(APPEND _rle4k_vcpkg_candidates "E:/vcpkg")
endif()

set(RLE4K_VCPKG_ROOT "")
set(RLE4K_VCPKG_SOURCE "")
foreach(_cand IN LISTS _rle4k_vcpkg_candidates)
    file(TO_CMAKE_PATH "${_cand}" _cand_norm)
    set(_tcm_lib "${_cand_norm}/installed/x64-windows-static/lib/libtcmalloc_minimal.lib")
    if(EXISTS "${_tcm_lib}")
        set(RLE4K_VCPKG_ROOT "${_cand_norm}")
        set(RLE4K_VCPKG_SOURCE "Fallback")
        if(DEFINED ENV{VCPKG_LOCAL_ROOT})
            file(TO_CMAKE_PATH "$ENV{VCPKG_LOCAL_ROOT}" _local)
            if(_cand_norm STREQUAL _local)
                set(RLE4K_VCPKG_SOURCE "VCPKG_LOCAL_ROOT")
            endif()
        endif()
        if(RLE4K_VCPKG_SOURCE STREQUAL "Fallback" AND EXISTS "${CMAKE_SOURCE_DIR}/../../vcpkg")
            file(TO_CMAKE_PATH "${CMAKE_SOURCE_DIR}/../../vcpkg" _proj)
            if(_cand_norm STREQUAL _proj)
                set(RLE4K_VCPKG_SOURCE "ProjectLocal")
            endif()
        endif()
        if(RLE4K_VCPKG_SOURCE STREQUAL "Fallback" AND DEFINED ENV{VCPKG_ROOT})
            file(TO_CMAKE_PATH "$ENV{VCPKG_ROOT}" _env)
            if(_cand_norm STREQUAL _env)
                set(RLE4K_VCPKG_SOURCE "VCPKG_ROOT")
            endif()
        endif()
        break()
    endif()
endforeach()

if(RLE4K_VCPKG_ROOT)
    set(RLE4K_VCPKG_INSTALLED "${RLE4K_VCPKG_ROOT}/installed/x64-windows-static")
    # Pin full path (redtt: VcpkgTcmallocLib) so a ~70KB import lib cannot shadow.
    set(TCMALLOC_LIBRARY "${RLE4K_VCPKG_INSTALLED}/lib/libtcmalloc_minimal.lib")
    message(STATUS "VcpkgRoot (${RLE4K_VCPKG_SOURCE}): ${RLE4K_VCPKG_ROOT}")
else()
    message(FATAL_ERROR
        "vcpkg static libtcmalloc_minimal.lib not found "
        "(need gperftools:x64-windows-static under VCPKG_LOCAL_ROOT / project vcpkg / "
        "VCPKG_ROOT / E:/vcpkg / D:/vcpkg). Same requirement as redtt.")
endif()

file(SIZE "${TCMALLOC_LIBRARY}" _tcmalloc_lib_size)
if(_tcmalloc_lib_size LESS 500000)
    message(FATAL_ERROR
        "libtcmalloc_minimal.lib at ${TCMALLOC_LIBRARY} looks like a DLL import lib "
        "(${_tcmalloc_lib_size} bytes). redcli/redtt require the static Override archive.")
endif()
message(STATUS "tcmalloc static lib: ${TCMALLOC_LIBRARY} (${_tcmalloc_lib_size} bytes)")

set(RLE4K_ZSTD_TARGET "")
if(TARGET zstd::libzstd_static)
    set(RLE4K_ZSTD_TARGET zstd::libzstd_static)
elseif(TARGET zstd::libzstd)
    set(RLE4K_ZSTD_TARGET zstd::libzstd)
else()
    message(FATAL_ERROR
        "zstd CMake target not found (expected zstd::libzstd_static or zstd::libzstd).\n"
        "  On Windows, configure with the vcpkg toolchain and triplet x64-windows-static.\n"
        "  See README.md and docs/DEPENDENCIES.md.")
endif()
endif() # NOT RLE4K_DEPS_VENDOR_OK
# ============================================================================
# Base static libs from deps/ (published via scripts/publish-deps.ps1)
# ============================================================================
include(${CMAKE_SOURCE_DIR}/cmake/DepsLibrle4k.cmake)
include(${CMAKE_SOURCE_DIR}/cmake/DepsLibdif.cmake)
include(${CMAKE_SOURCE_DIR}/cmake/DepsLibraster.cmake)

# ============================================================================
# rle4k_core — library sources shared by redcli and unit tests
# (DIF / raster / RLE4K / bitmap come from deps static libs)
# ============================================================================
set(RLE4K_CORE_SOURCES
    ${CMAKE_SOURCE_DIR}/redcli/rle4k_buffer_mgr.cpp
    ${CMAKE_SOURCE_DIR}/redcli/rle4k_format_encoder.cpp
)

# Alias for tests/CMakeLists.txt (${LZO_LIBRARY}) and multi-config generators.
set(LZO_LIBRARY optimized ${LZO_LIBRARY_RELEASE} debug ${LZO_LIBRARY_DEBUG})

add_library(rle4k_core STATIC ${RLE4K_CORE_SOURCES})
target_include_directories(rle4k_core PUBLIC
    ${CMAKE_SOURCE_DIR}/redcli
    ${CMAKE_SOURCE_DIR}/redcli/libcore
    ${LZO_INCLUDE_DIR}
    "${RLE4K_VCPKG_INSTALLED}/include"
    "${RLE4K_VCPKG_INSTALLED}/include/gperftools"
)
message(STATUS "Compression libs: deps/ vendor (or vcpkg fallback)")
target_link_libraries(rle4k_core PUBLIC
    librle4k::librle4k
    libdif::libdif
    libraster::libraster
    ZLIB::ZLIB
    ${LZO_LIBRARY}
    Snappy::snappy
    ${RLE4K_ZSTD_TARGET}
    unofficial::brotli::brotlienc
    unofficial::brotli::brotlicommon
)
if(MSVC)
    # PDBs help OpenCppCoverage / VS coverage map lines back to sources
    target_compile_options(rle4k_core PRIVATE /Zi)
    if(RLE4K_COVERAGE)
        target_compile_options(rle4k_core PRIVATE /Od /Ob0)
        message(STATUS "RLE4K_COVERAGE=ON — rle4k_core /Od /Ob0 (keeps /MT Release CRT)")
    endif()
endif()

# ============================================================================
# redcli.exe — open-source CLI (snake_case naming)
# ============================================================================
add_executable(redcli
    ${CMAKE_SOURCE_DIR}/redcli/main.cpp
)
# Put tcmalloc first so its malloc/free (gperftools[override]) win over CRT.
target_link_libraries(redcli PRIVATE
    "${TCMALLOC_LIBRARY}"
    rle4k_core
)
target_compile_definitions(redcli PRIVATE RLE4K_REDCLI_VERSION="${PROJECT_VERSION}")

if(MSVC)
    # gperftools[override] replaces CRT allocators in the .lib (no __tcmalloc stub).
    target_compile_definitions(redcli PRIVATE USE_TCMALLOC WIN32_OVERRIDE_ALLOCATORS)
endif()

if(WIN32)
    # gperftools Windows dependencies (see upstream CMakeLists.txt).
    target_link_libraries(redcli PRIVATE ws2_32 psapi synchronization shlwapi)
    # Same as redtt: static Override has no __tcmalloc; force TCMallocGuard ctor.
    target_compile_definitions(redcli PRIVATE
        RLE4K_TCMALLOC_INCLUDE=1
        USE_TCMALLOC
        PERFTOOLS_DLL_DECL=)
    target_link_options(redcli PRIVATE "/INCLUDE:??0TCMallocGuard@@QEAA@XZ")
endif()

# Generate redcli.json next to redcli.exe from built-in defaults (skip if present)
add_custom_command(TARGET redcli POST_BUILD
    COMMAND "$<TARGET_FILE:redcli>" init
    WORKING_DIRECTORY "$<TARGET_FILE_DIR:redcli>"
    COMMENT "redcli init -> redcli.json (built-in default)"
)

# ============================================================================
# Unit tests (Google Test) — optional via -DRLE4K_BUILD_TESTS=OFF
# ============================================================================
if(RLE4K_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# ============================================================================
# Install rules
# ============================================================================
install(TARGETS redcli RUNTIME DESTINATION bin)

# ============================================================================
# Configuration summary
# ============================================================================
message(STATUS "")
message(STATUS "=== RLE4K Reproducibility Build Configuration ===")
message(STATUS "  Project:      RLE4K-Reproducibility v${PROJECT_VERSION}")
message(STATUS "  C++ Standard: C++${CMAKE_CXX_STANDARD}")
message(STATUS "  Build Type:   ${CMAKE_BUILD_TYPE}")
message(STATUS "  ZSTD target:  ${RLE4K_ZSTD_TARGET}")
message(STATUS "  tcmalloc:     ${TCMALLOC_LIBRARY}")
message(STATUS "  Unit tests:   ${RLE4K_BUILD_TESTS}")
message(STATUS "  Coverage:     ${RLE4K_COVERAGE}")
message(STATUS "  Install:      ${CMAKE_INSTALL_PREFIX}")
message(STATUS "=============================================")
message(STATUS "")
