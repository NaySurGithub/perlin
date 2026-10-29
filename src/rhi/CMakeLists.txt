# backend-neutral rhi; usable on its own (cmake -S src/rhi) or from the root build
cmake_minimum_required(VERSION 3.28)
if(NOT TARGET perlin_rhi)
    if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
        project(perlin_rhi LANGUAGES C CXX)
        set(CMAKE_CXX_STANDARD 23)
        set(CMAKE_CXX_STANDARD_REQUIRED ON)
        set(CMAKE_CXX_EXTENSIONS OFF)
        set(PERLIN_RHI_STANDALONE ON)
    endif()

    option(PERLIN_RHI_VULKAN "build the vulkan backend" ON)
    option(PERLIN_RHI_D3D12 "build the direct3d 12 backend" ${WIN32})
    option(PERLIN_RHI_D3D11 "build the direct3d 11 backend" OFF)
    option(PERLIN_RHI_METAL "build the metal backend" OFF)
    option(PERLIN_RHI_WEBGPU "build the webgpu backend" OFF)
    # headers only: the streamline dlls are loaded at run time and shipped beside the executable
    option(PERLIN_RHI_STREAMLINE "build the dlss / reflex / frame generation layer" ${WIN32})
    # offline builds point these at checkouts; otherwise the pinned revisions are fetched
    set(PERLIN_RHI_DEPS_DIR "" CACHE PATH "directory holding Vulkan-Headers, volk, vma, d3d12ma, DirectX-Headers checkouts")

    set(PERLIN_RHI_SOURCES
        Common.cpp
        ShaderLibrary.cpp
        DeviceProfile.cpp
        vendor/Vendor.cpp
        common/StateValidator.cpp
        null/NullDevice.cpp)

    include(FetchContent)
    function(perlin_rhi_dep name dir repo tag)
        if(PERLIN_RHI_DEPS_DIR AND EXISTS "${PERLIN_RHI_DEPS_DIR}/${dir}")
            set(FETCHCONTENT_SOURCE_DIR_${name} "${PERLIN_RHI_DEPS_DIR}/${dir}" CACHE PATH "" FORCE)
        endif()
        FetchContent_Declare(${name} GIT_REPOSITORY ${repo} GIT_TAG ${tag} GIT_SHALLOW TRUE)
        FetchContent_GetProperties(${name})
        if(NOT ${name}_POPULATED)
            FetchContent_Populate(${name})
        endif()
        set(${name}_SOURCE_DIR "${${name}_SOURCE_DIR}" PARENT_SCOPE)
    endfunction()

    if(PERLIN_RHI_VULKAN)
        perlin_rhi_dep(vulkan_headers Vulkan-Headers https://github.com/KhronosGroup/Vulkan-Headers.git v1.4.366)
        perlin_rhi_dep(volk volk https://github.com/zeux/volk.git b7764bc7f5880139859a15d1609d2ee0c6251608)
        perlin_rhi_dep(vma vma https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator.git v3.5.0)
        list(APPEND PERLIN_RHI_SOURCES
            vulkan/VulkanInstance.cpp
            vulkan/VulkanDevice.cpp
            vulkan/VulkanCommands.cpp
            vulkan/VulkanSwapchain.cpp
            vulkan/VmaImpl.cpp
            "${volk_SOURCE_DIR}/volk.c")
    endif()

    if(PERLIN_RHI_D3D12)
        perlin_rhi_dep(directx_headers DirectX-Headers https://github.com/microsoft/DirectX-Headers.git faf928dc6feb8b86e9e3d6a39fc5171de7d8ac36)
        perlin_rhi_dep(d3d12ma d3d12ma https://github.com/GPUOpen-LibrariesAndSDKs/D3D12MemoryAllocator.git v3.2.1)
        list(APPEND PERLIN_RHI_SOURCES
            d3d12/D3D12Instance.cpp
            d3d12/D3D12Device.cpp
            d3d12/D3D12Commands.cpp
            d3d12/D3D12Swapchain.cpp
            d3d12/D3D12MaImpl.cpp)
        # toolchains without declspec(uuid) (mingw, clang) take interface ids from this
        if(NOT MSVC)
            list(APPEND PERLIN_RHI_SOURCES "${directx_headers_SOURCE_DIR}/src/dxguids.cpp")
        endif()
    endif()

    if(PERLIN_RHI_STREAMLINE AND WIN32)
        perlin_rhi_dep(streamline Streamline https://github.com/NVIDIAGameWorks/Streamline.git 625db07d4bca5abc67d781c42af7e7fb0761f1b6)
        list(APPEND PERLIN_RHI_SOURCES vendor/Streamline.cpp)
    endif()

    add_library(perlin_rhi STATIC ${PERLIN_RHI_SOURCES})
    target_include_directories(perlin_rhi PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/..")
    target_compile_features(perlin_rhi PUBLIC cxx_std_23)

    if(PERLIN_RHI_STREAMLINE AND WIN32)
        target_compile_definitions(perlin_rhi PUBLIC RHI_HAS_STREAMLINE=1)
        target_include_directories(perlin_rhi PRIVATE "${streamline_SOURCE_DIR}/include")
        if(NOT MSVC)
            target_include_directories(perlin_rhi PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/vendor/compat")
            # the sdk header expects std::to_underlying whenever it sees c++23, which not every standard library has yet
            set_source_files_properties(vendor/Streamline.cpp PROPERTIES COMPILE_OPTIONS "-std=c++20")
        endif()
        if(PERLIN_RHI_VULKAN)
            target_include_directories(perlin_rhi PRIVATE "${vulkan_headers_SOURCE_DIR}/include")
        endif()
    endif()

    foreach(api VULKAN D3D12 D3D11 METAL WEBGPU)
        if(PERLIN_RHI_${api})
            target_compile_definitions(perlin_rhi PUBLIC RHI_HAS_${api}=1)
        endif()
    endforeach()

    if(PERLIN_RHI_VULKAN)
        target_include_directories(perlin_rhi PRIVATE
            "${vulkan_headers_SOURCE_DIR}/include" "${volk_SOURCE_DIR}" "${vma_SOURCE_DIR}/include")
        # nothing links the loader: volk finds it at run time and the rhi reports "no vulkan" when it is missing
        target_compile_definitions(perlin_rhi PRIVATE VK_NO_PROTOTYPES)
        if(UNIX AND NOT APPLE)
            target_link_libraries(perlin_rhi PUBLIC ${CMAKE_DL_LIBS})
        endif()
    endif()

    if(PERLIN_RHI_D3D12)
        # directx/ must come first: it supplies d3d12.h for toolchains whose own headers are older or missing
        target_include_directories(perlin_rhi PRIVATE
            "${directx_headers_SOURCE_DIR}/include" "${directx_headers_SOURCE_DIR}/include/directx"
            "${d3d12ma_SOURCE_DIR}/include" "${d3d12ma_SOURCE_DIR}/src")
    endif()

    if(PERLIN_RHI_STANDALONE)
        enable_testing()
        add_executable(rhi_null_selftest "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/rhi_null_selftest.cpp")
        target_link_libraries(rhi_null_selftest PRIVATE perlin_rhi)
        add_test(NAME rhi_null_selftest COMMAND rhi_null_selftest)
        add_executable(rhi_profile_test "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/rhi_profile_test.cpp")
        target_link_libraries(rhi_profile_test PRIVATE perlin_rhi)
        add_test(NAME rhi_profile_test COMMAND rhi_profile_test)
        add_executable(rhi_vendor_test "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/rhi_vendor_test.cpp")
        target_link_libraries(rhi_vendor_test PRIVATE perlin_rhi)
        add_test(NAME rhi_vendor_test COMMAND rhi_vendor_test)
        find_program(PERLIN_SLANGC slangc HINTS "${PERLIN_RHI_DEPS_DIR}/slang/bin")
        find_package(Python3 COMPONENTS Interpreter)
        if(PERLIN_SLANGC AND Python3_FOUND)
            set(shader_root "${CMAKE_CURRENT_SOURCE_DIR}/../shaders/slang")
            file(GLOB shader_inputs CONFIGURE_DEPENDS "${shader_root}/*.slang" "${shader_root}/programs/*.slang")
            set(dxc_libs)
            if(PERLIN_RHI_DEPS_DIR AND EXISTS "${PERLIN_RHI_DEPS_DIR}/dxc/lib")
                set(dxc_libs --library-path "${PERLIN_RHI_DEPS_DIR}/dxc/lib")
            endif()
            add_custom_command(OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/test_shaders.pack"
                COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/../../tools/pack_shaders.py"
                        --slangc "${PERLIN_SLANGC}" -I "${shader_root}" ${dxc_libs} --slots "${shader_root}/texture_slots.json"
                        -o "${CMAKE_CURRENT_BINARY_DIR}/test_shaders.pack" "${shader_root}/programs"
                DEPENDS ${shader_inputs} "${CMAKE_CURRENT_SOURCE_DIR}/../../tools/pack_shaders.py" VERBATIM)
            add_custom_target(rhi_shader_pack DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/test_shaders.pack")
            add_executable(rhi_shaderlib_test "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/rhi_shaderlib_test.cpp")
            target_link_libraries(rhi_shaderlib_test PRIVATE perlin_rhi)
            add_dependencies(rhi_shaderlib_test rhi_shader_pack)
            add_test(NAME rhi_shaderlib_test COMMAND rhi_shaderlib_test "${CMAKE_CURRENT_BINARY_DIR}/test_shaders.pack")
            add_executable(rhi_pipeline_smoke "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/rhi_pipeline_smoke.cpp")
            target_link_libraries(rhi_pipeline_smoke PRIVATE perlin_rhi)
            add_dependencies(rhi_pipeline_smoke rhi_shader_pack)
            if(PERLIN_RHI_VULKAN)
                add_test(NAME rhi_pipeline_smoke_vulkan COMMAND rhi_pipeline_smoke "${CMAKE_CURRENT_BINARY_DIR}/test_shaders.pack" vulkan)
                set_tests_properties(rhi_pipeline_smoke_vulkan PROPERTIES SKIP_RETURN_CODE 77)
            endif()
            if(PERLIN_RHI_D3D12)
                add_test(NAME rhi_pipeline_smoke_d3d12 COMMAND rhi_pipeline_smoke "${CMAKE_CURRENT_BINARY_DIR}/test_shaders.pack" d3d12)
                set_tests_properties(rhi_pipeline_smoke_d3d12 PROPERTIES SKIP_RETURN_CODE 77)
            endif()
        endif()
        if(PERLIN_RHI_VULKAN OR PERLIN_RHI_D3D12)
            add_executable(rhi_gpu_selftest "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/rhi_gpu_selftest.cpp")
            target_link_libraries(rhi_gpu_selftest PRIVATE perlin_rhi)
            # test shaders are slang like the game's; without slangc the gpu tests are not registered
            find_program(PERLIN_SLANGC slangc HINTS "${PERLIN_RHI_DEPS_DIR}/slang/bin")
            if(PERLIN_SLANGC)
                set(shader_src "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/rhi/shaders")
                set(shader_out "${CMAKE_CURRENT_BINARY_DIR}/test_shaders")
                set(shader_files)
                foreach(entry "raster.slang;vsMain;vertex;raster.vs" "raster.slang;fsMain;fragment;raster.fs" "cull.slang;csMain;compute;cull.cs")
                    list(GET entry 0 file)
                    list(GET entry 1 name)
                    list(GET entry 2 stage)
                    list(GET entry 3 out)
                    foreach(target spirv dxil)
                        set(ext spv)
                        set(extra)
                        if(target STREQUAL dxil)
                            set(ext dxil)
                            set(extra -profile sm_6_2)
                        endif()
                        add_custom_command(OUTPUT "${shader_out}/${out}.${ext}"
                            COMMAND ${CMAKE_COMMAND} -E make_directory "${shader_out}"
                            COMMAND "${PERLIN_SLANGC}" "${shader_src}/${file}" -target ${target} ${extra} -entry ${name} -stage ${stage}
                                    -o "${shader_out}/${out}.${ext}"
                            DEPENDS "${shader_src}/${file}" VERBATIM)
                        list(APPEND shader_files "${shader_out}/${out}.${ext}")
                    endforeach()
                endforeach()
                add_custom_target(rhi_test_shaders DEPENDS ${shader_files})
                add_dependencies(rhi_gpu_selftest rhi_test_shaders)
                if(PERLIN_RHI_VULKAN)
                    add_test(NAME rhi_gpu_selftest_vulkan COMMAND rhi_gpu_selftest "${shader_out}" vulkan)
                    set_tests_properties(rhi_gpu_selftest_vulkan PROPERTIES SKIP_RETURN_CODE 77)
                endif()
                if(PERLIN_RHI_D3D12)
                    add_test(NAME rhi_gpu_selftest_d3d12 COMMAND rhi_gpu_selftest "${shader_out}" d3d12)
                    set_tests_properties(rhi_gpu_selftest_d3d12 PROPERTIES SKIP_RETURN_CODE 77)
                endif()
            endif()
        endif()
    endif()
endif()
