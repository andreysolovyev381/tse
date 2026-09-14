cmake_minimum_required(VERSION 3.20)
project(tse_ml_examples CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

get_filename_component(TSE_ENGINE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../.." ABSOLUTE)

if(DEFINED ENV{TSE_SDK_CPP})
    set(_tse_sdk_cpp_default "$ENV{TSE_SDK_CPP}")
else()
    set(_tse_sdk_cpp_default "${TSE_ENGINE_ROOT}/src/library/export/cpp")
endif()
set(TSE_SDK_CPP "${_tse_sdk_cpp_default}" CACHE PATH "Directory containing the C++ wrapper tse.hpp / tse.cpp")

if(DEFINED ENV{TSE_SDK_INCLUDE})
    set(_tse_sdk_include_default "$ENV{TSE_SDK_INCLUDE}")
else()
    set(_tse_sdk_include_default "${TSE_ENGINE_ROOT}/src/library/export/include")
endif()
set(TSE_SDK_INCLUDE "${_tse_sdk_include_default}" CACHE PATH "Directory containing the C headers tse/*.h")

if(DEFINED ENV{TSE_EXPORT_LIB})
    get_filename_component(_tse_export_lib_dir_default "$ENV{TSE_EXPORT_LIB}" DIRECTORY)
else()
    set(_tse_export_lib_dir_default "${TSE_ENGINE_ROOT}/deployment/dist")
endif()
set(TSE_EXPORT_LIB_DIR "${_tse_export_lib_dir_default}" CACHE PATH "Directory containing libtse_export")

if(DEFINED ENV{TSE_DATA_DIR})
    set(TSE_DATA "$ENV{TSE_DATA_DIR}")
else()
    set(TSE_DATA "${CMAKE_CURRENT_SOURCE_DIR}/data")
endif()

set(TSE_HELPERS "${CMAKE_CURRENT_SOURCE_DIR}/helpers/cpp")

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

function(tse_ml_example name src)
    add_executable(${name} "${src}" "${TSE_SDK_CPP}/tse.cpp")
    target_include_directories(${name} PRIVATE
        "${TSE_SDK_CPP}" "${TSE_SDK_INCLUDE}" "${TSE_HELPERS}"
        "${xgboost_SOURCE_DIR}/include"
    )
    target_compile_definitions(${name} PRIVATE TSE_DATA_DIR="${TSE_DATA}")
    target_link_directories(${name} PRIVATE "${TSE_EXPORT_LIB_DIR}")
    target_link_libraries(${name} PRIVATE tse_export sqlite3 xgboost)
    set_target_properties(${name} PROPERTIES BUILD_RPATH "${TSE_EXPORT_LIB_DIR}")
endfunction()

tse_ml_example(gbm                 "${CMAKE_CURRENT_SOURCE_DIR}/08 robots - gbm model/cpp/gbm.cpp")
tse_ml_example(candidate_selection "${CMAKE_CURRENT_SOURCE_DIR}/23 stats - candidate selection/cpp/candidate_selection.cpp")
