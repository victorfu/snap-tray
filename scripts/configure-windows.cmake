cmake_minimum_required(VERSION 3.16)

if(NOT DEFINED SNAPTRAY_BUILD_DIR OR NOT DEFINED SNAPTRAY_BUILD_TYPE OR "$ENV{QT_PATH}" STREQUAL "")
    message(FATAL_ERROR "Build directory, build type and QT_PATH are required")
endif()
get_filename_component(source_dir "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
get_filename_component(build_dir "${SNAPTRAY_BUILD_DIR}" ABSOLUTE)
get_filename_component(qt_prefix "$ENV{QT_PATH}" REALPATH BASE_DIR "${source_dir}")
set(qt_dir "${qt_prefix}/lib/cmake/Qt6")
if(NOT EXISTS "${qt_dir}/Qt6Config.cmake")
    message(FATAL_ERROR "QT_PATH does not contain a Qt6 CMake package: ${qt_prefix}")
endif()

function(cache_value key output)
    file(STRINGS "${build_dir}/CMakeCache.txt" entry LIMIT_COUNT 1 REGEX "^${key}:[^=]*=")
    string(REGEX REPLACE "^[^=]*=" "" value "${entry}")
    set(${output} "${value}" PARENT_SCOPE)
endfunction()

function(normalize_path input output)
    get_filename_component(normalized "${input}" REALPATH BASE_DIR "${source_dir}")
    file(TO_CMAKE_PATH "${normalized}" normalized)
    if(CMAKE_HOST_WIN32)
        string(TOLOWER "${normalized}" normalized)
    endif()
    set(${output} "${normalized}" PARENT_SCOPE)
endfunction()

set(pending "${build_dir}/.qt-deploy-pending")
set(needs_configure FALSE)
if(NOT EXISTS "${build_dir}/CMakeCache.txt" OR NOT EXISTS "${build_dir}/build.ninja" OR EXISTS "${pending}")
    set(needs_configure TRUE)
else()
    cache_value(Qt6_DIR cached_qt_dir)
    normalize_path("${qt_dir}" selected)
    normalize_path("${cached_qt_dir}" cached)
    cache_value(CMAKE_BUILD_TYPE cached_type)
    if(NOT selected STREQUAL cached OR NOT cached_type STREQUAL SNAPTRAY_BUILD_TYPE)
        message(STATUS "Qt installation or build type changed; reconfiguring ${build_dir}")
        set(needs_configure TRUE)
    endif()
    if(EXISTS "${build_dir}/CMakeFiles/rules.ninja")
        file(STRINGS "${build_dir}/CMakeFiles/rules.ninja" english_deps LIMIT_COUNT 1
            REGEX "^msvc_deps_prefix = Note: including file:")
        if(NOT english_deps)
            set(needs_configure TRUE)
        endif()
    endif()
endif()

if(needs_configure)
    file(MAKE_DIRECTORY "${build_dir}")
    # Keep this marker through configure/build/deployment failures. A retry must
    # regenerate partially updated caches and replace already-deployed Qt DLLs.
    file(WRITE "${pending}" "${qt_prefix}\n")
    execute_process(COMMAND "${CMAKE_COMMAND}" -S "${source_dir}" -B "${build_dir}" -G Ninja
        "-DCMAKE_BUILD_TYPE=${SNAPTRAY_BUILD_TYPE}"
        -U "Qt6*_DIR" -U "QT_DIR"
        "-DCMAKE_PREFIX_PATH=${qt_prefix}" "-DQt6_DIR:PATH=${qt_dir}"
        RESULT_VARIABLE result)
    if(NOT result STREQUAL "0")
        message(FATAL_ERROR "CMake configuration failed (${result}); build was not started")
    endif()
endif()
