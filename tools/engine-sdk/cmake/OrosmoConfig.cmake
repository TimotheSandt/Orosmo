# OrosmoConfig.cmake — Optional CMake package for the Orosmo SDK release.
#
# Usage in a game project's CMakeLists.txt:
#   list(APPEND CMAKE_PREFIX_PATH "${CMAKE_CURRENT_SOURCE_DIR}/orosmo")
#   find_package(Orosmo REQUIRED)
#   add_executable(mygame src/main.cpp)
#   orosmo_add_cui_views(mygame src/HudView.cui)
#   target_link_libraries(mygame PRIVATE Orosmo::Orosmo)

cmake_minimum_required(VERSION 3.20)

get_filename_component(_OROSMO_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)

# ── Platform / vcpkg triplet detection ───────────────────────────────────────
if(NOT DEFINED OROSMO_VCPKG_TRIPLET OR OROSMO_VCPKG_TRIPLET STREQUAL "")
    if(WIN32)
        set(OROSMO_VCPKG_TRIPLET "x64-mingw-static")
    elseif(APPLE)
        if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(arm64|aarch64)$")
            set(OROSMO_VCPKG_TRIPLET "arm64-osx")
        else()
            set(OROSMO_VCPKG_TRIPLET "x64-osx")
        endif()
    else()
        if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(aarch64|arm64)$")
            set(OROSMO_VCPKG_TRIPLET "arm64-linux")
        else()
            set(OROSMO_VCPKG_TRIPLET "x64-linux")
        endif()
    endif()
endif()

# Third-party libraries, as in tools/engine-sdk/engine.mk: OROSMO_VCPKG_ROOT given; deps/ of a release package (prebuilt,
# static); vcpkg_installed/<triplet> (engine repository, packages older than deps/).
set(_OROSMO_DEPS_PACKAGED FALSE)
if(DEFINED OROSMO_VCPKG_ROOT AND NOT OROSMO_VCPKG_ROOT STREQUAL "")
    set(_OROSMO_VCPKG_DIR "${OROSMO_VCPKG_ROOT}/${OROSMO_VCPKG_TRIPLET}")
elseif(EXISTS "${_OROSMO_ROOT}/deps/lib")
    set(_OROSMO_VCPKG_DIR "${_OROSMO_ROOT}/deps")
    set(_OROSMO_DEPS_PACKAGED TRUE)
else()
    set(_OROSMO_VCPKG_DIR "${_OROSMO_ROOT}/vcpkg_installed/${OROSMO_VCPKG_TRIPLET}")
endif()

if(EXISTS "${_OROSMO_ROOT}/includes")
    set(_OROSMO_INC_BASE "${_OROSMO_ROOT}/includes")
else()
    set(_OROSMO_INC_BASE "${_OROSMO_ROOT}/include")
endif()

set(OROSMO_ROOT_DIR "${_OROSMO_ROOT}")
set(OROSMO_SHADERS_DIR "${_OROSMO_ROOT}/shaders")
set(OROSMO_RES_DIR "${_OROSMO_ROOT}/res")
# CUI output: in the build tree of the project (orosmo_add_cui_views), never in the engine directory (read-only, shared).
if(NOT DEFINED OROSMO_GEN_DIR OR OROSMO_GEN_DIR STREQUAL "")
    set(OROSMO_GEN_DIR "${CMAKE_BINARY_DIR}/orosmo_generated")
endif()

if(WIN32)
    set(OROSMO_CUI_EXECUTABLE "${_OROSMO_ROOT}/tools/cui.exe")
else()
    set(OROSMO_CUI_EXECUTABLE "${_OROSMO_ROOT}/tools/cui")
endif()

# ── Imported subsystem static libraries ──────────────────────────────────────
set(_OROSMO_COMPONENTS app ui core platform rhi renderer assets anim scene sim thirdparty)
set(_OROSMO_IMPORTED_TARGETS "")

foreach(_comp IN LISTS _OROSMO_COMPONENTS)
    set(_tgt "Orosmo::${_comp}")
    if(NOT TARGET "${_tgt}")
        add_library("${_tgt}" STATIC IMPORTED)
        set(_dbg "${_OROSMO_ROOT}/lib/debug/lib${_comp}.a")
        set(_rel "${_OROSMO_ROOT}/lib/release/lib${_comp}.a")
        if(EXISTS "${_rel}")
            set_target_properties("${_tgt}" PROPERTIES
                IMPORTED_LOCATION "${_rel}"
                IMPORTED_LOCATION_RELEASE "${_rel}"
                IMPORTED_LOCATION_RELWITHDEBINFO "${_rel}"
                IMPORTED_LOCATION_MINSIZEREL "${_rel}"
            )
        endif()
        if(EXISTS "${_dbg}")
            set_target_properties("${_tgt}" PROPERTIES
                IMPORTED_LOCATION_DEBUG "${_dbg}"
            )
            if(NOT EXISTS "${_rel}")
                set_target_properties("${_tgt}" PROPERTIES IMPORTED_LOCATION "${_dbg}")
            endif()
        endif()
    endif()
    list(APPEND _OROSMO_IMPORTED_TARGETS "${_tgt}")
endforeach()

# Assets (M6 lot G) : KTX2 (ktx, qui dépend d'astcenc et de zstd) et meshoptimizer, comme tools/engine-sdk/engine.mk. Le nom
# de l'archive astcenc dépend du jeu d'instructions choisi par vcpkg : on prend celle qui est installée.
file(GLOB _OROSMO_ASTCENC "${_OROSMO_VCPKG_DIR}/lib/libastcenc-*-static.a")
set(_OROSMO_ASSET_LIBS ktx)
if(_OROSMO_ASTCENC)
    list(GET _OROSMO_ASTCENC 0 _OROSMO_ASTCENC)
    list(APPEND _OROSMO_ASSET_LIBS "${_OROSMO_ASTCENC}")
endif()
list(APPEND _OROSMO_ASSET_LIBS zstd meshoptimizer)

# ── Platform third-party libraries (matches tools/engine-sdk/engine.mk) ──────
if(WIN32)
    set(_OROSMO_PLATFORM_LIBS
        volk glfw3 glad msdfgen-core freetype
        png16 zs bz2 brotlidec brotlienc brotlicommon
        shaderc shaderc_util glslang
        MachineIndependent GenericCodeGen OSDependent
        spirv-cross-reflect spirv-cross-glsl spirv-cross-core
        SPIRV SPIRV-Tools-opt SPIRV-Tools
        psapi winmm gdi32 user32 shell32 opengl32 stdc++exp
    )
elseif(APPLE)
    set(_OROSMO_PLATFORM_LIBS
        volk glfw3 glad msdfgen-core freetype png16 z bz2
        brotlidec brotlicommon
        shaderc shaderc_util glslang
        spirv-cross-reflect spirv-cross-glsl spirv-cross-core
        SPIRV-Tools-opt SPIRV-Tools SPIRV
        "-framework OpenGL" "-framework Cocoa" "-framework IOKit" "-framework CoreVideo"
        "-framework QuartzCore" "-framework Metal" "-framework CoreFoundation"
    )
else()
    if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS "14.0")
        set(_OROSMO_STACKTRACE_LIB stdc++_libbacktrace)
    else()
        set(_OROSMO_STACKTRACE_LIB stdc++exp)
    endif()
    set(_OROSMO_PLATFORM_LIBS
        volk glfw3 glad GL msdfgen-core ${_OROSMO_STACKTRACE_LIB}
        freetype png16 z bz2 brotlidec brotlicommon
        shaderc shaderc_util glslang
        spirv-cross-reflect spirv-cross-glsl spirv-cross-core
        SPIRV-Tools-opt SPIRV-Tools SPIRV
        pthread X11 dl m
    )
endif()

# ── Umbrella target: Orosmo::Orosmo ──────────────────────────────────────────
if(NOT TARGET Orosmo::Orosmo)
    add_library(Orosmo::Orosmo INTERFACE IMPORTED)

    target_compile_features(Orosmo::Orosmo INTERFACE cxx_std_23)
    target_compile_definitions(Orosmo::Orosmo INTERFACE
        GLM_ENABLE_EXPERIMENTAL
        $<$<PLATFORM_ID:Windows>:NOMINMAX>
        $<$<PLATFORM_ID:Windows>:WIN32_LEAN_AND_MEAN>
    )
    target_compile_options(Orosmo::Orosmo INTERFACE
        $<$<OR:$<CXX_COMPILER_ID:GNU>,$<CXX_COMPILER_ID:Clang>,$<CXX_COMPILER_ID:AppleClang>>:-ffp-contract=off>
    )

    target_include_directories(Orosmo::Orosmo INTERFACE
        "${_OROSMO_INC_BASE}"
        "${_OROSMO_INC_BASE}/orosmo"
    )
    target_include_directories(Orosmo::Orosmo SYSTEM INTERFACE
        "${_OROSMO_VCPKG_DIR}/include"
    )

    target_link_directories(Orosmo::Orosmo INTERFACE
        "${_OROSMO_VCPKG_DIR}/lib"
    )

    if(APPLE)
        target_link_libraries(Orosmo::Orosmo INTERFACE
            ${_OROSMO_IMPORTED_TARGETS}
            ${_OROSMO_ASSET_LIBS} ${_OROSMO_PLATFORM_LIBS}
        )
        # volk and GLFW load the Vulkan loader at run time: rpaths to the system loader (as engine.mk, ENGINE_VULKAN_RPATHS),
        # then, without deps/ only, to vcpkg's vulkan-loader dylib (the Vulkan loader is never shipped in deps/).
        if(NOT DEFINED OROSMO_VULKAN_RPATHS)
            set(OROSMO_VULKAN_RPATHS /opt/homebrew/lib /usr/local/lib)
        endif()
        foreach(_orosmo_rpath IN LISTS OROSMO_VULKAN_RPATHS)
            target_link_options(Orosmo::Orosmo INTERFACE "-Wl,-rpath,${_orosmo_rpath}")
        endforeach()
        if(NOT _OROSMO_DEPS_PACKAGED)
            target_link_options(Orosmo::Orosmo INTERFACE
                "-Wl,-rpath,${_OROSMO_VCPKG_DIR}/lib"
            )
        endif()
    else()
        target_link_libraries(Orosmo::Orosmo INTERFACE
            -Wl,--start-group
            ${_OROSMO_IMPORTED_TARGETS}
            -Wl,--end-group
            ${_OROSMO_ASSET_LIBS} ${_OROSMO_PLATFORM_LIBS}
        )
    endif()
endif()

# ── Helper: compile .cui views at build time ─────────────────────────────────
function(orosmo_add_cui_views TARGET_NAME)
    if(NOT EXISTS "${OROSMO_CUI_EXECUTABLE}")
        message(FATAL_ERROR "Orosmo CUI compiler not found at ${OROSMO_CUI_EXECUTABLE}")
    endif()
    set(_out_base "${CMAKE_CURRENT_BINARY_DIR}/orosmo_generated")
    target_include_directories("${TARGET_NAME}" PRIVATE "${_out_base}")
    foreach(_cui_in IN LISTS ARGN)
        get_filename_component(_cui_abs "${_cui_in}" ABSOLUTE)
        get_filename_component(_stem "${_cui_abs}" NAME_WE)
        get_filename_component(_name "${_cui_abs}" NAME)
        set(_gen_cpp "${_out_base}/${_stem}.gen.cpp")
        set(_gen_h   "${_out_base}/${_stem}.gen.h")
        set(_gen_cui "${_out_base}/${_name}")
        add_custom_command(
            OUTPUT "${_gen_cpp}" "${_gen_h}" "${_gen_cui}"
            COMMAND "${CMAKE_COMMAND}" -E make_directory "${_out_base}"
            COMMAND "${OROSMO_CUI_EXECUTABLE}" run --output "${_out_base}" "${_cui_abs}"
            DEPENDS "${_cui_abs}" "${OROSMO_CUI_EXECUTABLE}"
            COMMENT "CUI ${_name}"
            VERBATIM
        )
        target_sources("${TARGET_NAME}" PRIVATE "${_gen_cpp}" "${_gen_h}" "${_gen_cui}")
    endforeach()
endfunction()

set(Orosmo_FOUND TRUE)
