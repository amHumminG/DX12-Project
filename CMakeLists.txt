cmake_minimum_required(VERSION 3.20)
project(DX12Project)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(DX12_LIBS d3d12.lib dxgi.lib dxguid.lib d3dcompiler.lib)

file(GLOB_RECURSE SOURCE_FILES CONFIGURE_DEPENDS
    "src/*.cpp"
    "src/*.h"
)

# HLSL shader files
file(GLOB_RECURSE SHADER_FILES CONFIGURE_DEPENDS
    "src/Shaders/*.hlsl"
)

add_executable(${PROJECT_NAME} WIN32
    ${SOURCE_FILES}
    ${SHADER_FILES}
)

target_include_directories(${PROJECT_NAME}
    PRIVATE src
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/D3D12/include
)

target_link_libraries(${PROJECT_NAME} PRIVATE ${DX12_LIBS})

# -----------------------------------------------------------------------------
# Shader compilation
# -----------------------------------------------------------------------------

find_program(FXC_COMPILER fxc.exe REQUIRED)

set(COMPILED_SHADERS)

foreach(SHADER ${SHADER_FILES})

    get_filename_component(SHADER_FILE ${SHADER} NAME)
    get_filename_component(SHADER_NAME ${SHADER} NAME_WE)

    # Determine shader profile from filename
    if(SHADER_FILE MATCHES "\\.vs\\.hlsl$")
        set(SHADER_PROFILE vs_5_1)

    elseif(SHADER_FILE MATCHES "\\.ps\\.hlsl$")
        set(SHADER_PROFILE ps_5_1)

    elseif(SHADER_FILE MATCHES "\\.cs\\.hlsl$")
        set(SHADER_PROFILE cs_5_1)

    elseif(SHADER_FILE MATCHES "\\.gs\\.hlsl$")
        set(SHADER_PROFILE gs_5_1)

    elseif(SHADER_FILE MATCHES "\\.hs\\.hlsl$")
        set(SHADER_PROFILE hs_5_1)

    elseif(SHADER_FILE MATCHES "\\.ds\\.hlsl$")
        set(SHADER_PROFILE ds_5_1)

    else()
        message(WARNING
            "Unknown shader type for file: ${SHADER_FILE}"
        )
        continue()
    endif()

    set(OUTPUT_FILE
        ${CMAKE_CURRENT_BINARY_DIR}/${SHADER_NAME}.cso
    )

    add_custom_command(
        OUTPUT ${OUTPUT_FILE}
        COMMAND ${FXC_COMPILER}
            /T ${SHADER_PROFILE}
            /E main
            /Fo ${OUTPUT_FILE}
            /nologo
            ${SHADER}

        DEPENDS ${SHADER}
        COMMENT "Compiling HLSL shader: ${SHADER_NAME}"
    )

    list(APPEND COMPILED_SHADERS ${OUTPUT_FILE})

endforeach()

add_custom_target(Shaders ALL DEPENDS ${COMPILED_SHADERS})

add_dependencies(${PROJECT_NAME} Shaders)

# Copy compiled shaders to output directory
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${COMPILED_SHADERS}
        $<TARGET_FILE_DIR:${PROJECT_NAME}>
)

# -----------------------------------------------------------------------------
# Copy Agility SDK
# -----------------------------------------------------------------------------

add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory
        "$<TARGET_FILE_DIR:${PROJECT_NAME}>/D3D12"

    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/D3D12/x64"
        "$<TARGET_FILE_DIR:${PROJECT_NAME}>/D3D12"

    COMMENT "Copying Agility SDK DLLs to build directory..."
)