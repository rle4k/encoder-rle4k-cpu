# Resolve pinned libraster under deps/ via versions.json (or CMake/env override).
set(_RLE4K_DEPS_ROOT "${CMAKE_SOURCE_DIR}/deps")
if(DEFINED RLE4K_DEPS_ROOT AND NOT "${RLE4K_DEPS_ROOT}" STREQUAL "")
    set(_RLE4K_DEPS_ROOT "${RLE4K_DEPS_ROOT}")
endif()

set(_RLE4K_VERSIONS_JSON "${_RLE4K_DEPS_ROOT}/versions.json")
set(_RLE4K_LIBRASTER_VER "")

if(DEFINED RLE4K_LIBRASTER_VERSION AND NOT "${RLE4K_LIBRASTER_VERSION}" STREQUAL "")
    set(_RLE4K_LIBRASTER_VER "${RLE4K_LIBRASTER_VERSION}")
elseif(DEFINED ENV{RLE4K_DEPS_LIBRASTER_VERSION} AND NOT "$ENV{RLE4K_DEPS_LIBRASTER_VERSION}" STREQUAL "")
    set(_RLE4K_LIBRASTER_VER "$ENV{RLE4K_DEPS_LIBRASTER_VERSION}")
elseif(EXISTS "${_RLE4K_VERSIONS_JSON}")
    file(READ "${_RLE4K_VERSIONS_JSON}" _RLE4K_VERSIONS_RAW)
    string(REGEX MATCH "\"libraster\"[ \t]*:[ \t]*\"([^\"]+)\"" _RLE4K_M "${_RLE4K_VERSIONS_RAW}")
    if(CMAKE_MATCH_1)
        set(_RLE4K_LIBRASTER_VER "${CMAKE_MATCH_1}")
    endif()
endif()

if(_RLE4K_LIBRASTER_VER STREQUAL "")
    set(_RLE4K_LIBRASTER_VER "1.0.0")
endif()

set(RLE4K_LIBRASTER_DIR "${_RLE4K_DEPS_ROOT}/libraster/${_RLE4K_LIBRASTER_VER}")
set(RLE4K_LIBRASTER_INCLUDE_DIR "${RLE4K_LIBRASTER_DIR}/include")
set(RLE4K_LIBRASTER_LIB "${RLE4K_LIBRASTER_DIR}/lib/libraster.lib")

if(NOT EXISTS "${RLE4K_LIBRASTER_LIB}")
    message(FATAL_ERROR
        "libraster not found at:\n  ${RLE4K_LIBRASTER_LIB}\n"
        "Run: .\\scripts\\publish-deps.ps1\n"
        "Or set RLE4K_LIBRASTER_VERSION / RLE4K_DEPS_ROOT.")
endif()

if(NOT TARGET libraster::libraster)
    add_library(libraster::libraster STATIC IMPORTED GLOBAL)
    set_target_properties(libraster::libraster PROPERTIES
        IMPORTED_LOCATION "${RLE4K_LIBRASTER_LIB}"
        INTERFACE_INCLUDE_DIRECTORIES "${RLE4K_LIBRASTER_INCLUDE_DIR}"
    )
endif()

message(STATUS "libraster: ${_RLE4K_LIBRASTER_VER} -> ${RLE4K_LIBRASTER_DIR}")