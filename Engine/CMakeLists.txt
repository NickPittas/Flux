# ***** BEGIN LICENSE BLOCK *****
# This file is part of Natron <https://natrongithub.github.io/>,
#
# Natron is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.
#
# Natron is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with Natron.  If not, see <http://www.gnu.org/licenses/gpl-2.0.html>
# ***** END LICENSE BLOCK *****

include(FindPkgConfig)

pkg_check_modules(Cairo REQUIRED IMPORTED_TARGET cairo fontconfig)
option(FLUX_ENABLE_CORRIDORKEY_TENSORRT "Enable optional native CorridorKey TensorRT/CUDA runtime support" OFF)
set(TENSORRT_ROOT "" CACHE PATH "TensorRT SDK root containing include/NvInfer.h and lib/libnvinfer.so")

if(FLUX_ENABLE_CORRIDORKEY_TENSORRT)
    set(CUDAToolkit_ROOT "" CACHE PATH "CUDA Toolkit root containing include/cuda_runtime_api.h and libcudart")
    if(CMAKE_VERSION VERSION_GREATER_EQUAL 3.17)
        find_package(CUDAToolkit QUIET)
    endif()

    set(_FLUX_CUDA_HINTS)
    if(CUDAToolkit_ROOT)
        list(APPEND _FLUX_CUDA_HINTS "${CUDAToolkit_ROOT}")
    endif()
    if(DEFINED ENV{CUDAToolkit_ROOT})
        list(APPEND _FLUX_CUDA_HINTS "$ENV{CUDAToolkit_ROOT}")
    endif()
    if(DEFINED ENV{CUDA_HOME})
        list(APPEND _FLUX_CUDA_HINTS "$ENV{CUDA_HOME}")
    endif()
    if(DEFINED ENV{CUDA_PATH})
        list(APPEND _FLUX_CUDA_HINTS "$ENV{CUDA_PATH}")
    endif()

    find_path(FLUX_CUDA_INCLUDE_DIR
        NAMES cuda_runtime_api.h
        HINTS ${_FLUX_CUDA_HINTS}
        PATH_SUFFIXES include targets/x86_64-linux/include
        DOC "CUDA Toolkit include directory containing cuda_runtime_api.h")
    if(NOT FLUX_CUDA_INCLUDE_DIR)
        message(FATAL_ERROR
            "FLUX_ENABLE_CORRIDORKEY_TENSORRT=ON requires CUDA Toolkit development headers.\n"
            "Missing: cuda_runtime_api.h.\n"
            "Install the CUDA Toolkit and configure with -DCUDAToolkit_ROOT=<cuda-prefix> when it is not in a standard location.\n"
            "Current detection:\n"
            "  CUDAToolkit_ROOT='${CUDAToolkit_ROOT}'\n"
            "  FLUX_CUDA_INCLUDE_DIR='${FLUX_CUDA_INCLUDE_DIR}'")
    endif()
    set(FLUX_CUDA_INCLUDE_DIRS ${FLUX_CUDA_INCLUDE_DIR})

    set(_FLUX_TENSORRT_HINTS)
    if(TENSORRT_ROOT)
        list(APPEND _FLUX_TENSORRT_HINTS "${TENSORRT_ROOT}")
    endif()
    if(TensorRT_ROOT)
        list(APPEND _FLUX_TENSORRT_HINTS "${TensorRT_ROOT}")
    endif()
    if(DEFINED ENV{TENSORRT_ROOT})
        list(APPEND _FLUX_TENSORRT_HINTS "$ENV{TENSORRT_ROOT}")
    endif()
    if(DEFINED ENV{TensorRT_ROOT})
        list(APPEND _FLUX_TENSORRT_HINTS "$ENV{TensorRT_ROOT}")
    endif()

    set(_FLUX_TENSORRT_INCLUDE_SUFFIXES include include/x86_64-linux-gnu)
    set(_FLUX_TENSORRT_LIBRARY_SUFFIXES lib lib64 lib/x86_64-linux-gnu)

    find_path(TENSORRT_INCLUDE_DIR
        NAMES NvInfer.h
        HINTS ${_FLUX_TENSORRT_HINTS}
        PATH_SUFFIXES ${_FLUX_TENSORRT_INCLUDE_SUFFIXES}
        DOC "TensorRT include directory containing NvInfer.h")

    find_library(TENSORRT_NVINFER_LIBRARY
        NAMES nvinfer
        HINTS ${_FLUX_TENSORRT_HINTS}
        PATH_SUFFIXES ${_FLUX_TENSORRT_LIBRARY_SUFFIXES}
        DOC "TensorRT core runtime library")

    find_library(TENSORRT_NVINFER_PLUGIN_LIBRARY
        NAMES nvinfer_plugin
        HINTS ${_FLUX_TENSORRT_HINTS}
        PATH_SUFFIXES ${_FLUX_TENSORRT_LIBRARY_SUFFIXES}
        DOC "TensorRT plugin runtime library")

    if(NOT TENSORRT_INCLUDE_DIR)
        message(FATAL_ERROR
            "FLUX_ENABLE_CORRIDORKEY_TENSORRT=ON requires TensorRT development headers.\n"
            "Missing: NvInfer.h.\n"
            "Install NVIDIA TensorRT development packages or unpack the TensorRT tarball and configure Flux with -DTENSORRT_ROOT=<TensorRT-prefix>.\n"
            "Expected layout:\n"
            "  <TensorRT-prefix>/include/NvInfer.h\n"
            "Current detection:\n"
            "  TENSORRT_ROOT='${TENSORRT_ROOT}'\n"
            "  TENSORRT_INCLUDE_DIR='${TENSORRT_INCLUDE_DIR}'")
    endif()

    if(EXISTS "${TENSORRT_INCLUDE_DIR}/NvInferVersion.h")
        set(_FLUX_TENSORRT_VERSION_FILE "${TENSORRT_INCLUDE_DIR}/NvInferVersion.h")
    else()
        set(_FLUX_TENSORRT_VERSION_FILE "${TENSORRT_INCLUDE_DIR}/NvInfer.h")
    endif()
    file(READ "${_FLUX_TENSORRT_VERSION_FILE}" _FLUX_TENSORRT_VERSION_HEADER)
    string(REGEX MATCH "#define[ \t]+NV_TENSORRT_MAJOR[ \t]+([0-9]+)" _FLUX_TENSORRT_MAJOR_MATCH "${_FLUX_TENSORRT_VERSION_HEADER}")
    set(_FLUX_TENSORRT_VERSION_MAJOR "${CMAKE_MATCH_1}")
    string(REGEX MATCH "#define[ \t]+NV_TENSORRT_MINOR[ \t]+([0-9]+)" _FLUX_TENSORRT_MINOR_MATCH "${_FLUX_TENSORRT_VERSION_HEADER}")
    set(_FLUX_TENSORRT_VERSION_MINOR "${CMAKE_MATCH_1}")
    string(REGEX MATCH "#define[ \t]+NV_TENSORRT_PATCH[ \t]+([0-9]+)" _FLUX_TENSORRT_PATCH_MATCH "${_FLUX_TENSORRT_VERSION_HEADER}")
    set(_FLUX_TENSORRT_VERSION_PATCH "${CMAKE_MATCH_1}")
    if(NOT _FLUX_TENSORRT_VERSION_MAJOR)
        string(REGEX MATCH "#define[ \t]+TRT_MAJOR_ENTERPRISE[ \t]+([0-9]+)" _FLUX_TENSORRT_MAJOR_ENTERPRISE_MATCH "${_FLUX_TENSORRT_VERSION_HEADER}")
        set(_FLUX_TENSORRT_VERSION_MAJOR "${CMAKE_MATCH_1}")
        string(REGEX MATCH "#define[ \t]+TRT_MINOR_ENTERPRISE[ \t]+([0-9]+)" _FLUX_TENSORRT_MINOR_ENTERPRISE_MATCH "${_FLUX_TENSORRT_VERSION_HEADER}")
        set(_FLUX_TENSORRT_VERSION_MINOR "${CMAKE_MATCH_1}")
        string(REGEX MATCH "#define[ \t]+TRT_PATCH_ENTERPRISE[ \t]+([0-9]+)" _FLUX_TENSORRT_PATCH_ENTERPRISE_MATCH "${_FLUX_TENSORRT_VERSION_HEADER}")
        set(_FLUX_TENSORRT_VERSION_PATCH "${CMAKE_MATCH_1}")
    endif()
    if(NOT _FLUX_TENSORRT_VERSION_MAJOR)
        message(FATAL_ERROR "FLUX_ENABLE_CORRIDORKEY_TENSORRT=ON requires TensorRT 8.6+; could not parse TensorRT version from ${_FLUX_TENSORRT_VERSION_FILE}.")
    endif()
    if(_FLUX_TENSORRT_VERSION_MAJOR LESS 8 OR (_FLUX_TENSORRT_VERSION_MAJOR EQUAL 8 AND _FLUX_TENSORRT_VERSION_MINOR LESS 6))
        message(FATAL_ERROR
            "FLUX_ENABLE_CORRIDORKEY_TENSORRT=ON requires TensorRT 8.6+ for name-based tensor APIs; "
            "found TensorRT ${_FLUX_TENSORRT_VERSION_MAJOR}.${_FLUX_TENSORRT_VERSION_MINOR}.${_FLUX_TENSORRT_VERSION_PATCH}.")
    endif()
    message(STATUS "Found TensorRT ${_FLUX_TENSORRT_VERSION_MAJOR}.${_FLUX_TENSORRT_VERSION_MINOR}.${_FLUX_TENSORRT_VERSION_PATCH}: headers=${TENSORRT_INCLUDE_DIR}")
    if(TENSORRT_NVINFER_LIBRARY)
        message(STATUS "Found TensorRT core library for developer diagnostics only: ${TENSORRT_NVINFER_LIBRARY}")
    endif()
    if(TENSORRT_NVINFER_PLUGIN_LIBRARY)
        message(STATUS "Found TensorRT plugin library for developer diagnostics only: ${TENSORRT_NVINFER_PLUGIN_LIBRARY}")
    endif()

    mark_as_advanced(FLUX_CUDA_INCLUDE_DIR FLUX_CUDA_CUDART_LIBRARY TENSORRT_INCLUDE_DIR TENSORRT_NVINFER_LIBRARY TENSORRT_NVINFER_PLUGIN_LIBRARY)
endif()


file(GLOB NatronEngine_HEADERS *.h)
file(GLOB NatronEngine_SOURCES *.cpp)

set(PYENGINE_OUT ${CMAKE_CURRENT_BINARY_DIR}/Qt${QT_VERSION_MAJOR})
set(PYENGINE_INCLUDE_DIRS ${PYSIDE_INCLUDE_DIRS} ${PYSIDE_INCLUDE_DIRS}/QtCore)
list(APPEND PYENGINE_INCLUDE_DIRS ${QTCORE_INCLUDE_DIRS} ${Python3_INCLUDE_DIRS} ${Boost_INCLUDE_DIRS})
list(TRANSFORM PYENGINE_INCLUDE_DIRS PREPEND "-I" OUTPUT_VARIABLE PYENGINE_INCS)
set(PYENGINE_HEADER PySide${PYSIDE_VERSION_MAJOR}_Engine_Python.h)

set(shiboken_args
    "-std=c++17" "--enable-parent-ctor-heuristic" "--use-isnull-as-nb_nonzero"
    "--avoid-protected-hack" "--enable-pyside-extensions"
    "-I." "-I.." "-I../Global" "-I../libs/OpenFX/include" ${PYENGINE_INCS}
    "--clang-option=-DNATRON_BUILD_NUMBER=0"
    "--clang-option=-DNATRON_CUSTOM_BUILD_USER_TOKEN=Flux"
    "-T${PYSIDE_TYPESYSTEMS}" "--output-directory=${PYENGINE_OUT}"
    "${PYENGINE_HEADER}" "typesystem_engine.xml")

execute_process(COMMAND ${Python3_EXECUTABLE} ../tools/utils/sourceList.py typesystem_engine.xml ${PYENGINE_OUT}
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    OUTPUT_VARIABLE PyEngine_SOURCES)
set_property(SOURCE ${PyEngine_SOURCES} PROPERTY SKIP_AUTOGEN ON)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    set_property(SOURCE ${PyEngine_SOURCES} APPEND PROPERTY COMPILE_OPTIONS
        -Wno-cast-function-type
        -Wno-missing-declarations
    )
endif()
add_custom_command(OUTPUT ${PyEngine_SOURCES}
    COMMAND $<TARGET_FILE:${SHIBOKEN_EXE}> ${shiboken_args}
    COMMAND ${Python3_EXECUTABLE} ../tools/utils/sourceCleanup.py typesystem_engine.xml ${PYENGINE_OUT}
    DEPENDS ${SHIBOKEN_EXE} ${PYENGINE_HEADER} ${NatronEngine_HEADERS} ../tools/utils/sourceCleanup.py typesystem_engine.xml
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})

if(UNIX AND NOT APPLE)
    set(XDG_LIBS ${X11_LIBRARIES})
    if(Wayland_FOUND)
        set(XDG_LIBS Wayland::Client Wayland::Egl ${XDG_LIBS})
        set(XDG_DEFS __NATRON_WAYLAND__)
    endif()
endif()


list(APPEND NatronEngine_SOURCES
    ${NatronEngine_SOURCES}
    ${PyEngine_SOURCES}
    ../Global/glad_source.c
    ../Global/FStreamsSupport.cpp
    ../Global/ProcInfo.cpp
    ../Global/PythonUtils.cpp
    ../Global/StrUtils.cpp
    ../libs/SequenceParsing/SequenceParsing.cpp
)
add_library(NatronEngine STATIC ${NatronEngine_HEADERS} ${NatronEngine_SOURCES})
target_link_libraries(NatronEngine
    PUBLIC
        HostSupport
        Boost::headers
        Boost::serialization
        PkgConfig::Cairo
    PRIVATE
        ${SHIBOKEN_LIB}
        ${PYSIDE_LIB}
        Qt::Core
        Qt::Concurrent
        Qt::Network
        Python3::Module
        ${XDG_LIBS}
        ceres
        hoedown
        mv
        openMVG
        tess
)

if(FLUX_ENABLE_CORRIDORKEY_TENSORRT)
    target_include_directories(NatronEngine SYSTEM PRIVATE ${FLUX_CUDA_INCLUDE_DIRS} ${TENSORRT_INCLUDE_DIR})
    target_link_libraries(NatronEngine PRIVATE dl)
    target_compile_definitions(NatronEngine PRIVATE FLUX_ENABLE_CORRIDORKEY_TENSORRT=1)
endif()

if(WIN32)
    #System library is required on windows to map network share names from drive letters
    target_link_libraries(NatronEngine PRIVATE mpr)
endif()

target_include_directories(NatronEngine
    PUBLIC
        ../Global/glad$<IF:$<CONFIG:Debug>,Deb,Rel>/include
    PRIVATE
        ${PYSIDE_INCLUDE_DIRS}/QtCore
        .
        ..
        ../Global
        ../libs/SequenceParsing
        ${X11_INCLUDE_DIR}
)
target_compile_definitions(NatronEngine
    PRIVATE
        NATRON_CUSTOM_BUILD_USER_TOKEN="${BUILD_USER_NAME}"
        NATRON_BUILD_NUMBER=0
        QT_NO_KEYWORDS
        ${XDG_DEFS}
)
