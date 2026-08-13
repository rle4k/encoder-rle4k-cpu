# Prefer vendored compression/allocator libs under deps/ (no live vcpkg required).
# Falls back to find_package / vcpkg-installed only if a package pin is missing.

set(_RLE4K_DEPS_ROOT "${CMAKE_SOURCE_DIR}/deps")
if(DEFINED RLE4K_DEPS_ROOT AND NOT "${RLE4K_DEPS_ROOT}" STREQUAL "")
    set(_RLE4K_DEPS_ROOT "${RLE4K_DEPS_ROOT}")
endif()
set(_RLE4K_VERSIONS_JSON "${_RLE4K_DEPS_ROOT}/versions.json")

function(_rle4k_read_pin out_var key default_ver)
    set(_ver "${default_ver}")
    if(DEFINED ENV{RLE4K_DEPS_${key}_VERSION} AND NOT "$ENV{RLE4K_DEPS_${key}_VERSION}" STREQUAL "")
        set(_ver "$ENV{RLE4K_DEPS_${key}_VERSION}")
    elseif(EXISTS "${_RLE4K_VERSIONS_JSON}")
        file(READ "${_RLE4K_VERSIONS_JSON}" _raw)
        string(REGEX MATCH "\"${key}\"[ \t]*:[ \t]*\"([^\"]+)\"" _m "${_raw}")
        if(CMAKE_MATCH_1)
            set(_ver "${CMAKE_MATCH_1}")
        endif()
    endif()
    set(${out_var} "${_ver}" PARENT_SCOPE)
endfunction()

set(RLE4K_DEPS_VENDOR_OK TRUE)

_rle4k_read_pin(RLE4K_ZLIB_VER zlib 1.3.1)
_rle4k_read_pin(RLE4K_LZO_VER lzo 2.10)
_rle4k_read_pin(RLE4K_SNAPPY_VER snappy 1.2.2)
_rle4k_read_pin(RLE4K_ZSTD_VER zstd 1.5.7)
_rle4k_read_pin(RLE4K_BROTLI_VER brotli 1.1.0)
_rle4k_read_pin(RLE4K_GPERF_VER gperftools 2.16)

set(RLE4K_ZLIB_DIR "${_RLE4K_DEPS_ROOT}/zlib/${RLE4K_ZLIB_VER}")
set(RLE4K_LZO_DIR "${_RLE4K_DEPS_ROOT}/lzo/${RLE4K_LZO_VER}")
set(RLE4K_SNAPPY_DIR "${_RLE4K_DEPS_ROOT}/snappy/${RLE4K_SNAPPY_VER}")
set(RLE4K_ZSTD_DIR "${_RLE4K_DEPS_ROOT}/zstd/${RLE4K_ZSTD_VER}")
set(RLE4K_BROTLI_DIR "${_RLE4K_DEPS_ROOT}/brotli/${RLE4K_BROTLI_VER}")
set(RLE4K_GPERF_DIR "${_RLE4K_DEPS_ROOT}/gperftools/${RLE4K_GPERF_VER}")

foreach(_pkg_dir IN ITEMS
    "${RLE4K_ZLIB_DIR}/lib/zlib.lib"
    "${RLE4K_LZO_DIR}/lib/lzo2.lib"
    "${RLE4K_SNAPPY_DIR}/lib/snappy.lib"
    "${RLE4K_ZSTD_DIR}/lib/zstd.lib"
    "${RLE4K_BROTLI_DIR}/lib/brotlienc.lib"
    "${RLE4K_GPERF_DIR}/lib/libtcmalloc_minimal.lib"
)
    if(NOT EXISTS "${_pkg_dir}")
        set(RLE4K_DEPS_VENDOR_OK FALSE)
        message(STATUS "deps vendor incomplete: missing ${_pkg_dir}")
    endif()
endforeach()

if(RLE4K_DEPS_VENDOR_OK)
    message(STATUS "Using vendored compression deps under ${_RLE4K_DEPS_ROOT}")

    add_library(ZLIB::ZLIB STATIC IMPORTED GLOBAL)
    set_target_properties(ZLIB::ZLIB PROPERTIES
        IMPORTED_LOCATION "${RLE4K_ZLIB_DIR}/lib/zlib.lib"
        INTERFACE_INCLUDE_DIRECTORIES "${RLE4K_ZLIB_DIR}/include"
    )

    set(LZO_INCLUDE_DIR "${RLE4K_LZO_DIR}/include")
    set(LZO_LIBRARY_RELEASE "${RLE4K_LZO_DIR}/lib/lzo2.lib")
    set(LZO_LIBRARY_DEBUG "${LZO_LIBRARY_RELEASE}")
    set(LZO_LIBRARY optimized ${LZO_LIBRARY_RELEASE} debug ${LZO_LIBRARY_DEBUG})

    add_library(Snappy::snappy STATIC IMPORTED GLOBAL)
    set_target_properties(Snappy::snappy PROPERTIES
        IMPORTED_LOCATION "${RLE4K_SNAPPY_DIR}/lib/snappy.lib"
        INTERFACE_INCLUDE_DIRECTORIES "${RLE4K_SNAPPY_DIR}/include"
    )

    add_library(zstd::libzstd_static STATIC IMPORTED GLOBAL)
    set_target_properties(zstd::libzstd_static PROPERTIES
        IMPORTED_LOCATION "${RLE4K_ZSTD_DIR}/lib/zstd.lib"
        INTERFACE_INCLUDE_DIRECTORIES "${RLE4K_ZSTD_DIR}/include"
    )
    set(RLE4K_ZSTD_TARGET zstd::libzstd_static)

    add_library(unofficial::brotli::brotlicommon STATIC IMPORTED GLOBAL)
    set_target_properties(unofficial::brotli::brotlicommon PROPERTIES
        IMPORTED_LOCATION "${RLE4K_BROTLI_DIR}/lib/brotlicommon.lib"
        INTERFACE_INCLUDE_DIRECTORIES "${RLE4K_BROTLI_DIR}/include"
    )
    add_library(unofficial::brotli::brotlienc STATIC IMPORTED GLOBAL)
    set_target_properties(unofficial::brotli::brotlienc PROPERTIES
        IMPORTED_LOCATION "${RLE4K_BROTLI_DIR}/lib/brotlienc.lib"
        INTERFACE_INCLUDE_DIRECTORIES "${RLE4K_BROTLI_DIR}/include"
        INTERFACE_LINK_LIBRARIES unofficial::brotli::brotlicommon
    )
    add_library(unofficial::brotli::brotlidec STATIC IMPORTED GLOBAL)
    set_target_properties(unofficial::brotli::brotlidec PROPERTIES
        IMPORTED_LOCATION "${RLE4K_BROTLI_DIR}/lib/brotlidec.lib"
        INTERFACE_INCLUDE_DIRECTORIES "${RLE4K_BROTLI_DIR}/include"
        INTERFACE_LINK_LIBRARIES unofficial::brotli::brotlicommon
    )

    set(TCMALLOC_LIBRARY "${RLE4K_GPERF_DIR}/lib/libtcmalloc_minimal.lib")
    set(RLE4K_VCPKG_INSTALLED "${RLE4K_GPERF_DIR}")
    set(RLE4K_VCPKG_ROOT "${_RLE4K_DEPS_ROOT}")
    set(RLE4K_VCPKG_SOURCE "deps/")

    file(SIZE "${TCMALLOC_LIBRARY}" _tcmalloc_lib_size)
    if(_tcmalloc_lib_size LESS 500000)
        message(FATAL_ERROR "libtcmalloc_minimal.lib looks like an import lib (${_tcmalloc_lib_size} bytes)")
    endif()
    message(STATUS "tcmalloc static lib: ${TCMALLOC_LIBRARY} (${_tcmalloc_lib_size} bytes)")
    message(STATUS "lzo2 (release):     ${LZO_LIBRARY_RELEASE}")
else()
    message(STATUS "Vendored deps incomplete — falling back to vcpkg/find_package (transitional)")
endif()
