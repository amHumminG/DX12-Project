cmake_minimum_required(VERSION 3.20)
project(DX12Sandbox)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)


set(DX12_LIBS d3d12.lib dxgi.lib dxguid.lib d3dcompiler.lib)

set(SOURCE_FILES
    src/main.cpp
    src/DX12.h
    src/Helpers.h
)
add_executable(${PROJECT_NAME} WIN32 ${SOURCE_FILES})

target_include_directories(${PROJECT_NAME}
    PRIVATE src
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/D3D12/include
)

target_link_libraries(${PROJECT_NAME} PRIVATE ${DX12_LIBS})

add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:${PROJECT_NAME}>/D3D12"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    "${CMAKE_CURRENT_SOURCE_DIR}/D3D12/x64"
    "$<TARGET_FILE_DIR:${PROJECT_NAME}>/D3D12"
    COMMENT "Copying Agility SDK DLLs to build directory..."
)