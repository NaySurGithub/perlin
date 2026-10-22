cmake_minimum_required(VERSION 3.28)

if(APPLE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "12.0" CACHE STRING "" FORCE)
endif()

option(PERLIN_USE_CLANG_CL "Build with clang-cl on Windows" ON)
if(CMAKE_HOST_WIN32 AND PERLIN_USE_CLANG_CL AND NOT CMAKE_GENERATOR MATCHES "Visual Studio"
   AND NOT CMAKE_C_COMPILER AND NOT CMAKE_CXX_COMPILER AND NOT DEFINED ENV{CC} AND NOT DEFINED ENV{CXX})
    set(_perlin_llvm_hints "$ENV{VCINSTALLDIR}/Tools/Llvm/x64/bin" "$ENV{ProgramFiles}/LLVM/bin" "C:/Program Files/LLVM/bin")
    find_program(PERLIN_CLANG_CL clang-cl HINTS ${_perlin_llvm_hints})
    find_program(PERLIN_LLD_LINK lld-link HINTS ${_perlin_llvm_hints})
    if(PERLIN_CLANG_CL)
        set(CMAKE_C_COMPILER "${PERLIN_CLANG_CL}" CACHE FILEPATH "C compiler" FORCE)
        set(CMAKE_CXX_COMPILER "${PERLIN_CLANG_CL}" CACHE FILEPATH "C++ compiler" FORCE)
        if(PERLIN_LLD_LINK)
            set(CMAKE_LINKER "${PERLIN_LLD_LINK}" CACHE FILEPATH "Linker" FORCE) # needed for LTO
        endif()
        message(STATUS "Perlin: using clang-cl (${PERLIN_CLANG_CL})")
    else()
        message(WARNING "Perlin: clang-cl not found, falling back to cl.exe. Install \"C++ Clang tools for Windows\" "
                        "in the Visual Studio Installer, or LLVM, to build with clang-cl.")
    endif()
endif()

project(Perlin VERSION 1.0.0 LANGUAGES C CXX)

if(WIN32 AND CMAKE_CXX_COMPILER_ID STREQUAL "Clang" AND CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
    set(PERLIN_CLANG_CL_BUILD ON)
    message(STATUS "Perlin: compiler is clang-cl ${CMAKE_CXX_COMPILER_VERSION}")
else()
    set(PERLIN_CLANG_CL_BUILD OFF)
endif()

set(CMAKE_POLICY_VERSION_MINIMUM 3.10)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

if(WIN32)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

add_compile_definitions(GLM_FORCE_DEPTH_ZERO_TO_ONE)

option(PERLIN_WEB_SHADERS_ONLY "Only compile the ESSL shaders used by the web build" OFF)
set(PERLIN_WEB_SHADER_DIR "" CACHE PATH "Directory holding prebuilt essl shader headers for the web build")
if(PERLIN_WEB_SHADERS_ONLY)
    set(PERLIN_BUILD_APP OFF)
else()
    set(PERLIN_BUILD_APP ON)
endif()

if(EMSCRIPTEN OR ANDROID)
    set(PERLIN_PREBUILT_SHADERS ON)
else()
    set(PERLIN_PREBUILT_SHADERS OFF)
endif()

if(EMSCRIPTEN)
    add_compile_options(-pthread -fwasm-exceptions)
    string(REPLACE "." ";" PERLIN_EMSCRIPTEN_VERSION_PARTS "${EMSCRIPTEN_VERSION}")
    list(GET PERLIN_EMSCRIPTEN_VERSION_PARTS 0 PERLIN_EM_MAJOR)
    list(GET PERLIN_EMSCRIPTEN_VERSION_PARTS 1 PERLIN_EM_MINOR)
    list(GET PERLIN_EMSCRIPTEN_VERSION_PARTS 2 PERLIN_EM_TINY)
    string(REGEX REPLACE "[^0-9].*" "" PERLIN_EM_TINY "${PERLIN_EM_TINY}")
    add_compile_definitions(
        __EMSCRIPTEN_MAJOR__=${PERLIN_EM_MAJOR}
        __EMSCRIPTEN_MINOR__=${PERLIN_EM_MINOR}
        __EMSCRIPTEN_TINY__=${PERLIN_EM_TINY})
    add_link_options(-pthread -fwasm-exceptions)
endif()

set(PERLIN_ANDROID_OPENSSL_BASE "" CACHE PATH "Directory holding one OpenSSL install per Android ABI")
if(ANDROID AND PERLIN_ANDROID_OPENSSL_BASE AND NOT OPENSSL_ROOT_DIR)
    set(OPENSSL_ROOT_DIR "${PERLIN_ANDROID_OPENSSL_BASE}/${ANDROID_ABI}" CACHE PATH "OpenSSL root" FORCE)
endif()

if(WIN32 AND NOT OPENSSL_ROOT_DIR)
    foreach(root "$ENV{VCPKG_INSTALLATION_ROOT}" "$ENV{VCPKG_ROOT}" "C:/Users/judah/vcpkg")
        if(root AND EXISTS "${root}/installed/x64-windows-static/include/openssl")
            set(OPENSSL_ROOT_DIR "${root}/installed/x64-windows-static" CACHE PATH "OpenSSL root")
            break()
        endif()
    endforeach()
endif()

if(APPLE AND NOT OPENSSL_ROOT_DIR)
    foreach(candidate /opt/homebrew/opt/openssl@3 /usr/local/opt/openssl@3)
        if(EXISTS "${candidate}")
            set(OPENSSL_ROOT_DIR "${candidate}" CACHE PATH "OpenSSL root")
            break()
        endif()
    endforeach()
endif()

if(OPENSSL_ROOT_DIR)
    file(TO_CMAKE_PATH "${OPENSSL_ROOT_DIR}" OPENSSL_ROOT_DIR)
endif()

set(OPENSSL_USE_STATIC_LIBS ON)
if(WIN32)
    set(OPENSSL_MSVC_STATIC_RT ON)
endif()
if((EMSCRIPTEN OR ANDROID) AND OPENSSL_ROOT_DIR)
    list(APPEND CMAKE_FIND_ROOT_PATH "${OPENSSL_ROOT_DIR}")
endif()
if(PERLIN_BUILD_APP)
    find_package(OpenSSL REQUIRED)
    find_package(Threads REQUIRED)
endif()
find_package(Python3 REQUIRED COMPONENTS Interpreter)

include(cmake/CPM.cmake)

if(PERLIN_BUILD_APP)
CPMAddPackage("gh:stephenberry/glaze@7.1.1")
CPMAddPackage("gh:cameron314/concurrentqueue@1.0.4")
CPMAddPackage("gh:g-truc/glm#1.0.1")
CPMAddPackage(NAME stb GITHUB_REPOSITORY nothings/stb GIT_TAG master DOWNLOAD_ONLY YES)
if(EMSCRIPTEN)
CPMAddPackage(NAME wasm3 GITHUB_REPOSITORY wasm3/wasm3 GIT_TAG 28ecb9af6d2040e474a70f7cb7f43666740141fb DOWNLOAD_ONLY YES)
set(WASM3_CORE_SOURCES m3_bind.c m3_code.c m3_compile.c m3_core.c m3_env.c m3_exec.c m3_function.c m3_info.c m3_module.c m3_parse.c m3_validate.c)
list(TRANSFORM WASM3_CORE_SOURCES PREPEND "${wasm3_SOURCE_DIR}/source/")
add_library(wasm3_core STATIC ${WASM3_CORE_SOURCES})
target_include_directories(wasm3_core PUBLIC "${wasm3_SOURCE_DIR}/source")
target_compile_definitions(wasm3_core PRIVATE d_m3HasTracer=0)
if(NOT MSVC)
    target_compile_options(wasm3_core PRIVATE -w -mtail-call)
endif()
set_target_properties(wasm3_core PROPERTIES POSITION_INDEPENDENT_CODE ON C_STANDARD 11)
set(PERLIN_WASM_ENGINE wasm3_core)
else()
CPMAddPackage(NAME wasmedge GITHUB_REPOSITORY WasmEdge/WasmEdge GIT_TAG 0.18.0 DOWNLOAD_ONLY YES)
set(WASMEDGE_BUILD_SHARED_LIB OFF CACHE BOOL "" FORCE)
set(WASMEDGE_BUILD_STATIC_LIB OFF CACHE BOOL "" FORCE)
set(WASMEDGE_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
set(WASMEDGE_BUILD_PLUGINS OFF CACHE BOOL "" FORCE)
set(WASMEDGE_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(WASMEDGE_BUILD_EXAMPLE OFF CACHE BOOL "" FORCE)
set(WASMEDGE_USE_LLVM OFF CACHE BOOL "" FORCE)
if(ANDROID)
    set(WASMEDGE_FORCE_DISABLE_LTO ON CACHE BOOL "" FORCE)
else()
    set(WASMEDGE_FORCE_DISABLE_LTO OFF CACHE BOOL "" FORCE)
endif()
set(WASMEDGE_PLUGIN_WASI_LOGGING OFF CACHE BOOL "" FORCE)
get_property(perlin_job_pools GLOBAL PROPERTY JOB_POOLS)
if(NOT perlin_job_pools MATCHES "(^|;)link=")
    set_property(GLOBAL APPEND PROPERTY JOB_POOLS link=2)
endif()
add_subdirectory("${wasmedge_SOURCE_DIR}" "${CMAKE_BINARY_DIR}/_deps/wasmedge-build" EXCLUDE_FROM_ALL)
function(perlin_quiet_targets dir)
    get_property(targets DIRECTORY "${dir}" PROPERTY BUILDSYSTEM_TARGETS)
    foreach(t IN LISTS targets)
        get_target_property(type ${t} TYPE)
        if(NOT type STREQUAL "INTERFACE_LIBRARY" AND NOT type STREQUAL "UTILITY")
            if(WIN32)
                set_property(TARGET ${t} PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
            endif()
            if(MSVC)
                target_compile_options(${t} PRIVATE /WX- /W0)
            else()
                target_compile_options(${t} PRIVATE -Wno-error -w)
            endif()
        endif()
    endforeach()
    get_property(subdirs DIRECTORY "${dir}" PROPERTY SUBDIRECTORIES)
    foreach(d IN LISTS subdirs)
        perlin_quiet_targets("${d}")
    endforeach()
endfunction()
perlin_quiet_targets("${wasmedge_SOURCE_DIR}")
add_library(perlin_wasmedge STATIC
    "${wasmedge_SOURCE_DIR}/lib/api/wasmedge.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/client/mods/WasmEdgeStop.cpp")
target_compile_definitions(perlin_wasmedge PUBLIC WASMEDGE_COMPILE_LIBRARY)
target_include_directories(perlin_wasmedge PUBLIC
    "${CMAKE_BINARY_DIR}/_deps/wasmedge-build/include/api"
    "${wasmedge_SOURCE_DIR}/include/api")
target_link_libraries(perlin_wasmedge PUBLIC wasmedgeVM wasmedgeDriver)
set_target_properties(perlin_wasmedge PROPERTIES CXX_STANDARD 17 CXX_EXTENSIONS OFF POSITION_INDEPENDENT_CODE ON)
if(MSVC)
    target_compile_options(perlin_wasmedge PRIVATE /utf-8 /bigobj /W0)
else()
    target_compile_options(perlin_wasmedge PRIVATE -w)
endif()
set(PERLIN_WASM_ENGINE perlin_wasmedge)
endif()
if(NOT EMSCRIPTEN)
CPMAddPackage(NAME httplib GITHUB_REPOSITORY yhirose/cpp-httplib GIT_TAG v0.60.1 DOWNLOAD_ONLY YES)
endif()
endif()

if(PERLIN_BUILD_APP AND NOT EMSCRIPTEN)
set(DATACHANNEL_TESTS OFF CACHE BOOL "" FORCE)
set(DBUILD_TESTS OFF CACHE BOOL "" FORCE)
set(NO_EXAMPLES ON CACHE BOOL "" FORCE)
set(NO_TESTS ON CACHE BOOL "" FORCE)
set(LIBSRTP_TEST_APPS OFF CACHE BOOL "" FORCE)
set(ENABLE_WARNINGS_AS_ERRORS OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS ON CACHE BOOL "" FORCE)
CPMAddPackage(
    NAME libdatachannel
    GITHUB_REPOSITORY paullouisageneau/libdatachannel
    GIT_TAG v0.23.2
    EXCLUDE_FROM_ALL YES
    OPTIONS "USE_GNUTLS OFF" "USE_NICE OFF" "NO_TESTS ON" "NO_EXAMPLES ON"
)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
endif()

if(PERLIN_BUILD_APP)
CPMAddPackage(NAME miniz GITHUB_REPOSITORY richgel999/miniz GIT_TAG 3.0.2)

if(EMSCRIPTEN)
    set(SDL_PTHREADS ON CACHE BOOL "" FORCE)
endif()
if(ANDROID)
    set(SDL_SHARED ON CACHE BOOL "" FORCE)
    set(SDL_STATIC OFF CACHE BOOL "" FORCE)
else()
    set(SDL_SHARED OFF CACHE BOOL "" FORCE)
    set(SDL_STATIC ON CACHE BOOL "" FORCE)
endif()
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
set(SDL_TESTS OFF CACHE BOOL "" FORCE)
set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SDL_INSTALL OFF CACHE BOOL "" FORCE)
CPMAddPackage(NAME SDL3 GITHUB_REPOSITORY libsdl-org/SDL GIT_TAG release-3.4.18)

set(RMLUI_FONT_ENGINE none CACHE STRING "" FORCE)
set(RMLUI_SAMPLES OFF CACHE BOOL "" FORCE)
set(RMLUI_TESTS OFF CACHE BOOL "" FORCE)
set(RMLUI_PRECOMPILED_HEADERS OFF CACHE BOOL "" FORCE)
set(RMLUI_COMPILER_OPTIONS OFF CACHE BOOL "" FORCE)
set(RMLUI_THIRDPARTY_CONTAINERS OFF CACHE BOOL "" FORCE)
CPMAddPackage(
    NAME RmlUi
    GITHUB_REPOSITORY mikke89/RmlUi
    GIT_TAG 6.1
    GIT_SHALLOW YES
)
endif()

set(BGFX_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
if(PERLIN_PREBUILT_SHADERS)
    set(BGFX_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
    set(BGFX_BUILD_TOOLS_SHADER OFF CACHE BOOL "" FORCE)
else()
    set(BGFX_BUILD_TOOLS ON CACHE BOOL "" FORCE)
    set(BGFX_BUILD_TOOLS_SHADER ON CACHE BOOL "" FORCE)
endif()
set(BGFX_BUILD_TOOLS_TEXTURE OFF CACHE BOOL "" FORCE)
set(BGFX_BUILD_TOOLS_GEOMETRY OFF CACHE BOOL "" FORCE)
set(BGFX_BUILD_TOOLS_BIN2C OFF CACHE BOOL "" FORCE)
set(BGFX_INSTALL OFF CACHE BOOL "" FORCE)
set(BGFX_CUSTOM_TARGETS OFF CACHE BOOL "" FORCE)
CPMAddPackage(
    NAME bgfx
    GITHUB_REPOSITORY bkaradzic/bgfx.cmake
    GIT_TAG v1.166.9546-589
    GIT_SUBMODULES "bgfx" "bimg" "bx"
    GIT_SHALLOW YES
)
if(TARGET bgfx)
    # Java shader packs bind more than 16 textures in one pass and carry large uniform sets.
    target_compile_definitions(bgfx PRIVATE BGFX_CONFIG_MAX_VERTEX_BUFFERS=16384 BGFX_CONFIG_MAX_INDEX_BUFFERS=16384
                               BGFX_CONFIG_MAX_TEXTURE_SAMPLERS=32 BGFX_CONFIG_MAX_UNIFORMS=1024)
endif()

# Direct3D 11 has 16 sampler slots; bgfx would pass the larger configured count to PSSetSamplers.
if(TARGET bgfx AND WIN32)
    set(PERLIN_BGFX_D3D11 "${bgfx_SOURCE_DIR}/bgfx/src/renderer_d3d11.cpp")
    file(READ "${PERLIN_BGFX_D3D11}" perlin_d3d11_src)
    if(NOT perlin_d3d11_src MATCHES "perlin sampler clamp")
        string(REPLACE "Samplers(0, maxTextureSamplers," "Samplers(0, bx::min<uint32_t>(maxTextureSamplers, D3D11_COMMONSHADER_SAMPLER_SLOT_COUNT)," perlin_d3d11_src "${perlin_d3d11_src}")
        string(APPEND perlin_d3d11_src "\n// perlin sampler clamp\n")
        file(WRITE "${PERLIN_BGFX_D3D11}" "${perlin_d3d11_src}")
    endif()
endif()

# Java (OptiFine / Iris) shader packs: GLSL -> SPIR-V (glslang) -> GLSL / HLSL (SPIRV-Cross).
set(ENABLE_OPT OFF CACHE BOOL "" FORCE)
set(ENABLE_HLSL OFF CACHE BOOL "" FORCE)
set(ENABLE_GLSLANG_BINARIES OFF CACHE BOOL "" FORCE)
set(ENABLE_SPVREMAPPER OFF CACHE BOOL "" FORCE)
set(GLSLANG_TESTS OFF CACHE BOOL "" FORCE)
set(GLSLANG_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
set(BUILD_EXTERNAL OFF CACHE BOOL "" FORCE)
CPMAddPackage(NAME glslang GITHUB_REPOSITORY KhronosGroup/glslang GIT_TAG e2bb10631198bc9d73daf752d9170608d5e2c8bd GIT_SHALLOW NO)
set(SPIRV_CROSS_CLI OFF CACHE BOOL "" FORCE)
set(SPIRV_CROSS_ENABLE_TESTS OFF CACHE BOOL "" FORCE)
set(SPIRV_CROSS_ENABLE_MSL OFF CACHE BOOL "" FORCE)
set(SPIRV_CROSS_ENABLE_CPP OFF CACHE BOOL "" FORCE)
set(SPIRV_CROSS_ENABLE_REFLECT OFF CACHE BOOL "" FORCE)
set(SPIRV_CROSS_ENABLE_C_API OFF CACHE BOOL "" FORCE)
set(SPIRV_CROSS_ENABLE_UTIL OFF CACHE BOOL "" FORCE)
set(SPIRV_CROSS_SKIP_INSTALL ON CACHE BOOL "" FORCE)
CPMAddPackage(NAME SPIRV-Cross GITHUB_REPOSITORY KhronosGroup/SPIRV-Cross GIT_TAG 300681bbcb8365a8d1eacdacdbdd0249bbf1d6b1 GIT_SHALLOW NO)

if(ANDROID AND TARGET bgfx)
    set(PERLIN_BGFX_EGL "${bgfx_SOURCE_DIR}/bgfx/src/glcontext_egl.cpp")
    file(READ "${PERLIN_BGFX_EGL}" perlin_egl_src)
    if(NOT perlin_egl_src MATCHES "perlin_bgfx_android_window")
        string(REPLACE "namespace bgfx { namespace gl" "extern \"C\" { void* perlin_bgfx_android_window = NULL; }\n\nnamespace bgfx { namespace gl" perlin_egl_src "${perlin_egl_src}")
        string(REPLACE "EGLNativeWindowType nwh = (EGLNativeWindowType )m_nwh;\n\t\t\teglMakeCurrent(m_display, EGL_NO_SURFACE"
                       "EGLNativeWindowType nwh = (EGLNativeWindowType )(NULL != perlin_bgfx_android_window ? perlin_bgfx_android_window : m_nwh);\n\t\t\teglMakeCurrent(m_display, EGL_NO_SURFACE"
                       perlin_egl_src "${perlin_egl_src}")
        string(REPLACE "\t\t\t\t  (ANativeWindow*)m_nwh\n\t\t\t\t, _swapChain.width"
                       "\t\t\t\t  (ANativeWindow*)nwh\n\t\t\t\t, _swapChain.width"
                       perlin_egl_src "${perlin_egl_src}")
        if(NOT perlin_egl_src MATCHES "perlin_bgfx_android_window \\? perlin_bgfx_android_window")
            message(FATAL_ERROR "Perlin: could not patch bgfx's glcontext_egl.cpp for Android resume (bgfx changed?)")
        endif()
        file(WRITE "${PERLIN_BGFX_EGL}" "${perlin_egl_src}")
    endif()
endif()

if(EMSCRIPTEN AND TARGET bgfx)
    set(PERLIN_BGFX_HTML5 "${bgfx_SOURCE_DIR}/bgfx/src/glcontext_html5.cpp")
    file(READ "${PERLIN_BGFX_HTML5}" perlin_html5_src)
    if(NOT perlin_html5_src MATCHES "perlin opaque canvas")
        string(REGEX REPLACE "s_attrs\\.alpha[ \t]*=[ \t]*0 != colorBlockInfo\\.aBits;"
               "s_attrs.alpha = false; /* perlin opaque canvas */" perlin_html5_src "${perlin_html5_src}")
        string(REGEX REPLACE "s_attrs\\.antialias[ \t]*=[ \t]*false;"
               "s_attrs.antialias = false; s_attrs.powerPreference = EM_WEBGL_POWER_PREFERENCE_HIGH_PERFORMANCE;"
               perlin_html5_src "${perlin_html5_src}")
        if(NOT perlin_html5_src MATCHES "perlin opaque canvas")
            message(FATAL_ERROR "Perlin: could not patch bgfx's glcontext_html5.cpp (bgfx changed?)")
        endif()
        file(WRITE "${PERLIN_BGFX_HTML5}" "${perlin_html5_src}")
    endif()
endif()

if(WIN32 AND TARGET shaderc)
    if(MSVC)
        target_link_options(shaderc PRIVATE "/STACK:67108864")
    else()
        target_link_options(shaderc PRIVATE "-Wl,--stack,67108864")
    endif()
endif()

set(PERLIN_SHADER_DIR "${CMAKE_CURRENT_SOURCE_DIR}/src/shaders")
set(PERLIN_SHADER_OUT "${CMAKE_CURRENT_BINARY_DIR}/shaders")
file(MAKE_DIRECTORY "${PERLIN_SHADER_OUT}")

if(PERLIN_PREBUILT_SHADERS OR PERLIN_WEB_SHADERS_ONLY)
    set(PERLIN_SHADER_PROFILES 300_es)
    set(PERLIN_EMBED essl)
elseif(WIN32)
    set(PERLIN_SHADER_PROFILES spirv 430 s_5_0)
    set(PERLIN_EMBED dxbc glsl spv)
elseif(APPLE)
    set(PERLIN_SHADER_PROFILES metal 430)
    set(PERLIN_EMBED mtl glsl)
else()
    set(PERLIN_SHADER_PROFILES spirv 430)
    set(PERLIN_EMBED spv glsl)
endif()
set(PERLIN_SHADER_DEFS)
foreach(k DXBC DXIL ESSL GLSL SPIRV METAL WGSL PSSL NVN)
    set(on 0)
    string(TOLOWER "${k}" lk)
    if(lk STREQUAL "spirv")
        set(lk spv)
    elseif(lk STREQUAL "metal")
        set(lk mtl)
    endif()
    if(lk IN_LIST PERLIN_EMBED)
        set(on 1)
    endif()
    list(APPEND PERLIN_SHADER_DEFS "BGFX_PLATFORM_SUPPORTS_${k}=${on}")
endforeach()

set(PERLIN_SHADER_INCLUDES "${bgfx_SOURCE_DIR}/bgfx/src" "${PERLIN_SHADER_DIR}" "${PERLIN_SHADER_DIR}/vv")
set(PERLIN_SHADER_OUTPUTS)
set(PERLIN_SHADER_NAMES
    vs_terrain fs_terrain_solid fs_terrain_cutout fs_terrain_translucent vs_sky fs_sky vs_star fs_star
    vs_sunrise fs_sunrise vs_celestial fs_celestial vs_cloud fs_cloud vs_screen fs_screen vs_ui fs_ui vs_post fs_post
    vs_entity_shadow fs_entity_shadow fs_bloom fs_upscale fs_taa fs_framegen
    vv_vs_terrain vv_fs_terrain_solid vv_fs_terrain_cutout vv_fs_terrain_translucent vv_vs_sky vv_fs_sky vv_vs_star vv_fs_star
    vv_vs_sunrise vv_fs_sunrise vv_vs_celestial vv_fs_celestial vv_vs_cloud vv_fs_cloud vv_vs_shadow vv_fs_shadow vv_fs_shadow_cutout vv_fs_shadow_glass)

function(perlin_compile_shaders type varying)
    set(sources)
    foreach(name IN LISTS ARGN)
        list(APPEND sources "${PERLIN_SHADER_DIR}/${name}.sc")
    endforeach()
    bgfx_compile_shaders(
        TYPE ${type}
        SHADERS ${sources}
        VARYING_DEF "${PERLIN_SHADER_DIR}/${varying}"
        OUTPUT_DIR "${PERLIN_SHADER_OUT}"
        PROFILES ${PERLIN_SHADER_PROFILES}
        INCLUDE_DIRS ${PERLIN_SHADER_INCLUDES}
        OUT_FILES_VAR compiled
        AS_HEADERS
    )
    set(PERLIN_SHADER_OUTPUTS ${PERLIN_SHADER_OUTPUTS} ${compiled} PARENT_SCOPE)
endfunction()

if(NOT PERLIN_PREBUILT_SHADERS)
perlin_compile_shaders(VERTEX varying_terrain.def.sc vs_terrain)
perlin_compile_shaders(FRAGMENT varying_terrain.def.sc fs_terrain_solid fs_terrain_cutout fs_terrain_translucent)
perlin_compile_shaders(VERTEX varying_sky.def.sc vs_sky)
perlin_compile_shaders(FRAGMENT varying_sky.def.sc fs_sky)
perlin_compile_shaders(VERTEX varying_star.def.sc vs_star)
perlin_compile_shaders(FRAGMENT varying_star.def.sc fs_star)
perlin_compile_shaders(VERTEX varying_sunrise.def.sc vs_sunrise)
perlin_compile_shaders(FRAGMENT varying_sunrise.def.sc fs_sunrise)
perlin_compile_shaders(VERTEX varying_celestial.def.sc vs_celestial)
perlin_compile_shaders(FRAGMENT varying_celestial.def.sc fs_celestial)
perlin_compile_shaders(VERTEX varying_cloud.def.sc vs_cloud)
perlin_compile_shaders(FRAGMENT varying_cloud.def.sc fs_cloud)
perlin_compile_shaders(VERTEX varying_screen.def.sc vs_screen)
perlin_compile_shaders(FRAGMENT varying_screen.def.sc fs_screen)
perlin_compile_shaders(VERTEX varying_ui.def.sc vs_ui)
perlin_compile_shaders(FRAGMENT varying_ui.def.sc fs_ui)
perlin_compile_shaders(VERTEX varying_post.def.sc vs_post)
perlin_compile_shaders(FRAGMENT varying_post.def.sc fs_post fs_bloom fs_upscale fs_taa fs_framegen)
perlin_compile_shaders(VERTEX varying_entity_shadow.def.sc vs_entity_shadow)
perlin_compile_shaders(FRAGMENT varying_entity_shadow.def.sc fs_entity_shadow)
perlin_compile_shaders(VERTEX vv/vv_varying_terrain.def.sc vv/vv_vs_terrain)
if("s_5_0" IN_LIST PERLIN_SHADER_PROFILES)
    perlin_compile_shaders(FRAGMENT vv/vv_varying_terrain.def.sc vv/vv_fs_terrain_solid vv/vv_fs_terrain_cutout)
    set(_perlin_profiles_all ${PERLIN_SHADER_PROFILES})
    set(PERLIN_SHADER_PROFILES ${PERLIN_SHADER_PROFILES})
    list(REMOVE_ITEM PERLIN_SHADER_PROFILES s_5_0)
    perlin_compile_shaders(FRAGMENT vv/vv_varying_terrain.def.sc vv/vv_fs_terrain_translucent)
    set(PERLIN_SHADER_PROFILES ${_perlin_profiles_all})
    set(_perlin_water_src "${PERLIN_SHADER_DIR}/vv/vv_fs_terrain_translucent.sc")
    set(_perlin_water_out "${PERLIN_SHADER_OUT}/dxbc/vv_fs_terrain_translucent.sc.bin.h")
    string(REPLACE ";" "|" _perlin_water_inc "${PERLIN_SHADER_INCLUDES}")
    set(_perlin_water_variants
        "O3:full|noopt:full|O3:VV_DX_NO_SSR|O3:VV_DX_NO_CLOUDS|O3:VV_DX_NO_SSR,VV_DX_NO_CLOUDS|O3:VV_DX_NO_SSR,VV_DX_NO_CLOUDS,VV_DX_NO_LIT,VV_DX_NO_SKY|noopt:VV_DX_NO_SSR,VV_DX_NO_CLOUDS,VV_DX_NO_LIT,VV_DX_NO_SKY")
    set(_perlin_water_script "${CMAKE_CURRENT_BINARY_DIR}/PerlinDxbcFallback.cmake")
    file(WRITE "${_perlin_water_script}" [==[
string(REPLACE "|" ";" include_list "${INCLUDES}")
set(include_args)
foreach(dir IN LISTS include_list)
    list(APPEND include_args -i "${dir}")
endforeach()
string(REPLACE "|" ";" variant_list "${VARIANTS}")
set(failed)
foreach(variant IN LISTS variant_list)
    string(REPLACE ":" ";" parts "${variant}")
    list(GET parts 0 opt)
    list(GET parts 1 defs)
    set(extra)
    if(opt STREQUAL "O3")
        list(APPEND extra -O 3)
    endif()
    if(NOT defs STREQUAL "full")
        string(REPLACE "," ";" define_list "${defs}")
        list(APPEND extra --define "${define_list}")
    endif()
    file(REMOVE "${OUT}.tmp")
    execute_process(
        COMMAND "${SHADERC}" -f "${SRC}" ${include_args} -o "${OUT}.tmp" --bin2c "${NAME}"
                --platform windows --type fragment --varyingdef "${VARYING}" --profile s_5_0 --Werror ${extra}
        RESULT_VARIABLE rc
        OUTPUT_VARIABLE out
        ERROR_VARIABLE err
        TIMEOUT 600)
    if("${rc}" STREQUAL "0" AND EXISTS "${OUT}.tmp")
        file(RENAME "${OUT}.tmp" "${OUT}")
        if(failed)
            message(WARNING "[dxbc] ${NAME}: FXC failed on [${failed}]; using [${variant}]")
        else()
            message(STATUS "[dxbc] ${NAME}: compiled [${variant}]")
        endif()
        return()
    endif()
    string(STRIP "${out} ${err}" log)
    message(STATUS "[dxbc] ${NAME}: [${variant}] failed (${rc}) ${log}")
    list(APPEND failed "${variant}")
endforeach()
file(REMOVE "${OUT}.tmp")
file(WRITE "${OUT}" "// FXC could not compile this shader (tried: ${failed}).\n// The game uses its built-in shader for it on Direct3D 11.\nstatic const uint8_t ${NAME}[1] = { 0x00 };\n")
message(WARNING "[dxbc] ${NAME}: FXC failed on every variant [${failed}]; wrote a placeholder (built-in water on Direct3D 11)")
]==])
    add_custom_command(
        OUTPUT "${_perlin_water_out}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${PERLIN_SHADER_OUT}/dxbc"
        COMMAND ${CMAKE_COMMAND} "-DSHADERC=$<TARGET_FILE:bgfx::shaderc>" "-DSRC=${_perlin_water_src}"
                "-DOUT=${_perlin_water_out}" "-DVARYING=${PERLIN_SHADER_DIR}/vv/vv_varying_terrain.def.sc"
                "-DNAME=vv_fs_terrain_translucent_dxbc" "-DINCLUDES=${_perlin_water_inc}"
                "-DVARIANTS=${_perlin_water_variants}"
                -P "${_perlin_water_script}"
        MAIN_DEPENDENCY "${_perlin_water_src}"
        DEPENDS "${PERLIN_SHADER_DIR}/vv/vv_varying_terrain.def.sc" "${PERLIN_SHADER_DIR}/vv/vv_common.sh"
                "${PERLIN_SHADER_DIR}/vv/vv_terrain_common.sh" bgfx::shaderc
        VERBATIM
    )
    list(APPEND PERLIN_SHADER_OUTPUTS "${_perlin_water_out}")
else()
    perlin_compile_shaders(FRAGMENT vv/vv_varying_terrain.def.sc vv/vv_fs_terrain_solid vv/vv_fs_terrain_cutout vv/vv_fs_terrain_translucent)
endif()
perlin_compile_shaders(VERTEX vv/vv_varying_sky.def.sc vv/vv_vs_sky)
perlin_compile_shaders(FRAGMENT vv/vv_varying_sky.def.sc vv/vv_fs_sky)
perlin_compile_shaders(VERTEX vv/vv_varying_star.def.sc vv/vv_vs_star)
perlin_compile_shaders(FRAGMENT vv/vv_varying_star.def.sc vv/vv_fs_star)
perlin_compile_shaders(VERTEX vv/vv_varying_sunrise.def.sc vv/vv_vs_sunrise)
perlin_compile_shaders(FRAGMENT vv/vv_varying_sunrise.def.sc vv/vv_fs_sunrise)
perlin_compile_shaders(VERTEX vv/vv_varying_celestial.def.sc vv/vv_vs_celestial)
perlin_compile_shaders(FRAGMENT vv/vv_varying_celestial.def.sc vv/vv_fs_celestial)
perlin_compile_shaders(VERTEX vv/vv_varying_cloud.def.sc vv/vv_vs_cloud)
perlin_compile_shaders(FRAGMENT vv/vv_varying_cloud.def.sc vv/vv_fs_cloud)
perlin_compile_shaders(VERTEX vv/vv_varying_shadow.def.sc vv/vv_vs_shadow)
perlin_compile_shaders(FRAGMENT vv/vv_varying_shadow.def.sc vv/vv_fs_shadow vv/vv_fs_shadow_cutout vv/vv_fs_shadow_glass)
endif()

if(PERLIN_PREBUILT_SHADERS)
    if(NOT PERLIN_WEB_SHADER_DIR OR NOT EXISTS "${PERLIN_WEB_SHADER_DIR}/essl")
        message(FATAL_ERROR "Set PERLIN_WEB_SHADER_DIR to a directory containing the essl shaders built with PERLIN_WEB_SHADERS_ONLY")
    endif()
    file(COPY "${PERLIN_WEB_SHADER_DIR}/essl" DESTINATION "${PERLIN_SHADER_OUT}")
endif()

set(PERLIN_SHADER_HEADER "${PERLIN_SHADER_OUT}/perlin_shaders.h")
set(PERLIN_SHADER_HEADER_TEXT "#pragma once\n")
foreach(dir dxbc dxil essl glsl spirv metal)
    foreach(name IN LISTS PERLIN_SHADER_NAMES)
        string(APPEND PERLIN_SHADER_HEADER_TEXT
            "#if __has_include(\"${dir}/${name}.sc.bin.h\")\n#include \"${dir}/${name}.sc.bin.h\"\n#endif\n")
    endforeach()
endforeach()
file(GENERATE OUTPUT "${PERLIN_SHADER_HEADER}" CONTENT "${PERLIN_SHADER_HEADER_TEXT}")

if(PERLIN_WEB_SHADERS_ONLY)
    add_custom_target(perlin_web_shaders ALL DEPENDS ${PERLIN_SHADER_OUTPUTS})
    return()
endif()

set(PERLIN_PROTOCOL_OUT "${CMAKE_CURRENT_BINARY_DIR}/generated/protocol")
set(PERLIN_PROTOCOL_VERSION 2193)
set(PERLIN_PROTOCOL_CODEC_COUNT 6)
set(PERLIN_PROTOCOL_SOURCES)
foreach(i RANGE 0 5)
    list(APPEND PERLIN_PROTOCOL_SOURCES "${PERLIN_PROTOCOL_OUT}/Codec${i}.cpp")
endforeach()
set(PERLIN_PROTOCOL_HEADERS "${PERLIN_PROTOCOL_OUT}/Types.hpp" "${PERLIN_PROTOCOL_OUT}/Packets.hpp")
set(PERLIN_PROTOCOL_ROUNDTRIP "${PERLIN_PROTOCOL_OUT}/RoundTrip.cpp")

add_custom_command(
    OUTPUT ${PERLIN_PROTOCOL_HEADERS} ${PERLIN_PROTOCOL_SOURCES} ${PERLIN_PROTOCOL_ROUNDTRIP}
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/protogen/protogen.py"
            --spec "${CMAKE_CURRENT_SOURCE_DIR}/protocol/bedrock-1.26.51.json"
            --overrides "${CMAKE_CURRENT_SOURCE_DIR}/protocol/overrides.json"
            --out "${PERLIN_PROTOCOL_OUT}"
            --parts ${PERLIN_PROTOCOL_CODEC_COUNT}
            --namespace proto${PERLIN_PROTOCOL_VERSION}
    DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/tools/protogen/protogen.py"
            "${CMAKE_CURRENT_SOURCE_DIR}/protocol/bedrock-1.26.51.json"
            "${CMAKE_CURRENT_SOURCE_DIR}/protocol/overrides.json"
    COMMENT "Generating protocol codecs"
    VERBATIM)

add_library(perlin_protocol STATIC
    "${CMAKE_CURRENT_SOURCE_DIR}/src/network/Nbt.cpp"
    ${PERLIN_PROTOCOL_SOURCES}
    ${PERLIN_PROTOCOL_HEADERS})
target_include_directories(perlin_protocol PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${CMAKE_CURRENT_BINARY_DIR}/generated")
target_compile_features(perlin_protocol PUBLIC cxx_std_23)
set_target_properties(perlin_protocol PROPERTIES ARCHIVE_OUTPUT_NAME perlin_protocol_gen)
if(MSVC)
    set_source_files_properties(${PERLIN_PROTOCOL_SOURCES} PROPERTIES COMPILE_OPTIONS "/w;/bigobj;$<$<CONFIG:Debug>:/O1>")
else()
    set_source_files_properties(${PERLIN_PROTOCOL_SOURCES} PROPERTIES COMPILE_OPTIONS "-w;$<$<CONFIG:Debug>:-O1>")
endif()

file(GLOB PERLIN_RESOURCE_FILES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/resources/*")
set(PERLIN_EMBED_DIR "${CMAKE_CURRENT_BINARY_DIR}/embedded")
file(MAKE_DIRECTORY "${PERLIN_EMBED_DIR}")
set(PERLIN_EMBED_NAMES_SOURCE "")
if(MSVC)
    enable_language(RC)
    set(PERLIN_EMBED_SOURCE "${PERLIN_EMBED_DIR}/embedded_resources.rc")
    set(PERLIN_EMBED_NAMES_SOURCE "${PERLIN_EMBED_DIR}/embedded_names.cpp")
    set(text "")
    set(names "// Generated by CMake: names of the RCDATA resources (resource id = index + 1).\n#include <cstddef>\n")
    string(APPEND names "extern \"C\" {\nextern const char* const perlin_embedded_names[] = {\n")
    set(i 0)
    foreach(f IN LISTS PERLIN_RESOURCE_FILES)
        get_filename_component(n "${f}" NAME)
        math(EXPR id "${i}+1")
        string(APPEND text "${id} RCDATA \"${f}\"\n")
        string(APPEND names "    \"${n}\",\n")
        math(EXPR i "${i}+1")
    endforeach()
    string(APPEND names "    nullptr};\nextern const size_t perlin_embedded_count = ${i};\n}\n")
    file(GENERATE OUTPUT "${PERLIN_EMBED_NAMES_SOURCE}" CONTENT "${names}")
else()
    set(PERLIN_EMBED_SOURCE "${PERLIN_EMBED_DIR}/embedded_resources.cpp")
    set(endsection ".previous")
    if(EMSCRIPTEN)
        set(sym "")
        set(section ".section .rodata.perlin,\\\"\\\",@")
        set(endsection ".text")
    elseif(APPLE)
        set(sym "_")
        set(section ".section __DATA,__const")
    elseif(WIN32)
        set(sym "")
        set(section ".section .rdata,\\\"dr\\\"")
    else()
        set(sym "")
        set(section ".section .rodata")
    endif()
    set(text "// Generated by CMake: resources/ linked into the executable.\n#include <cstddef>\n#include <cstdint>\n")
    string(APPEND text "asm(\n\"${section}\\n\"\n\".balign 16\\n\"\n")
    set(table "")
    set(decls "")
    set(i 0)
    foreach(f IN LISTS PERLIN_RESOURCE_FILES)
        get_filename_component(n "${f}" NAME)
        string(APPEND text "\".globl ${sym}perlin_res_${i}\\n${sym}perlin_res_${i}:\\n.incbin \\\"${f}\\\"\\n.globl ${sym}perlin_res_${i}_end\\n${sym}perlin_res_${i}_end:\\n.balign 16\\n\"\n")
        string(APPEND decls "extern const uint8_t perlin_res_${i}[]; extern const uint8_t perlin_res_${i}_end[];\n")
        string(APPEND table "    {\"${n}\", perlin_res_${i}, perlin_res_${i}_end},\n")
        math(EXPR i "${i}+1")
    endforeach()
    string(APPEND text "\"${endsection}\\n\"\n);\nextern \"C\" {\n${decls}")
    string(APPEND text "struct PerlinEmbeddedFile { const char* name; const uint8_t* begin; const uint8_t* end; };\n")
    string(APPEND text "extern const PerlinEmbeddedFile perlin_embedded_files[] = {\n${table}    {nullptr, nullptr, nullptr}};\n")
    string(APPEND text "extern const size_t perlin_embedded_count = ${i};\n}\n")
endif()
file(GENERATE OUTPUT "${PERLIN_EMBED_SOURCE}" CONTENT "${text}")
set_source_files_properties("${PERLIN_EMBED_SOURCE}" PROPERTIES GENERATED TRUE OBJECT_DEPENDS "${PERLIN_RESOURCE_FILES}")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${PERLIN_RESOURCE_FILES})

file(GLOB_RECURSE PERLIN_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")
list(FILTER PERLIN_SOURCES EXCLUDE REGEX "/src/network/Nbt\\.cpp")
list(FILTER PERLIN_SOURCES EXCLUDE REGEX "/src/mods/")
list(FILTER PERLIN_SOURCES EXCLUDE REGEX "/src/(src|tests)/")
list(FILTER PERLIN_SOURCES EXCLUDE REGEX "/src/client/mods/WasmEdgeStop\\.cpp$")
if(EMSCRIPTEN)
    list(FILTER PERLIN_SOURCES EXCLUDE REGEX "/src/client/mods/WasmRuntime\\.cpp$")
else()
    list(FILTER PERLIN_SOURCES EXCLUDE REGEX "/src/client/mods/WasmRuntimeWasm3\\.cpp$")
endif()
if(EMSCRIPTEN)
    list(FILTER PERLIN_SOURCES EXCLUDE REGEX "/src/network/(NetherNet|Http|RakNet)\\.cpp")
else()
    list(FILTER PERLIN_SOURCES EXCLUDE REGEX "_web\\.cpp$")
endif()

if(ANDROID)
    add_library(Perlin SHARED ${PERLIN_SOURCES} ${PERLIN_EMBED_SOURCE} ${PERLIN_EMBED_NAMES_SOURCE} ${PERLIN_SHADER_OUTPUTS})
    set_target_properties(Perlin PROPERTIES OUTPUT_NAME main)
elseif(WIN32)
    add_executable(Perlin WIN32 ${PERLIN_SOURCES} ${PERLIN_EMBED_SOURCE} ${PERLIN_EMBED_NAMES_SOURCE} ${PERLIN_SHADER_OUTPUTS})
elseif(APPLE)
    add_executable(Perlin ${PERLIN_SOURCES} ${PERLIN_EMBED_SOURCE} ${PERLIN_EMBED_NAMES_SOURCE} ${PERLIN_SHADER_OUTPUTS})
else()
    add_executable(Perlin ${PERLIN_SOURCES} ${PERLIN_EMBED_SOURCE} ${PERLIN_EMBED_NAMES_SOURCE} ${PERLIN_SHADER_OUTPUTS})
endif()

target_compile_features(Perlin PRIVATE cxx_std_23)

target_include_directories(Perlin PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${CMAKE_CURRENT_SOURCE_DIR}/mods/sdk"
    "${PERLIN_SHADER_OUT}"
    "${stb_SOURCE_DIR}"
    "${concurrentqueue_SOURCE_DIR}"
    "${glm_SOURCE_DIR}"
    "${glslang_SOURCE_DIR}"
    "${SPIRV-Cross_SOURCE_DIR}"
)
if(NOT EMSCRIPTEN)
    target_include_directories(Perlin PRIVATE "${httplib_SOURCE_DIR}" "${libdatachannel_SOURCE_DIR}/include")
endif()

target_compile_definitions(Perlin PRIVATE PERLIN_SOURCE_DIR="${CMAKE_SOURCE_DIR}" PERLIN_VERSION="${PROJECT_VERSION}" ${PERLIN_SHADER_DEFS}
    GLM_FORCE_DEPTH_ZERO_TO_ONE
    OPENSSL_SUPPRESS_DEPRECATED
)
if(NOT EMSCRIPTEN)
    target_compile_definitions(Perlin PRIVATE CPPHTTPLIB_OPENSSL_SUPPORT)
endif()

if(WIN32)
    target_compile_definitions(Perlin PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN _WIN32_WINNT=0x0A00)
endif()

target_link_libraries(Perlin PRIVATE
    glslang SPIRV glslang-default-resource-limits spirv-cross-hlsl spirv-cross-glsl spirv-cross-core
    ${PERLIN_WASM_ENGINE}
    perlin_protocol
    glaze::glaze
    miniz
    OpenSSL::Crypto
    Threads::Threads
    SDL3::SDL3
    rmlui_core
    bgfx
    bimg
    bx
)
if(NOT EMSCRIPTEN)
    target_link_libraries(Perlin PRIVATE datachannel-static OpenSSL::SSL)
endif()

if(EMSCRIPTEN)
    set(PERLIN_WEB_DIR "${CMAKE_CURRENT_SOURCE_DIR}/web")
    set_target_properties(Perlin PROPERTIES OUTPUT_NAME index SUFFIX ".html")
    target_link_options(Perlin PRIVATE
        -sWASMFS=1
        -sFORCE_FILESYSTEM=1
        -sMAX_WEBGL_VERSION=2
        -sMIN_WEBGL_VERSION=2
        -sFULL_ES3=1
        -sALLOW_MEMORY_GROWTH=1
        -sINITIAL_MEMORY=1024MB
        -sMAXIMUM_MEMORY=4GB
        -sSTACK_SIZE=8MB
        -sDEFAULT_PTHREAD_STACK_SIZE=2MB
        -sPTHREAD_POOL_SIZE=20
        -sPTHREAD_POOL_SIZE_STRICT=0
        -sFETCH=1
        -sEXIT_RUNTIME=0
        -sASSERTIONS=0
        -sENVIRONMENT=web,worker
        -sTEXTDECODER=1
        "-sEXPORTED_FUNCTIONS=_main,_malloc,_free,_perlin_nn_on_message,_perlin_nn_on_closed"
        "-sEXPORTED_RUNTIME_METHODS=FS,callMain,ccall,cwrap,UTF8ToString,stringToUTF8,lengthBytesUTF8"
        "--js-library=${PERLIN_WEB_DIR}/library_nethernet.js"
        "--shell-file=${PERLIN_WEB_DIR}/shell.html")
    option(PERLIN_WEB_PROFILING "Keep function names and enable runtime checks in the web build" OFF)
    if(PERLIN_WEB_PROFILING)
        target_link_options(Perlin PRIVATE --profiling-funcs -sASSERTIONS=1 -sSTACK_OVERFLOW_CHECK=2)
    endif()
    set_property(TARGET Perlin APPEND PROPERTY LINK_DEPENDS "${PERLIN_WEB_DIR}/library_nethernet.js" "${PERLIN_WEB_DIR}/shell.html")
    add_custom_command(TARGET Perlin POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy_directory "${PERLIN_WEB_DIR}/static" "$<TARGET_FILE_DIR:Perlin>")
elseif(WIN32)
    target_link_libraries(Perlin PRIVATE ws2_32 crypt32 dbghelp advapi32 user32 gdi32 bcrypt iphlpapi)
elseif(APPLE)
    target_link_libraries(Perlin PRIVATE "-framework Security" "-framework CoreFoundation")
    target_compile_definitions(Perlin PRIVATE CPPHTTPLIB_USE_CERTS_FROM_MACOSX_KEYCHAIN)
elseif(ANDROID)
    target_link_libraries(Perlin PRIVATE log android EGL GLESv3)
    target_link_options(Perlin PRIVATE -Wl,--build-id=none -Wl,-z,max-page-size=16384)
elseif(NOT EMSCRIPTEN)
    target_link_libraries(Perlin PRIVATE dl)
    target_link_options(Perlin PRIVATE -static-libstdc++ -static-libgcc)
endif()

option(PERLIN_DEBUG_OPT "Optimise Perlin's own sources in Debug builds" ON)
if(PERLIN_DEBUG_OPT)
    if(MSVC)
        set_source_files_properties(${PERLIN_SOURCES} PROPERTIES COMPILE_OPTIONS "$<$<CONFIG:Debug>:/O2;/Ob2>")
    else()
        set_source_files_properties(${PERLIN_SOURCES} PROPERTIES COMPILE_OPTIONS "$<$<CONFIG:Debug>:-O2>")
    endif()
endif()

if(MSVC)
    target_compile_options(Perlin PRIVATE /W4 /permissive- /EHsc /Zc:__cplusplus)
    if(PERLIN_CLANG_CL_BUILD)
        target_compile_options(Perlin PRIVATE -Wno-unused-parameter -Wno-microsoft-cast -Wno-microsoft-enum-value
                               -Wno-language-extension-token -Wno-c++98-compat -Wno-pragma-pack)
    endif()
    target_compile_options(Perlin PRIVATE /Zi)
    target_link_options(Perlin PRIVATE /DEBUG /OPT:REF /OPT:ICF /MAP)
    string(REGEX REPLACE "[/-]RTC1" "" CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG}")
    set_property(TARGET Perlin PROPERTY MSVC_RUNTIME_CHECKS "")
    file(GLOB_RECURSE PERLIN_HOT_SOURCES
        "${CMAKE_CURRENT_SOURCE_DIR}/src/client/renderer/chunk/*.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/client/multiplayer/*.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/network/*.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/world/*.cpp")
    set_source_files_properties(${PERLIN_HOT_SOURCES} PROPERTIES COMPILE_OPTIONS "/O2;/Ob2")
else()
    target_compile_options(Perlin PRIVATE -Wall -Wextra -Wno-unused-parameter)
endif()

if(MSVC AND NOT PERLIN_CLANG_CL_BUILD)
    target_compile_options(Perlin PRIVATE "$<$<CONFIG:Release>:/Gy;/Gw;/Zc:inline;/Oi>")
elseif(PERLIN_CLANG_CL_BUILD)
    target_compile_options(Perlin PRIVATE "$<$<CONFIG:Release>:/Gy;/Gw;/Zc:inline;-fno-math-errno;-fno-trapping-math>")
else()
    target_compile_options(Perlin PRIVATE "$<$<CONFIG:Release>:-fno-math-errno;-fno-trapping-math;-ffunction-sections;-fdata-sections>")
    if(NOT APPLE AND NOT EMSCRIPTEN)
        target_link_options(Perlin PRIVATE "$<$<CONFIG:Release>:-Wl,--gc-sections;-Wl,--as-needed>")
    endif()
endif()

option(PERLIN_FAST_MATH "Relax floating-point rules (faster maths, results differ in the last digits)" OFF)
if(PERLIN_FAST_MATH)
    if(MSVC AND NOT PERLIN_CLANG_CL_BUILD)
        target_compile_options(Perlin PRIVATE /fp:fast)
    else()
        target_compile_options(Perlin PRIVATE -fno-signed-zeros -freciprocal-math -ffp-contract=fast)
    endif()
    message(STATUS "Perlin: fast math is ON")
endif()

include(CheckIPOSupported)
if(EMSCRIPTEN OR ANDROID)
    set(PERLIN_IPO OFF)
else()
    check_ipo_supported(RESULT PERLIN_IPO OUTPUT PERLIN_IPO_LOG)
endif()
if(PERLIN_IPO)
    set_property(TARGET Perlin PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELEASE ON)
    if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
        set_source_files_properties(src/client/entity/EntityRenderer.cpp PROPERTIES COMPILE_OPTIONS "-fno-lto")
    elseif(MSVC)
        set_source_files_properties(src/client/entity/EntityRenderer.cpp PROPERTIES COMPILE_OPTIONS "/GL-")
    endif()
endif()

if(NOT ANDROID)
    install(TARGETS Perlin RUNTIME DESTINATION .)
endif()
if(MSVC)
    install(FILES "$<TARGET_PDB_FILE:Perlin>" DESTINATION . OPTIONAL)
    install(FILES "$<TARGET_FILE_DIR:Perlin>/Perlin.map" DESTINATION . OPTIONAL)
endif()
option(PERLIN_BUILD_TESTS "Build unit tests" OFF)
if(PERLIN_BUILD_TESTS AND NOT EMSCRIPTEN AND NOT ANDROID)
    enable_testing()
    add_executable(hud_dump tests/hud_dump.cpp src/client/ui/Json.cpp src/client/ui/UiExpr.cpp src/client/ui/JsonUi.cpp
        src/client/ui/UiLayout.cpp src/client/ui/McFont.cpp src/client/ui/RmlEmit.cpp src/client/ui/HudData.cpp
        src/client/ui/HudRenderers.cpp src/client/ui/EffectData.cpp src/client/i18n/Lang.cpp src/client/resources/ServerPackRegistry.cpp src/client/i18n/BuiltinEnglish.cpp src/util/Resources.cpp src/util/Paths.cpp)
    target_include_directories(hud_dump PRIVATE src "${stb_SOURCE_DIR}")
    target_compile_features(hud_dump PRIVATE cxx_std_23)
    add_executable(attachable_selftest tests/attachable_selftest.cpp
        src/client/attachable/Molang.cpp src/client/attachable/Animation.cpp src/client/attachable/Geometry.cpp
        src/client/ui/Json.cpp)
    target_include_directories(attachable_selftest PRIVATE src "${glm_SOURCE_DIR}")
    target_compile_features(attachable_selftest PRIVATE cxx_std_23)
    add_test(NAME attachable_selftest COMMAND attachable_selftest)

    add_executable(particle_selftest tests/particle_selftest.cpp src/client/particle/DataParticles.cpp
        src/client/attachable/Molang.cpp src/client/ui/Json.cpp)
    target_include_directories(particle_selftest PRIVATE src "${glm_SOURCE_DIR}")
    target_compile_features(particle_selftest PRIVATE cxx_std_23)
    add_test(NAME particle_selftest COMMAND particle_selftest)

    add_executable(audio_selftest tests/audio_selftest.cpp src/client/audio/AudioEngine.cpp src/client/audio/Decode.cpp
        src/client/audio/SoundSystem.cpp src/client/audio/Subtitles.cpp src/client/ui/Json.cpp
        src/client/i18n/Lang.cpp src/client/resources/ServerPackRegistry.cpp src/client/i18n/BuiltinEnglish.cpp src/util/Resources.cpp src/util/Paths.cpp)
    target_compile_definitions(audio_selftest PRIVATE PERLIN_LANG_FILE="${CMAKE_SOURCE_DIR}/resources/perlin_en_US.lang")
    target_include_directories(audio_selftest PRIVATE src "${glm_SOURCE_DIR}" "${stb_SOURCE_DIR}")
    target_link_libraries(audio_selftest PRIVATE SDL3::SDL3)
    target_compile_features(audio_selftest PRIVATE cxx_std_23)
    add_test(NAME audio_selftest COMMAND audio_selftest)

    add_executable(lang_selftest tests/lang_selftest.cpp src/client/i18n/Lang.cpp src/client/resources/ServerPackRegistry.cpp src/client/i18n/BuiltinEnglish.cpp src/client/ui/Json.cpp src/util/Resources.cpp src/util/Paths.cpp)
    target_include_directories(lang_selftest PRIVATE src)
    target_compile_features(lang_selftest PRIVATE cxx_std_23)
    add_test(NAME lang_selftest COMMAND lang_selftest "${CMAKE_SOURCE_DIR}/resources")

    add_executable(material_selftest tests/material_selftest.cpp src/client/renderer/MaterialDb.cpp src/client/entity/Materials.cpp src/client/ui/Json.cpp)
    target_include_directories(material_selftest PRIVATE src)
    target_compile_features(material_selftest PRIVATE cxx_std_23)
    add_test(NAME material_selftest COMMAND material_selftest)

    add_executable(chunk_selftest
        tests/chunk_selftest.cpp
        src/world/level/chunk/ChunkCodec.cpp)
    target_link_libraries(chunk_selftest PRIVATE perlin_protocol)
    add_test(NAME chunk_selftest COMMAND chunk_selftest)

    add_executable(packet_selftest tests/packet_selftest.cpp ${PERLIN_PROTOCOL_ROUNDTRIP})
    target_link_libraries(packet_selftest PRIVATE perlin_protocol)
    add_test(NAME packet_selftest COMMAND packet_selftest)

    add_executable(input_selftest
        tests/input_selftest.cpp
        src/client/input/Keys.cpp
        src/client/input/KeyMapping.cpp
        src/client/renderer/Environment.cpp
        src/client/renderer/Fog.cpp
        src/client/renderer/Lightmap.cpp
        src/client/renderer/SkyGeometry.cpp)
    target_include_directories(input_selftest PRIVATE src "${glm_SOURCE_DIR}")
    add_test(NAME input_selftest COMMAND input_selftest)

    add_executable(physics_selftest
        tests/physics_selftest.cpp
        src/client/player/PlayerPhysics.cpp)
    target_include_directories(physics_selftest PRIVATE src)
    target_compile_features(physics_selftest PRIVATE cxx_std_23)
    add_test(NAME physics_selftest COMMAND physics_selftest)

    add_executable(bedsim_trace_selftest
        tests/bedsim_trace_selftest.cpp
        src/client/player/PlayerPhysics.cpp)
    target_include_directories(bedsim_trace_selftest PRIVATE src)
    target_compile_features(bedsim_trace_selftest PRIVATE cxx_std_23)
    add_test(NAME bedsim_trace_selftest COMMAND bedsim_trace_selftest "${CMAKE_CURRENT_SOURCE_DIR}/tests/data/bedsim_traces.txt")

    add_executable(panorama_selftest tests/panorama_selftest.cpp src/client/ui/TitlePanorama.cpp)
    target_include_directories(panorama_selftest PRIVATE src)
    target_compile_features(panorama_selftest PRIVATE cxx_std_23)
    add_test(NAME panorama_selftest COMMAND panorama_selftest)

    add_executable(uitemplate_selftest tests/uitemplate_selftest.cpp src/client/ui/UiTemplate.cpp)
    target_include_directories(uitemplate_selftest PRIVATE src)
    target_compile_features(uitemplate_selftest PRIVATE cxx_std_23)
    add_test(NAME uitemplate_selftest COMMAND uitemplate_selftest)

    add_executable(trace_diff
        tests/trace_diff.cpp
        src/client/player/PlayerPhysics.cpp)
    target_include_directories(trace_diff PRIVATE src)
    target_compile_features(trace_diff PRIVATE cxx_std_23)

    add_executable(damage_selftest tests/damage_selftest.cpp)
    target_include_directories(damage_selftest PRIVATE src "${glm_SOURCE_DIR}")
    target_compile_features(damage_selftest PRIVATE cxx_std_23)
    add_test(NAME damage_selftest COMMAND damage_selftest)

    add_executable(models_selftest tests/models_selftest.cpp src/client/renderer/block/BlockModels.cpp)
    target_include_directories(models_selftest PRIVATE src)
    target_compile_features(models_selftest PRIVATE cxx_std_23)
    add_test(NAME models_selftest COMMAND models_selftest "${CMAKE_CURRENT_SOURCE_DIR}/resources/block_models.bin")

    file(GLOB TEXTURE_TEST_SOURCES CONFIGURE_DEPENDS
        src/client/renderer/block/*.cpp src/client/resources/TextureLoader.cpp src/client/resources/TexturePack.cpp)
    add_executable(texture_selftest
        tests/texture_selftest.cpp ${TEXTURE_TEST_SOURCES}
        src/client/Options.cpp src/client/input/KeyMapping.cpp
        src/util/Json.cpp src/util/Paths.cpp
        src/world/level/block/BlockPalette.cpp src/util/Resources.cpp
        src/client/resources/ServerPackRegistry.cpp src/client/resources/ServerBlocks.cpp src/client/ui/Json.cpp)
    target_include_directories(texture_selftest PRIVATE src "${stb_SOURCE_DIR}")
    target_link_libraries(texture_selftest PRIVATE glaze::glaze miniz Threads::Threads)
    target_compile_features(texture_selftest PRIVATE cxx_std_23)
    if(PERLIN_TEXTURE_TEST_ASSETS)
        add_test(NAME texture_selftest COMMAND texture_selftest "${PERLIN_TEXTURE_TEST_ASSETS}")
    endif()

    add_executable(auth_selftest
        tests/auth_selftest.cpp
        src/network/XboxAuth.cpp src/network/LoginBuilder.cpp src/network/ClientIdentity.cpp src/network/Http.cpp src/network/PacketCipher.cpp
        src/client/multiplayer/SkinLoader.cpp src/client/resources/TextureLoader.cpp src/util/Json.cpp)
    target_include_directories(auth_selftest PRIVATE src "${stb_SOURCE_DIR}" "${httplib_SOURCE_DIR}")
    target_compile_definitions(auth_selftest PRIVATE CPPHTTPLIB_OPENSSL_SUPPORT OPENSSL_SUPPRESS_DEPRECATED)
    target_link_libraries(auth_selftest PRIVATE glaze::glaze miniz OpenSSL::SSL OpenSSL::Crypto Threads::Threads)
    if(WIN32)
        target_link_libraries(auth_selftest PRIVATE ws2_32 crypt32)
    elseif(APPLE)
        target_link_libraries(auth_selftest PRIVATE "-framework Security" "-framework CoreFoundation")
        target_compile_definitions(auth_selftest PRIVATE CPPHTTPLIB_USE_CERTS_FROM_MACOSX_KEYCHAIN)
    endif()
    target_compile_features(auth_selftest PRIVATE cxx_std_23)
    add_test(NAME auth_selftest COMMAND auth_selftest)

    add_executable(rak_selftest tests/rak_selftest.cpp src/network/RakNet.cpp)
    target_include_directories(rak_selftest PRIVATE src "${concurrentqueue_SOURCE_DIR}")
    target_link_libraries(rak_selftest PRIVATE Threads::Threads)
    if(WIN32)
        target_link_libraries(rak_selftest PRIVATE ws2_32)
    endif()
    target_compile_features(rak_selftest PRIVATE cxx_std_23)

    add_executable(clientassets_selftest tests/clientassets_selftest.cpp src/client/resources/ClientAssets.cpp src/client/resources/TextureLoader.cpp src/util/Json.cpp)
    target_include_directories(clientassets_selftest PRIVATE src "${stb_SOURCE_DIR}")
    target_link_libraries(clientassets_selftest PRIVATE glaze::glaze miniz)
    target_compile_features(clientassets_selftest PRIVATE cxx_std_23)

    add_executable(shaderpack_selftest tests/shaderpack_selftest.cpp src/client/resources/ShaderPack.cpp src/client/ui/Json.cpp tests/BundledShaderStub.cpp)
    target_include_directories(shaderpack_selftest PRIVATE src)
    target_link_libraries(shaderpack_selftest PRIVATE miniz)
    target_compile_features(shaderpack_selftest PRIVATE cxx_std_23)
    add_test(NAME shaderpack_selftest COMMAND shaderpack_selftest)

    add_executable(light_selftest
        tests/light_selftest.cpp
        src/world/level/lighting/ChunkLighting.cpp
        src/world/level/lighting/LightRegistry.cpp
        src/util/Json.cpp
        src/world/level/Level.cpp
        src/world/level/block/BlockPalette.cpp src/util/Resources.cpp src/util/Paths.cpp)
    target_include_directories(light_selftest PRIVATE src)
    target_link_libraries(light_selftest PRIVATE glaze::glaze Threads::Threads)
    target_compile_features(light_selftest PRIVATE cxx_std_23)
    add_test(NAME light_selftest COMMAND light_selftest)

    add_executable(mod_selftest tests/mod_selftest.cpp src/client/mods/ModManager.cpp src/client/mods/WasmRuntime.cpp src/client/ui/Json.cpp)
    target_include_directories(mod_selftest PRIVATE src mods/sdk)
    target_link_libraries(mod_selftest PRIVATE perlin_wasmedge Threads::Threads)
    target_compile_features(mod_selftest PRIVATE cxx_std_23)
    set(PERLIN_TEST_PLUGINS "${CMAKE_CURRENT_SOURCE_DIR}/tests/data/plugins")
    add_test(NAME mod_selftest COMMAND mod_selftest
        "${PERLIN_TEST_PLUGINS}/hello64.wasm" "${PERLIN_TEST_PLUGINS}/hello32.wasm"
        "${PERLIN_TEST_PLUGINS}/loop_64.wasm" "${PERLIN_TEST_PLUGINS}/loop_32.wasm"
        "${CMAKE_CURRENT_SOURCE_DIR}/mods/prebuilt/waila.wasm")

    find_package(Python3 QUIET COMPONENTS Interpreter)
    if(Python3_FOUND)
        add_test(NAME mod_api_check COMMAND ${Python3_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/mods/tools/check_api.py")
        if(NOT MSVC)
            set(PERLIN_MODS_GEN "${CMAKE_CURRENT_BINARY_DIR}/mods_gen")
            foreach(spec "waila:mods/waila/waila.json" "sdk:mods/tests/data/settings_test.json")
                string(REPLACE ":" ";" spec_parts "${spec}")
                list(GET spec_parts 0 gen_name)
                list(GET spec_parts 1 gen_manifest)
                add_custom_command(OUTPUT "${PERLIN_MODS_GEN}/${gen_name}/mod_settings.hpp"
                    COMMAND ${CMAKE_COMMAND} -E make_directory "${PERLIN_MODS_GEN}/${gen_name}"
                    COMMAND ${Python3_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/mods/tools/perlin_settings.py"
                            "${CMAKE_CURRENT_SOURCE_DIR}/${gen_manifest}" "${PERLIN_MODS_GEN}/${gen_name}/mod_settings.hpp"
                    DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/${gen_manifest}" "${CMAKE_CURRENT_SOURCE_DIR}/mods/tools/perlin_settings.py")
            endforeach()
            add_executable(waila_test mods/waila/tests/waila_test.cpp "${PERLIN_MODS_GEN}/waila/mod_settings.hpp")
            target_include_directories(waila_test PRIVATE mods/sdk mods/waila "${PERLIN_MODS_GEN}/waila")
            target_compile_definitions(waila_test PRIVATE MANIFEST="${CMAKE_CURRENT_SOURCE_DIR}/mods/waila/waila.json")
            target_compile_features(waila_test PRIVATE cxx_std_23)
            add_test(NAME waila_test COMMAND waila_test)
            add_executable(sdk_test mods/tests/sdk_test.cpp "${PERLIN_MODS_GEN}/sdk/mod_settings.hpp")
            target_include_directories(sdk_test PRIVATE mods/sdk "${PERLIN_MODS_GEN}/sdk")
            target_compile_features(sdk_test PRIVATE cxx_std_23)
            add_test(NAME sdk_test COMMAND sdk_test)
            foreach(wire_test abi_test meta_test)
                add_executable(${wire_test} mods/tests/${wire_test}.cpp)
                target_include_directories(${wire_test} PRIVATE mods/sdk)
                target_compile_features(${wire_test} PRIVATE cxx_std_23)
                add_test(NAME ${wire_test} COMMAND ${wire_test})
            endforeach()
            if(APPLE)
                foreach(native_test waila_test sdk_test abi_test meta_test)
                    target_compile_definitions(${native_test} PRIVATE _LIBCPP_DISABLE_AVAILABILITY)
                endforeach()
            endif()
        endif()
    endif()
endif()
