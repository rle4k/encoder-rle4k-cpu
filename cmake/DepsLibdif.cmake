# Resolve pinned libdif under deps/ via versions.json (or CMake/env override).
set(_RLE4K_DEPS_ROOT "${CMAKE_SOURCE_DIR}/deps")
if(DEFINED RLE4K_DEPS_ROOT AND NOT "${RLE4K_DEPS_ROOT}" STREQUAL "")
    set(_RLE4K_DEPS_ROOT "${RLE4K_DEPS_ROOT}")
endif()

set(_RLE4K_VERSIONS_JSON "${_RLE4K_DEPS_ROOT}/versions.json")
set(_RLE4K_LIBDIF_VER "")

if(DEFINED RLE4K_LIBDIF_VERSION AND NOT "${RLE4K_LIBDIF_VERSION}" STREQUAL "")
    set(_RLE4K_LIBDIF_VER "${RLE4K_LIBDIF_VERSION}")
elseif(DEFINED ENV{RLE4K_DEPS_LIBDIF_VERSION} AND NOT "$ENV{RLE4K_DEPS_LIBDIF_VERSION}" STREQUAL "")
    set(_RLE4K_LIBDIF_VER "$ENV{RLE4K_DEPS_LIBDIF_VERSION}")
elseif(EXISTS "${_RLE4K_VERSIONS_JSON}")
    file(READ "${_RLE4K_VERSIONS_JSON}" _RLE4K_VERSIONS_RAW)
    string(REGEX MATCH "\"libdif\"[ \t]*:[ \t]*\"([^\"]+)\"" _RLE4K_M "${_RLE4K_VERSIONS_RAW}")
    if(CMAKE_MATCH_1)
        set(_RLE4K_LIBDIF_VER "${CMAKE_MATCH_1}")
    endif()
endif()

if(_RLE4K_LIBDIF_VER STREQUAL "")
    set(_RLE4K_LIBDIF_VER "1.0.0")
endif()

set(RLE4K_LIBDIF_DIR "${_RLE4K_DEPS_ROOT}/libdif/${_RLE4K_LIBDIF_VER}")
set(RLE4K_LIBDIF_INCLUDE_DIR "${RLE4K_LIBDIF_DIR}/include")
set(RLE4K_LIBDIF_LIB "${RLE4K_LIBDIF_DIR}/lib/libdif.lib")

if(NOT EXISTS "${RLE4K_LIBDIF_LIB}")
    message(FATAL_ERROR
        "libdif not found at:\n  ${RLE4K_LIBDIF_LIB}\n"
        "Run: .\\scripts\\publish-deps.ps1\n"
        "Or set RLE4K_LIBDIF_VERSION / RLE4K_DEPS_ROOT.")
endif()

if(NOT TARGET libdif::libdif)
    add_library(libdif::libdif STATIC IMPORTED GLOBAL)
    set_target_properties(libdif::libdif PROPERTIES
        IMPORTED_LOCATION "${RLE4K_LIBDIF_LIB}"
        INTERFACE_INCLUDE_DIRECTORIES "${RLE4K_LIBDIF_INCLUDE_DIR}"
    )
endif()

# Publish also vendors common namespace headers under include/
message(STATUS "libdif: ${_RLE4K_LIBDIF_VER} -> ${RLE4K_LIBDIF_DIR}")