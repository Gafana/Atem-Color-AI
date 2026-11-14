cmake_minimum_required(VERSION 3.16)
project(AtemColorAI VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

option(ENABLE_GUI "Build Qt Widgets GUI shell (default: console only)" OFF)
option(ENABLE_GSTREAMER "Enable GStreamer frame capture module" ON)
set(ATEM_SDK_DIR "" CACHE PATH "Path to locally installed Blackmagic ATEM Switchers SDK")

# Qt6 preferred, Qt5 fallback
set(QT_MIN_VERSION 5.15)
find_package(Qt6 COMPONENTS Core Gui Widgets QUIET)
if(Qt6_FOUND)
    set(QT_VERSION_MAJOR 6)
    set(QT_LIBS Qt6::Core)
else()
    find_package(Qt5 ${QT_MIN_VERSION} REQUIRED COMPONENTS Core Gui Widgets)
    set(QT_VERSION_MAJOR 5)
    set(QT_LIBS Qt5::Core)
endif()

if(ENABLE_GUI)
    if(QT_VERSION_MAJOR EQUAL 6)
        list(APPEND QT_LIBS Qt6::Widgets)
    else()
        list(APPEND QT_LIBS Qt5::Widgets)
    endif()
endif()

find_package(ATEMSDK QUIET)
if(ATEMSDK_FOUND)
    message(STATUS "ATEM SDK found (${ATEMSDK_PLATFORM}): ${ATEMSDK_INCLUDE_DIR}")
else()
    message(STATUS "ATEM SDK not found — building with mock AtemClient (set -DATEM_SDK_DIR=...)")
endif()

set(HAVE_GSTREAMER OFF)
if(ENABLE_GSTREAMER)
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        pkg_check_modules(GSTREAMER QUIET gstreamer-1.0 gstreamer-app-1.0)
        if(GSTREAMER_FOUND)
            set(HAVE_GSTREAMER ON)
            message(STATUS "GStreamer found: ${GSTREAMER_VERSION}")
        endif()
    endif()
    if(NOT HAVE_GSTREAMER)
        message(STATUS "GStreamer not found — capture module disabled (install libgstreamer1.0-dev)")
    endif()
endif()

set(SOURCES
    src/main.cpp
    src/atem/AtemClient.h
    src/atem/AtemClientFactory.cpp
    src/atem/AtemClientMock.cpp
)

if(ATEMSDK_FOUND)
    list(APPEND SOURCES src/atem/AtemClientSdk.cpp)
    if(ATEMSDK_DISPATCH_CPP)
        list(APPEND SOURCES "${ATEMSDK_DISPATCH_CPP}")
        message(STATUS "ATEM SDK dispatch: ${ATEMSDK_DISPATCH_CPP}")
    endif()
endif()

if(HAVE_GSTREAMER)
    list(APPEND SOURCES
        src/capture/GstFrameCapture.h
        src/capture/GstFrameCapture.cpp
    )
endif()

add_executable(atem-color-ai ${SOURCES})

target_link_libraries(atem-color-ai PRIVATE ${QT_LIBS})

target_compile_definitions(atem-color-ai PRIVATE
    QT_NO_KEYWORDS
)

if(ATEMSDK_FOUND)
    target_compile_definitions(atem-color-ai PRIVATE HAVE_ATEM_SDK)
    target_link_libraries(atem-color-ai PRIVATE ATEMSDK::ATEMSDK)
    if(WIN32)
        target_compile_definitions(atem-color-ai PRIVATE ATEM_SDK_WIN32)
        target_link_libraries(atem-color-ai PRIVATE ole32 oleaut32)
    elseif(APPLE)
        find_library(ATEM_COREFOUNDATION CoreFoundation REQUIRED)
        target_link_libraries(atem-color-ai PRIVATE "${ATEM_COREFOUNDATION}")
    endif()
endif()

if(HAVE_GSTREAMER)
    if(QT_VERSION_MAJOR EQUAL 6)
        target_link_libraries(atem-color-ai PRIVATE Qt6::Gui)
    else()
        target_link_libraries(atem-color-ai PRIVATE Qt5::Gui)
    endif()
    target_compile_definitions(atem-color-ai PRIVATE HAVE_GSTREAMER)
    target_include_directories(atem-color-ai PRIVATE ${GSTREAMER_INCLUDE_DIRS})
    target_link_libraries(atem-color-ai PRIVATE ${GSTREAMER_LIBRARIES})
    target_compile_options(atem-color-ai PRIVATE ${GSTREAMER_CFLAGS_OTHER})
endif()

install(TARGETS atem-color-ai RUNTIME DESTINATION bin)