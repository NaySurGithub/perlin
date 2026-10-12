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

if(WIN32 AND NOT OPENSSL_ROOT_DIR)
    foreach(root "$ENV{VCPKG_INSTALLATION_ROOT}" "$ENV{VCPKG_ROOT}" "C:/Users/judah/vcpkg")
        if(root AND EXISTS "${root}/installed/x64-windows/include/openssl")
            set(OPENSSL_ROOT_DIR "${root}/installed/x64-windows" CACHE PATH "OpenSSL root")
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

if(APPLE)
    set(OPENSSL_USE_STATIC_LIBS ON)
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
    vs_sunrise fs_sunrise vs_celestial fs_celestial vs_cloud fs_cloud)

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

file(GLOB_RECURSE PERLIN_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")
list(FILTER PERLIN_SOURCES EXCLUDE REGEX "/src/network/(Nbt\\.cpp|custom/)")

if(WIN32)
    add_executable(Perlin WIN32 ${PERLIN_SOURCES} ${PERLIN_SHADER_OUTPUTS})
elseif(APPLE)
    add_executable(Perlin ${PERLIN_SOURCES} ${PERLIN_SHADER_OUTPUTS})
else()
    add_executable(Perlin ${PERLIN_SOURCES} ${PERLIN_SHADER_OUTPUTS})
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
    target_link_libraries(Perlin PRIVATE ws2_32 crypt32)
elseif(APPLE)
    target_link_libraries(Perlin PRIVATE "-framework Security" "-framework CoreFoundation")
    target_compile_definitions(Perlin PRIVATE CPPHTTPLIB_USE_CERTS_FROM_MACOSX_KEYCHAIN)
else()
    target_link_libraries(Perlin PRIVATE dl)
endif()

if(MSVC)
    target_compile_options(Perlin PRIVATE /W4 /permissive- /EHsc /Zc:__cplusplus)
    target_link_options(Perlin PRIVATE /DEBUG)
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

add_custom_command(TARGET Perlin POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/resources" "$<TARGET_FILE_DIR:Perlin>/resources"
    VERBATIM)

include(CheckIPOSupported)
check_ipo_supported(RESULT PERLIN_IPO OUTPUT PERLIN_IPO_LOG)
if(PERLIN_IPO)
    set_property(TARGET Perlin PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELEASE ON)
endif()

if(WIN32)
    foreach(dll libssl-3-x64.dll libcrypto-3-x64.dll)
        if(OPENSSL_ROOT_DIR AND EXISTS "${OPENSSL_ROOT_DIR}/bin/${dll}")
            add_custom_command(TARGET Perlin POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${OPENSSL_ROOT_DIR}/bin/${dll}" "$<TARGET_FILE_DIR:Perlin>"
                VERBATIM)
        endif()
    endforeach()
endif()

install(TARGETS Perlin RUNTIME DESTINATION .)
install(DIRECTORY resources DESTINATION .)
if(WIN32 AND OPENSSL_ROOT_DIR)
    foreach(dll libssl-3-x64.dll libcrypto-3-x64.dll)
        if(EXISTS "${OPENSSL_ROOT_DIR}/bin/${dll}")
            install(FILES "${OPENSSL_ROOT_DIR}/bin/${dll}" DESTINATION .)
        endif()
    endforeach()
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

    add_executable(light_selftest
        tests/light_selftest.cpp
        src/world/level/lighting/ChunkLighting.cpp
        src/world/level/lighting/LightRegistry.cpp
        src/util/Json.cpp
        src/world/level/Level.cpp
        src/world/level/block/BlockPalette.cpp)
    target_include_directories(light_selftest PRIVATE src)
    target_link_libraries(light_selftest PRIVATE glaze::glaze Threads::Threads)
    target_compile_features(light_selftest PRIVATE cxx_std_23)
    add_test(NAME light_selftest COMMAND light_selftest)
endif()
