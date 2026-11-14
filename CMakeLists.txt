cmake_minimum_required(VERSION 3.16)
project(AtemColorAI VERSION 0.2.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

option(ENABLE_GUI "Build Qt Widgets GUI shell" OFF)
option(ENABLE_GSTREAMER "Enable GStreamer frame capture module" ON)
option(BUILD_TESTING "Build unit tests" ON)
set(ATEM_SDK_DIR "" CACHE PATH "Path to locally installed Blackmagic ATEM Switchers SDK")

set(QT_MIN_VERSION 5.15)
find_package(Qt6 COMPONENTS Core Gui Widgets Test QUIET)
if(Qt6_FOUND)
    set(QT_VERSION_MAJOR 6)
    set(QT_LIBS Qt6::Core Qt6::Gui)
    set(QT_TEST_LIBS Qt6::Test)
else()
    find_package(Qt5 ${QT_MIN_VERSION} REQUIRED COMPONENTS Core Gui Widgets Test)
    set(QT_VERSION_MAJOR 5)
    set(QT_LIBS Qt5::Core Qt5::Gui)
    set(QT_TEST_LIBS Qt5::Test)
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

add_library(atem_color STATIC
    src/color/ColorTypes.h
    src/color/ColorAnalysis.h
    src/color/ColorAnalysis.cpp
    src/color/ColorMatching.h
    src/color/ColorMatching.cpp
    src/atem/CameraControlProtocol.h
    src/atem/CameraCorrection.h
    src/atem/CameraCorrection.cpp
    src/atem/AtemCorrectionApply.h
    src/atem/AtemCorrectionApply.cpp
)

target_link_libraries(atem_color PUBLIC ${QT_LIBS})
target_include_directories(atem_color PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_compile_definitions(atem_color PUBLIC QT_NO_KEYWORDS)

set(APP_SOURCES
    src/main.cpp
    src/atem/AtemClient.h
    src/atem/AtemClientFactory.cpp
    src/atem/AtemClientMock.cpp
)

if(ATEMSDK_FOUND)
    list(APPEND APP_SOURCES src/atem/AtemClientSdk.cpp)
    if(ATEMSDK_DISPATCH_CPP)
        list(APPEND APP_SOURCES "${ATEMSDK_DISPATCH_CPP}")
        message(STATUS "ATEM SDK dispatch: ${ATEMSDK_DISPATCH_CPP}")
    endif()
endif()

if(HAVE_GSTREAMER)
    list(APPEND APP_SOURCES
        src/capture/GstFrameCapture.h
        src/capture/GstFrameCapture.cpp
    )
endif()

if(ENABLE_GUI)
    list(APPEND APP_SOURCES
        src/gui/MainWindow.h
        src/gui/MainWindow.cpp
        src/gui/HistogramWidget.h
        src/gui/HistogramWidget.cpp
        src/gui/VectorscopeWidget.h
        src/gui/VectorscopeWidget.cpp
    )
endif()

add_executable(atem-color-ai ${APP_SOURCES})
target_link_libraries(atem-color-ai PRIVATE atem_color ${QT_LIBS})
target_compile_definitions(atem-color-ai PRIVATE QT_NO_KEYWORDS)

if(ENABLE_GUI)
    target_compile_definitions(atem-color-ai PRIVATE ENABLE_GUI)
endif()

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
    target_compile_definitions(atem-color-ai PRIVATE HAVE_GSTREAMER)
    target_include_directories(atem-color-ai PRIVATE ${GSTREAMER_INCLUDE_DIRS})
    target_link_libraries(atem-color-ai PRIVATE ${GSTREAMER_LIBRARIES})
    target_compile_options(atem-color-ai PRIVATE ${GSTREAMER_CFLAGS_OTHER})
endif()

install(TARGETS atem-color-ai RUNTIME DESTINATION bin)

if(BUILD_TESTING)
    enable_testing()
    add_executable(tst_coloranalysis tests/tst_coloranalysis.cpp)
    target_link_libraries(tst_coloranalysis PRIVATE atem_color ${QT_TEST_LIBS})
    target_compile_definitions(tst_coloranalysis PRIVATE QT_NO_KEYWORDS)
    add_test(NAME coloranalysis COMMAND tst_coloranalysis)
endif()