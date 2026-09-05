cmake_minimum_required(VERSION 3.16)
project(SDL3Template LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ============================================================
# 通用编译选项（Debug / Release 均生效）
# ============================================================
add_compile_options(
    -fexec-charset=UTF-8
    -fdiagnostics-color=always
    -Wno-narrowing
    -Wno-unused-variable
    -Wno-unknown-pragmas
    -Wno-unused-parameter
    -Wno-conversion-null
    -Wno-missing-field-initializers
    -Wno-sign-compare
    -Wno-subobject-linkage
    -Wno-unused-function
    "-ffile-prefix-map=${CMAKE_CURRENT_SOURCE_DIR}=."
)

# 区分 Debug 构建与 Release 构建（选取合适的编译参数）
if(CMAKE_BUILD_TYPE STREQUAL "Release")
    message(STATUS "[CMake] 构建类型: Release")
    add_compile_options(-O2)
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -mwindows")
    add_definitions(-DNDEBUG)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/bin/release")

else()
    message(STATUS "[CMake] 构建类型: Debug（或未指定，默认按 Debug 处理）")
    add_compile_options(-g -O0 -Wall -Wextra)
    add_definitions(-DDEBUG)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/bin/debug")
endif()

# 静态链接运行时库，减少对系统 DLL 的依赖
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -static-libstdc++ -static-libgcc")

set(SDL_KIT_DIR "D:/AppInstallers/RecentlyC++Programs")

# 头文件搜索路径
include_directories(
    "D:/AppInstallers/RecentlyC++Programs"
    ${CMAKE_SOURCE_DIR}/include
    ${SDL_KIT_DIR}/SDL3-3.4.14/include
    ${SDL_KIT_DIR}/SDL3_ttf-3.2.2/include
    ${SDL_KIT_DIR}/SDL3_image-3.4.4/include
)

# 库文件搜索路径
link_directories(
    ${SDL_KIT_DIR}/SDL3-3.4.14/lib/x64
    ${SDL_KIT_DIR}/SDL3_ttf-3.2.2/lib/x64
    ${SDL_KIT_DIR}/SDL3_image-3.4.4/lib/x64
)

file(GLOB SRCS ${CMAKE_SOURCE_DIR}/src/*.cpp)

# 外部 C++ 源文件
set(EXT_SRCS
    
)

# 可执行文件图标：若 resource/icon.ico 存在，则通过 RC 资源嵌入为 exe 图标
set(EXE_ICON_SRC "")
if(EXISTS "${CMAKE_SOURCE_DIR}/resource/icon.ico")
    set(EXE_ICON_SRC "${CMAKE_CURRENT_BINARY_DIR}/SDL3Template_icon.rc.obj")
    add_custom_command(
        OUTPUT ${EXE_ICON_SRC}
        COMMAND ${CMAKE_RC_COMPILER} -O coff
                "${CMAKE_SOURCE_DIR}/resource/icon.rc"
                ${EXE_ICON_SRC}
        DEPENDS "${CMAKE_SOURCE_DIR}/resource/icon.rc"
                "${CMAKE_SOURCE_DIR}/resource/icon.ico"
        COMMENT "[CMake] 编译图标资源 icon.rc"
        VERBATIM
    )
    message(STATUS "[CMake] 检测到 resource/icon.ico，已设置为可执行文件图标")
else()
    message(STATUS "[CMake] 未检测到 resource/icon.ico，跳过 exe 图标设置")
endif()

add_executable(SDL3Template ${SRCS} ${EXT_SRCS} ${EXE_ICON_SRC})

# 输出文件名设为 main.exe（最终位于 ${CMAKE_SOURCE_DIR}/bin/{debug|release} 下）
set_target_properties(SDL3Template PROPERTIES OUTPUT_NAME "main")

# 链接库
# 注意：GNU ld 为单遍链接，被依赖的库（SDL2）必须放在依赖它的库之后，
# 否则 SDL2main/SDL2_ttf/SDL2_image 中引用的 SDL2 符号将无法解析。
target_link_libraries(SDL3Template
    SDL3_ttf
    SDL3_image
    SDL3
    imm32
    dwmapi
    comdlg32
    shell32
    comctl32
    ole32
    oleaut32
    uuid
    pdh
    psapi
    wbemuuid
    dxgi
    # ws2_32
    # user32
    # gdi32
    # msimg32
    # imm32
    # psapi
    # comdlg32
    # ole32
    # ksuser
)
