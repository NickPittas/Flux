cmake_minimum_required(VERSION 3.16)

project(FluxOpenFX LANGUAGES CXX)

if(${CMAKE_SOURCE_DIR} STREQUAL ${CMAKE_BINARY_DIR})
    message(FATAL_ERROR "In-tree builds are not supported. Use a separate build directory.")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(FindOpenGL REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(FLUX_TEXT_DEPS REQUIRED IMPORTED_TARGET freetype2 fontconfig harfbuzz)

set(OPENFX_MISC_PATH "${CMAKE_CURRENT_SOURCE_DIR}/../openfx-misc" CACHE PATH "Path to openfx-misc checkout")
set(OPENFX_PATH "${OPENFX_MISC_PATH}/openfx")

if(NOT EXISTS "${OPENFX_PATH}/include")
    message(FATAL_ERROR "Missing OpenFX support headers. Expected ${OPENFX_PATH}/include")
endif()

set(OFX_SUPPORT_HEADER_DIR "${OPENFX_PATH}/Support/include")
set(OFX_SUPPORT_LIBRARY_DIR "${OPENFX_PATH}/Support/Library")
set(OFX_SUPPORTEXT_DIR "${OPENFX_MISC_PATH}/SupportExt")

include_directories(
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${OFX_SUPPORTEXT_DIR}"
    "${OPENFX_PATH}/include"
    "${OFX_SUPPORT_HEADER_DIR}"
    "${OPENFX_PATH}/Support/Plugins/include"
)

set(OFX_SUPPORT_SOURCES
    "${OFX_SUPPORT_LIBRARY_DIR}/ofxsCore.cpp"
    "${OFX_SUPPORT_LIBRARY_DIR}/ofxsImageEffect.cpp"
    "${OFX_SUPPORT_LIBRARY_DIR}/ofxsInteract.cpp"
    "${OFX_SUPPORT_LIBRARY_DIR}/ofxsLog.cpp"
    "${OFX_SUPPORT_LIBRARY_DIR}/ofxsMultiThread.cpp"
    "${OFX_SUPPORT_LIBRARY_DIR}/ofxsParams.cpp"
    "${OFX_SUPPORT_LIBRARY_DIR}/ofxsProperty.cpp"
    "${OFX_SUPPORT_LIBRARY_DIR}/ofxsPropertyValidation.cpp"
)

set(FLUX_SUPPORTEXT_SOURCES
    "${OFX_SUPPORTEXT_DIR}/ofxsGenerator.cpp"
    "${OFX_SUPPORTEXT_DIR}/ofxsRectangleInteract.cpp"
)

add_library(FluxTextRender SHARED
    TextRender/TextRender.cpp
    TextRender/TextRasterizer.cpp
    ${OFX_SUPPORT_SOURCES}
    ${FLUX_SUPPORTEXT_SOURCES}
)

set_target_properties(FluxTextRender PROPERTIES
    PREFIX ""
    SUFFIX ".ofx"
)

if(APPLE)
    set_target_properties(FluxTextRender PROPERTIES LINK_FLAGS "-fvisibility=hidden -exported_symbols_list,${OFX_SUPPORT_HEADER_DIR}/osxSymbols")
    set_target_properties(FluxTextRender PROPERTIES INSTALL_RPATH "@loader_path/../Frameworks;@loader_path/../Libraries")
elseif(MINGW)
    set_target_properties(FluxTextRender PROPERTIES LINK_FLAGS "-shared -fvisibility=hidden -Xlinker --version-script=${OFX_SUPPORT_HEADER_DIR}/linuxSymbols")
elseif(${CMAKE_SYSTEM_NAME} STREQUAL "FreeBSD" OR ${CMAKE_SYSTEM_NAME} STREQUAL "Linux")
    set_target_properties(FluxTextRender PROPERTIES LINK_FLAGS "-Wl,-fvisibility=hidden,--version-script=${OFX_SUPPORT_HEADER_DIR}/linuxSymbols")
    set_target_properties(FluxTextRender PROPERTIES INSTALL_RPATH "$ORIGIN/../../Libraries")
endif()

target_compile_definitions(FluxTextRender PRIVATE
    OFX_EXTENSIONS_VEGAS
    OFX_EXTENSIONS_NUKE
    OFX_EXTENSIONS_NATRON
    OFX_EXTENSIONS_TUTTLE
    OFX_SUPPORTS_OPENGLRENDER
    NOMINMAX
)

target_link_libraries(FluxTextRender ${OPENGL_gl_LIBRARY} PkgConfig::FLUX_TEXT_DEPS)

set(OFX_ARCH UNKNOWN)
if(MINGW)
    set(OFX_ARCH "Win32")
    if(${CMAKE_SYSTEM_PROCESSOR} STREQUAL "x86_64")
        set(OFX_ARCH "Win64")
    endif()
elseif(MSVC)
    set(OFX_ARCH "Win32")
    if((${CMAKE_SYSTEM_PROCESSOR} STREQUAL "IA64") OR (${CMAKE_SYSTEM_PROCESSOR} STREQUAL "AMD64"))
        set(OFX_ARCH "Win64")
    endif()
elseif(${CMAKE_SYSTEM_NAME} STREQUAL "FreeBSD")
    set(OFX_ARCH "FreeBSD-x86")
    if(${CMAKE_SYSTEM_PROCESSOR} STREQUAL "x86_64")
        set(OFX_ARCH "FreeBSD-x86-64")
    endif()
elseif(${CMAKE_SYSTEM_NAME} STREQUAL "Darwin")
    set(OFX_ARCH "MacOS")
elseif(${CMAKE_SYSTEM_NAME} STREQUAL "Linux")
    set(OFX_ARCH "Linux-x86")
    if(${CMAKE_SYSTEM_PROCESSOR} STREQUAL "x86_64")
        set(OFX_ARCH "Linux-x86-64")
    endif()
endif()

set(OFX_ARCH_NAME ${OFX_ARCH} CACHE STRING "OpenFX target OS and architecture")
if("${OFX_ARCH_NAME}" STREQUAL "UNKNOWN")
    message(SEND_ERROR "OFX_ARCH_NAME was not detected. Install targets are unavailable.")
else()
    install(TARGETS FluxTextRender
        RUNTIME DESTINATION FluxTextRender.ofx.bundle/Contents/${OFX_ARCH_NAME}
        LIBRARY DESTINATION FluxTextRender.ofx.bundle/Contents/${OFX_ARCH_NAME}
    )
    install(FILES TextRender/Info.plist
        DESTINATION FluxTextRender.ofx.bundle/Contents
    )
endif()
