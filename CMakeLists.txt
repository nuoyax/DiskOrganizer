cmake_minimum_required(VERSION 3.21)

project(DiskOrganizer
    VERSION 1.0.0
    DESCRIPTION "Disk Organizer - 磁盘整理与清理工具"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Qt6 静态编译
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)
set(QT_DISABLE_DEPRECATED_UP_TO 0x060500)

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Widgets Concurrent)

# Win8+ (NT 6.2) 最低版本
if(WIN32)
    add_compile_definitions(
        _WIN32_WINNT=0x0602
        WINVER=0x0602
        UNICODE _UNICODE
    )
    if(MSVC)
        add_compile_options(/MP /W3 /permissive-)
        # 本机 Qt 静态库以 /MD(runtime MD_DynamicRelease, _MSC_VER=1900) 构建，必须匹配
        set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreadedDLL$<$<CONFIG:Debug>:Debug>")
    endif()
endif()

add_subdirectory(src)
enable_testing()
add_subdirectory(tests)
