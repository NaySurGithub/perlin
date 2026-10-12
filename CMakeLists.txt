cmake_minimum_required(VERSION 3.28)

if(APPLE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "12.0" CACHE STRING "" FORCE)
endif()

project(Perlin VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_POLICY_VERSION_MINIMUM 3.10)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

if(WIN32)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

add_compile_definitions(GLM_FORCE_DEPTH_ZERO_TO_ONE)

# Everything is linked statically so the executable is the whole program: no OpenSSL / libdatachannel / C++ runtime
# files next to it, and the data files from resources/ are embedded in it.
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
find_package(OpenSSL REQUIRED)
find_package(Threads REQUIRED)
find_package(Python3 REQUIRED COMPONENTS Interpreter)

include(cmake/CPM.cmake)

CPMAddPackage("gh:stephenberry/glaze@7.1.1")
CPMAddPackage("gh:cameron314/concurrentqueue@1.0.4")
CPMAddPackage("gh:g-truc/glm#1.0.1")
CPMAddPackage(NAME stb GITHUB_REPOSITORY nothings/stb GIT_TAG master DOWNLOAD_ONLY YES)
CPMAddPackage(NAME httplib GITHUB_REPOSITORY yhirose/cpp-httplib GIT_TAG v0.60.1 DOWNLOAD_ONLY YES)

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
    OPTIONS "USE_GNUTLS OFF" "USE_NICE OFF" "NO_TESTS ON" "NO_EXAMPLES ON"
)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
# libdatachannel's install rules want its shared target to exist, but only the static one is linked: don't build the shared one.
set_target_properties(datachannel PROPERTIES EXCLUDE_FROM_ALL ON)

CPMAddPackage(NAME miniz GITHUB_REPOSITORY richgel999/miniz GIT_TAG 3.0.2)

set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON CACHE BOOL "" FORCE)
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
set(SDL_TESTS OFF CACHE BOOL "" FORCE)
set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SDL_INSTALL OFF CACHE BOOL "" FORCE)
CPMAddPackage(NAME SDL3 GITHUB_REPOSITORY libsdl-org/SDL GIT_TAG release-3.4.18)

set(BGFX_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BGFX_BUILD_TOOLS ON CACHE BOOL "" FORCE)
set(BGFX_BUILD_TOOLS_SHADER ON CACHE BOOL "" FORCE)
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

set(PERLIN_SHADER_DIR "${CMAKE_CURRENT_SOURCE_DIR}/src/shaders")
set(PERLIN_SHADER_OUT "${CMAKE_CURRENT_BINARY_DIR}/shaders")
file(MAKE_DIRECTORY "${PERLIN_SHADER_OUT}")

if(WIN32)
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

set(PERLIN_SHADER_INCLUDES "${bgfx_SOURCE_DIR}/bgfx/src" "${PERLIN_SHADER_DIR}")
set(PERLIN_SHADER_OUTPUTS)
set(PERLIN_SHADER_NAMES
    vs_terrain fs_terrain_solid fs_terrain_cutout fs_terrain_translucent vs_sky fs_sky vs_star fs_star
    vs_sunrise fs_sunrise vs_celestial fs_celestial vs_cloud fs_cloud vs_screen fs_screen)

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

set(PERLIN_SHADER_HEADER "${PERLIN_SHADER_OUT}/perlin_shaders.h")
set(PERLIN_SHADER_HEADER_TEXT "#pragma once\n")
foreach(dir dxbc dxil essl glsl spirv metal)
    foreach(name IN LISTS PERLIN_SHADER_NAMES)
        string(APPEND PERLIN_SHADER_HEADER_TEXT
            "#if __has_include(\"${dir}/${name}.sc.bin.h\")\n#include \"${dir}/${name}.sc.bin.h\"\n#endif\n")
    endforeach()
endforeach()
file(GENERATE OUTPUT "${PERLIN_SHADER_HEADER}" CONTENT "${PERLIN_SHADER_HEADER_TEXT}")

set(PERLIN_PROTOCOL_OUT "${CMAKE_CURRENT_BINARY_DIR}/generated/protocol")
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
    DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/tools/protogen/protogen.py"
            "${CMAKE_CURRENT_SOURCE_DIR}/protocol/bedrock-1.26.51.json"
            "${CMAKE_CURRENT_SOURCE_DIR}/protocol/overrides.json"
    COMMENT "Generating protocol codecs"
    VERBATIM)

file(GLOB PERLIN_PROTOCOL_CUSTOM CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/network/custom/*.cpp")
add_library(perlin_protocol STATIC
    "${CMAKE_CURRENT_SOURCE_DIR}/src/network/Nbt.cpp"
    ${PERLIN_PROTOCOL_CUSTOM}
    ${PERLIN_PROTOCOL_SOURCES}
    ${PERLIN_PROTOCOL_HEADERS})
target_include_directories(perlin_protocol PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${CMAKE_CURRENT_BINARY_DIR}/generated")
target_compile_features(perlin_protocol PUBLIC cxx_std_23)
if(MSVC)
    set_source_files_properties(${PERLIN_PROTOCOL_SOURCES} PROPERTIES COMPILE_OPTIONS "/w;/bigobj")
else()
    set_source_files_properties(${PERLIN_PROTOCOL_SOURCES} PROPERTIES COMPILE_OPTIONS "-w")
endif()

# Embed resources/* into the executable.
file(GLOB PERLIN_RESOURCE_FILES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/resources/*")
set(PERLIN_EMBED_DIR "${CMAKE_CURRENT_BINARY_DIR}/embedded")
file(MAKE_DIRECTORY "${PERLIN_EMBED_DIR}")
set(PERLIN_EMBED_NAMES_SOURCE "")
if(MSVC)
    # Windows resources (RCDATA, numbered 1..N) plus a generated name table. rc.exe is only used for MSVC; MinGW and
    # everything else embed with .incbin below.
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
    if(APPLE)
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
    string(APPEND text ");\nextern \"C\" {\n${decls}")
    string(APPEND text "struct PerlinEmbeddedFile { const char* name; const uint8_t* begin; const uint8_t* end; };\n")
    string(APPEND text "extern const PerlinEmbeddedFile perlin_embedded_files[] = {\n${table}    {nullptr, nullptr, nullptr}};\n")
    string(APPEND text "extern const size_t perlin_embedded_count = ${i};\n}\n")
endif()
file(GENERATE OUTPUT "${PERLIN_EMBED_SOURCE}" CONTENT "${text}")
set_source_files_properties("${PERLIN_EMBED_SOURCE}" PROPERTIES GENERATED TRUE OBJECT_DEPENDS "${PERLIN_RESOURCE_FILES}")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${PERLIN_RESOURCE_FILES})

file(GLOB_RECURSE PERLIN_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")
list(FILTER PERLIN_SOURCES EXCLUDE REGEX "/src/network/(Nbt\\.cpp|custom/)")

if(WIN32)
    add_executable(Perlin WIN32 ${PERLIN_SOURCES} ${PERLIN_EMBED_SOURCE} ${PERLIN_EMBED_NAMES_SOURCE} ${PERLIN_SHADER_OUTPUTS})
elseif(APPLE)
    add_executable(Perlin ${PERLIN_SOURCES} ${PERLIN_EMBED_SOURCE} ${PERLIN_EMBED_NAMES_SOURCE} ${PERLIN_SHADER_OUTPUTS})
else()
    add_executable(Perlin ${PERLIN_SOURCES} ${PERLIN_EMBED_SOURCE} ${PERLIN_EMBED_NAMES_SOURCE} ${PERLIN_SHADER_OUTPUTS})
endif()

target_compile_features(Perlin PRIVATE cxx_std_23)

target_include_directories(Perlin PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${PERLIN_SHADER_OUT}"
    "${stb_SOURCE_DIR}"
    "${httplib_SOURCE_DIR}"
    "${concurrentqueue_SOURCE_DIR}"
    "${glm_SOURCE_DIR}"
    "${libdatachannel_SOURCE_DIR}/include"
)

target_compile_definitions(Perlin PRIVATE ${PERLIN_SHADER_DEFS}
    GLM_FORCE_DEPTH_ZERO_TO_ONE
    CPPHTTPLIB_OPENSSL_SUPPORT
    OPENSSL_SUPPRESS_DEPRECATED
)

if(WIN32)
    target_compile_definitions(Perlin PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN _WIN32_WINNT=0x0A00)
endif()

target_link_libraries(Perlin PRIVATE
    perlin_protocol
    glaze::glaze
    miniz
    datachannel-static
    OpenSSL::SSL
    OpenSSL::Crypto
    Threads::Threads
    SDL3::SDL3
    bgfx
    bimg
    bx
)

if(WIN32)
    target_link_libraries(Perlin PRIVATE ws2_32 crypt32 dbghelp advapi32 user32 gdi32 bcrypt iphlpapi)
elseif(APPLE)
    target_link_libraries(Perlin PRIVATE "-framework Security" "-framework CoreFoundation")
    target_compile_definitions(Perlin PRIVATE CPPHTTPLIB_USE_CERTS_FROM_MACOSX_KEYCHAIN)
else()
    target_link_libraries(Perlin PRIVATE dl)
    target_link_options(Perlin PRIVATE -static-libstdc++ -static-libgcc)
endif()

if(MSVC)
    target_compile_options(Perlin PRIVATE /W4 /permissive- /EHsc /Zc:__cplusplus)
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

include(CheckIPOSupported)
check_ipo_supported(RESULT PERLIN_IPO OUTPUT PERLIN_IPO_LOG)
if(PERLIN_IPO)
    set_property(TARGET Perlin PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELEASE ON)
endif()

install(TARGETS Perlin RUNTIME DESTINATION .)
if(MSVC)
    # Debug info ships next to the exe so crash stack traces in perlin.log resolve to function names.
    install(FILES "$<TARGET_PDB_FILE:Perlin>" DESTINATION . OPTIONAL)
    install(FILES "$<TARGET_FILE_DIR:Perlin>/Perlin.map" DESTINATION . OPTIONAL)
endif()
option(PERLIN_BUILD_TESTS "Build unit tests" OFF)
if(PERLIN_BUILD_TESTS)
    enable_testing()
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
        src/world/level/block/BlockPalette.cpp src/util/Resources.cpp)
    target_include_directories(texture_selftest PRIVATE src "${stb_SOURCE_DIR}")
    target_link_libraries(texture_selftest PRIVATE glaze::glaze miniz Threads::Threads)
    target_compile_features(texture_selftest PRIVATE cxx_std_23)
    if(PERLIN_TEXTURE_TEST_ASSETS)
        add_test(NAME texture_selftest COMMAND texture_selftest "${PERLIN_TEXTURE_TEST_ASSETS}")
    endif()

    add_executable(shaderpack_selftest tests/shaderpack_selftest.cpp src/client/resources/ShaderPack.cpp)
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
endif()
