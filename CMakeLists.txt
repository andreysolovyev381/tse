cmake_minimum_required(VERSION 3.20)
project(tse_examples CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

if(DEFINED ENV{TSE_SDK_DIR})
    set(_tse_sdk_dir_default "$ENV{TSE_SDK_DIR}")
else()
    set(_tse_sdk_dir_default "${CMAKE_CURRENT_SOURCE_DIR}/sdk")
endif()
set(TSE_SDK_DIR "${_tse_sdk_dir_default}" CACHE PATH "Directory the SDK archive is unpacked into")

if(NOT EXISTS "${TSE_SDK_DIR}/python/tse_build_version.py")
    message(FATAL_ERROR "No SDK in ${TSE_SDK_DIR}: unpack the SDK archive there, or set TSE_SDK_DIR to the directory it is unpacked into")
endif()

file(STRINGS "${TSE_SDK_DIR}/python/tse_build_version.py" _tse_sdk_version_line REGEX "^LIB_VERSION = ")
string(REGEX REPLACE "^LIB_VERSION = \"(.+)\"$" "\\1" TSE_SDK_VERSION "${_tse_sdk_version_line}")

find_library(TSE_EXPORT_LIBRARY NAMES "tse_export.${TSE_SDK_VERSION}" PATHS "${TSE_SDK_DIR}/lib" NO_DEFAULT_PATH REQUIRED)
find_library(TSE_EXPORT_CPP_LIBRARY NAMES tse_export_cpp PATHS "${TSE_SDK_DIR}/lib" NO_DEFAULT_PATH REQUIRED)

if(DEFINED ENV{TSE_DATA_DIR})
    set(TSE_DATA "$ENV{TSE_DATA_DIR}")
else()
    set(TSE_DATA "${CMAKE_CURRENT_SOURCE_DIR}/data")
endif()

set(TSE_HELPERS "${CMAKE_CURRENT_SOURCE_DIR}/helpers/cpp")

option(TSE_EXAMPLES_XGBOOST "Fetch XGBoost and build the two examples that use it" ON)
set(TSE_XGBOOST_EXAMPLES gbm candidate_selection)

if(TSE_EXAMPLES_XGBOOST)
    include(FetchContent)

    set(XGBOOST_VERSION "v3.3.0")

    set(BUILD_STATIC_LIB    ON  CACHE BOOL "" FORCE)
    set(XGBOOST_BUILD_DOC   OFF CACHE BOOL "" FORCE)
    set(PLUGIN_LZ4          OFF CACHE BOOL "" FORCE)
    set(PLUGIN_DENSE_PARSER OFF CACHE BOOL "" FORCE)
    set(GOOGLE_TEST         OFF CACHE BOOL "" FORCE)

    FetchContent_Declare(
        xgboost
        GIT_REPOSITORY         https://github.com/dmlc/xgboost.git
        GIT_TAG                ${XGBOOST_VERSION}
        GIT_SHALLOW            TRUE
        GIT_SUBMODULES_RECURSE TRUE
    )
    FetchContent_MakeAvailable(xgboost)

    function(get_all_targets out_var dir)
        get_property(subdirs DIRECTORY "${dir}" PROPERTY SUBDIRECTORIES)
        foreach(subdir IN LISTS subdirs)
            get_all_targets(${out_var} "${subdir}")
        endforeach()
        get_property(targets DIRECTORY "${dir}" PROPERTY BUILDSYSTEM_TARGETS)
        set(${out_var} ${${out_var}} ${targets} PARENT_SCOPE)
    endfunction()

    get_all_targets(xgboost_targets ${xgboost_SOURCE_DIR})
    foreach(target IN LISTS xgboost_targets)
        get_target_property(target_type ${target} TYPE)
        if(target_type STREQUAL "STATIC_LIBRARY" OR
           target_type STREQUAL "SHARED_LIBRARY" OR
           target_type STREQUAL "MODULE_LIBRARY" OR
           target_type STREQUAL "OBJECT_LIBRARY" OR
           target_type STREQUAL "EXECUTABLE")
            target_compile_options(${target} PRIVATE
                $<$<CXX_COMPILER_ID:MSVC>:/WX->
                $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wno-error>
            )
        endif()
    endforeach()
endif()

file(GLOB TSE_EXAMPLE_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/[0-9][0-9] */cpp/*.cpp")

foreach(src IN LISTS TSE_EXAMPLE_SOURCES)
    get_filename_component(name "${src}" NAME_WE)
    if(name IN_LIST TSE_XGBOOST_EXAMPLES AND NOT TSE_EXAMPLES_XGBOOST)
        continue()
    endif()
    add_executable(${name} "${src}")
    target_include_directories(${name} PRIVATE
        "${TSE_SDK_DIR}/include" "${TSE_SDK_DIR}/include/tse" "${TSE_HELPERS}"
    )
    target_compile_definitions(${name} PRIVATE TSE_DATA_DIR="${TSE_DATA}")
    target_link_libraries(${name} PRIVATE "${TSE_EXPORT_CPP_LIBRARY}" "${TSE_EXPORT_LIBRARY}")
    set_target_properties(${name} PROPERTIES BUILD_RPATH "${TSE_SDK_DIR}/lib")
    if(name IN_LIST TSE_XGBOOST_EXAMPLES)
        target_include_directories(${name} PRIVATE "${xgboost_SOURCE_DIR}/include")
        target_link_libraries(${name} PRIVATE xgboost)
    endif()
endforeach()
