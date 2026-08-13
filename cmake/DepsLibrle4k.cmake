# Resolve pinned librle4k under deps/ via versions.json (or CMake/env override).
# Expected layout: ${RLE4K_DEPS_ROOT}/librle4k/<ver>/{include,lib/librle4k.lib}

set(_RLE4K_DEPS_ROOT "${CMAKE_SOURCE_DIR}/deps")
if(DEFINED RLE4K_DEPS_ROOT AND NOT "${RLE4K_DEPS_ROOT}" STREQUAL "")
    set(_RLE4K_DEPS_ROOT "${RLE4K_DEPS_ROOT}")
endif()

set(_RLE4K_VERSIONS_JSON "${_RLE4K_DEPS_ROOT}/versions.json")
set(_RLE4K_LIBRLE4K_VER "")

if(DEFINED RLE4K_LIBRLE4K_VERSION AND NOT "${RLE4K_LIBRLE4K_VERSION}" STREQUAL "")
    set(_RLE4K_LIBRLE4K_VER "${RLE4K_LIBRLE4K_VERSION}")
elseif(DEFINED ENV{RLE4K_DEPS_LIBRLE4K_VERSION} AND NOT "$ENV{RLE4K_DEPS_LIBRLE4K_VERSION}" STREQUAL "")
    set(_RLE4K_LIBRLE4K_VER "$ENV{RLE4K_DEPS_LIBRLE4K_VERSION}")
elseif(EXISTS "${_RLE4K_VERSIONS_JSON}")
    file(READ "${_RLE4K_VERSIONS_JSON}" _RLE4K_VERSIONS_RAW)
    string(REGEX MATCH "\"librle4k\"[ \t]*:[ \t]*\"([^\"]+)\"" _RLE4K_M "${_RLE4K_VERSIONS_RAW}")
    if(CMAKE_MATCH_1)
        set(_RLE4K_LIBRLE4K_VER "${CMAKE_MATCH_1}")
    endif()
endif()

if(_RLE4K_LIBRLE4K_VER STREQUAL "")
    set(_RLE4K_LIBRLE4K_VER "1.0.0")
endif()

set(RLE4K_LIBRLE4K_DIR "${_RLE4K_DEPS_ROOT}/librle4k/${_RLE4K_LIBRLE4K_VER}")
set(RLE4K_LIBRLE4K_INCLUDE_DIR "${RLE4K_LIBRLE4K_DIR}/include")
set(RLE4K_LIBRLE4K_LIB "${RLE4K_LIBRLE4K_DIR}/lib/librle4k.lib")

if(NOT EXISTS "${RLE4K_LIBRLE4K_LIB}")
    message(FATAL_ERROR
        "librle4k not found at:\n  ${RLE4K_LIBRLE4K_LIB}\n"
        "Run: .\\scripts\\publish-deps.ps1\n"
        "Or set RLE4K_LIBRLE4K_VERSION / RLE4K_DEPS_ROOT.")
endif()
if(NOT EXISTS "${RLE4K_LIBRLE4K_INCLUDE_DIR}/rle4k.h")
    message(FATAL_ERROR "librle4k headers missing under ${RLE4K_LIBRLE4K_INCLUDE_DIR}")
endif()

if(NOT TARGET librle4k::librle4k)
    add_library(librle4k::librle4k STATIC IMPORTED GLOBAL)
    set_target_properties(librle4k::librle4k PROPERTIES
        IMPORTED_LOCATION "${RLE4K_LIBRLE4K_LIB}"
        INTERFACE_INCLUDE_DIRECTORIES "${RLE4K_LIBRLE4K_INCLUDE_DIR}"
    )
endif()

message(STATUS "librle4k: ${_RLE4K_LIBRLE4K_VER} -> ${RLE4K_LIBRLE4K_DIR}")
