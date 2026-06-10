cmake_minimum_required(VERSION 3.21)
project(fileprocessor VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Core: template engine, planner, executor. Pure C++, no Qt — builds and
# tests everywhere, the Qt UI is linked on top of it.
add_library(fp_core STATIC
    src/core/template/template.cpp
    src/core/template/textutil.cpp
)
target_include_directories(fp_core PUBLIC src)

if (MSVC)
    target_compile_options(fp_core PRIVATE /W4 /utf-8)
else()
    target_compile_options(fp_core PRIVATE -Wall -Wextra)
endif()

enable_testing()
add_executable(fp_core_tests tests/template_tests.cpp)
target_link_libraries(fp_core_tests PRIVATE fp_core)
add_test(NAME core COMMAND fp_core_tests)

# UI is optional so the core can be built/tested on machines without Qt.
find_package(Qt6 COMPONENTS Widgets QUIET)
if (Qt6_FOUND)
    set(CMAKE_AUTOMOC ON)
    add_executable(fileprocessor WIN32
        src/ui/main.cpp
        src/ui/mainwindow.cpp
        src/ui/mainwindow.h
    )
    target_link_libraries(fileprocessor PRIVATE fp_core Qt6::Widgets)
    if (MSVC)
        target_compile_options(fileprocessor PRIVATE /W4 /utf-8)
    endif()
else()
    message(STATUS "Qt6 not found — building fp_core and tests only")
endif()
