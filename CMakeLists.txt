# TinyShell -- two ways to build it:
#
#   * as an ESP-IDF component: list the checkout in EXTRA_COMPONENT_DIRS (or
#     pull it with the component manager) and `REQUIRES TinyShell`. The
#     compile-time capacities and TINYSHELL_COLOR come from Kconfig
#     (menuconfig -> "TinyShell") instead of -D flags;
#   * as a plain CMake library (add_subdirectory / FetchContent), target
#     `tinyshell`, configured with the same TINYSHELL_* macros as -D flags.
#
# Both compile the same four sources; PlatformIO keeps using library.json.

set(tinyshell_sources
    "TinyShell.cpp"
    "ShellLineEditor.cpp"
    "ShellStyle.cpp"
    "TableLinker/TableLinker.cpp")

if(ESP_PLATFORM)
    idf_component_register(
        SRCS ${tinyshell_sources}
        INCLUDE_DIRS "." "TableLinker")

    # PUBLIC: the capacities size tables declared in the headers, so every
    # translation unit that includes TinyShell.h must see the same values.
    target_compile_definitions(${COMPONENT_LIB} PUBLIC
        TINYSHELL_MAX_MODULES=${CONFIG_TINYSHELL_MAX_MODULES}
        TINYSHELL_MAX_COMMANDS=${CONFIG_TINYSHELL_MAX_COMMANDS}
        TINYSHELL_MAX_ARGS=${CONFIG_TINYSHELL_MAX_ARGS}
        TINYSHELL_MAX_LINE=${CONFIG_TINYSHELL_MAX_LINE}
        TINYSHELL_OUTPUT_CAPACITY=${CONFIG_TINYSHELL_OUTPUT_CAPACITY})
    if(CONFIG_TINYSHELL_COLOR)
        target_compile_definitions(${COMPONENT_LIB} PUBLIC TINYSHELL_COLOR)
    endif()
    return()
endif()

cmake_minimum_required(VERSION 3.16)
project(tinyshell LANGUAGES CXX)

add_library(tinyshell ${tinyshell_sources})
target_include_directories(tinyshell PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/TableLinker)
target_compile_features(tinyshell PUBLIC cxx_std_17)
