cmake_minimum_required(VERSION 3.16)
project(AtemColorAI VERSION 0.2.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

option(ENABLE_GUI "Build Qt Widgets GUI shell" OFF)
option(ENABLE_GSTREAMER "Enable GStreamer frame capture module" ON)
option(BUILD_TESTING "Build unit tests" ON)
option(BUILD_WINDOWS_INSTALLER "Configure CPack NSIS installer (Windows)" OFF)
set(ATEM_SDK_DIR "" CACHE PATH "Path to locally installed Blackmagic ATEM Switchers SDK")
set(GSTREAMER_ROOT "" CACHE PATH "GStreamer MSVC prefix for Windows bundling (e.g. C:/gstreamer/1.0/msvc_x86_64)")

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

set(ATEMSDK_MIDL_TARGET "")
if(WIN32 AND ATEM_SDK_DIR AND NOT ATEMSDK_FOUND)
    include(AtemSdkMidl)
    atem_sdk_find_idl("${ATEM_SDK_DIR}" ATEM_SDK_IDL)
    if(ATEM_SDK_IDL)
        enable_language(C)
        atem_sdk_add_midl_target("${ATEM_SDK_IDL}" "${CMAKE_BINARY_DIR}/atem_midl")
        set(ATEMSDK_FOUND TRUE)
        set(ATEMSDK_INCLUDE_DIR "${ATEMSDK_MIDL_INCLUDE_DIR}")
        set(ATEMSDK_PLATFORM "WIN32_MIDL")
        if(NOT TARGET ATEMSDK::ATEMSDK)
            add_library(ATEMSDK::ATEMSDK INTERFACE IMPORTED)
            set_target_properties(ATEMSDK::ATEMSDK PROPERTIES
                INTERFACE_INCLUDE_DIRECTORIES "${ATEMSDK_INCLUDE_DIR}")
        endif()
        message(STATUS "ATEM SDK IDL found — MIDL headers: ${ATEMSDK_INCLUDE_DIR}")
    endif()
endif()

if(ATEMSDK_FOUND)
    message(STATUS "ATEM SDK found (${ATEMSDK_PLATFORM}): ${ATEMSDK_INCLUDE_DIR}")
else()
    message(STATUS "ATEM SDK not found — building with mock AtemClient (set -DATEM_SDK_DIR=...)")
endif()

if(ATEMSDK_FOUND)
    set(ATEM_BUILD_VARIANT "atem-sdk")
else()
    set(ATEM_BUILD_VARIANT "mock")
endif()

set(HAVE_GSTREAMER OFF)
if(ENABLE_GSTREAMER)
    if(WIN32 AND GSTREAMER_ROOT)
        set(ENV{PKG_CONFIG_PATH}
            "${GSTREAMER_ROOT}/lib/pkgconfig;${GSTREAMER_ROOT}/lib/gstreamer-1.0/pkgconfig")
    endif()
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        pkg_check_modules(GSTREAMER QUIET gstreamer-1.0 gstreamer-app-1.0)
        if(GSTREAMER_FOUND)
            set(HAVE_GSTREAMER ON)
            message(STATUS "GStreamer found: ${GSTREAMER_VERSION}")
        endif()
    endif()
    if(NOT HAVE_GSTREAMER AND WIN32 AND GSTREAMER_ROOT AND EXISTS "${GSTREAMER_ROOT}/include/gstreamer-1.0")
        set(HAVE_GSTREAMER ON)
        set(GSTREAMER_INCLUDE_DIRS
            "${GSTREAMER_ROOT}/include/gstreamer-1.0"
            "${GSTREAMER_ROOT}/include/glib-2.0"
            "${GSTREAMER_ROOT}/lib/glib-2.0/include")
        set(GSTREAMER_LIBRARIES
            "${GSTREAMER_ROOT}/lib/gstreamer-1.0.lib"
            "${GSTREAMER_ROOT}/lib/gstapp-1.0.lib"
            "${GSTREAMER_ROOT}/lib/glib-2.0.lib"
            "${GSTREAMER_ROOT}/lib/gobject-2.0.lib")
        message(STATUS "GStreamer (manual GSTREAMER_ROOT): ${GSTREAMER_ROOT}")
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
    src/platform/AtemAppSettings.h
    src/platform/AtemAppSettings.cpp
    src/platform/AtemPathValidation.h
    src/platform/AtemPathValidation.cpp
    src/platform/AtemRuntime.h
    src/platform/AtemRuntime.cpp
    src/platform/AtemSetup.h
    src/platform/AtemSetup.cpp
)

if(WIN32)
    list(APPEND APP_SOURCES
        src/platform/WindowsBundledPaths.h
        src/platform/WindowsBundledPaths.cpp
    )
endif()

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
        src/gui/AtemPathsSetupDialog.h
        src/gui/AtemPathsSetupDialog.cpp
        src/gui/AtemAdvancedSettingsDialog.h
        src/gui/AtemAdvancedSettingsDialog.cpp
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
        if(ATEMSDK_MIDL_TARGET)
            target_link_libraries(atem-color-ai PRIVATE "${ATEMSDK_MIDL_TARGET}")
        endif()
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

if(WIN32 AND BUILD_WINDOWS_INSTALLER)
    include(WindowsPackaging)

    install(PROGRAMS ${CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS} DESTINATION bin)

    install_windeployqt("atem-color-ai.exe")

    if(GSTREAMER_ROOT)
        install_gstreamer_bundle("${GSTREAMER_ROOT}")
    elseif(DEFINED ENV{GSTREAMER_ROOT})
        install_gstreamer_bundle("$ENV{GSTREAMER_ROOT}")
    endif()

    install(FILES
        "${CMAKE_SOURCE_DIR}/cmake/nsis/AtemRuntimeWarning.nsh"
        DESTINATION share/nsis)

    set(CPACK_GENERATOR "NSIS")
    set(CPACK_PACKAGE_NAME "AtemColorAI")
    set(CPACK_PACKAGE_VENDOR "Gafana")
    set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
    set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "ATEM camera color matching for Windows")
    set(CPACK_PACKAGE_INSTALL_DIRECTORY "Atem Color AI")
    set(CPACK_PACKAGE_INSTALL_REGISTRY_KEY "AtemColorAI")
    set(CPACK_NSIS_DISPLAY_NAME "Atem Color AI")
    set(CPACK_NSIS_PACKAGE_NAME "Atem Color AI")
    set(CPACK_NSIS_HELP_LINK
        "https://www.blackmagicdesign.com/support/family/atem-live-production-switchers")
    set(CPACK_NSIS_URL_INFO_ABOUT "https://github.com/Gafana/Atem-Color-AI")
    set(CPACK_NSIS_CONTACT "https://github.com/Gafana/Atem-Color-AI")
    atem_configure_cpack_nsis_shortcuts()
    # Do not append install dir to system PATH (long PATH breaks NSIS; app uses bundled layout).
    set(CPACK_NSIS_MODIFY_PATH OFF)
    set(CPACK_NSIS_ENABLE_UNINSTALL_BEFORE_INSTALL ON)
    set(CPACK_PACKAGE_FILE_NAME
        "${CPACK_PACKAGE_NAME}-${CPACK_PACKAGE_VERSION}-win64-${ATEM_BUILD_VARIANT}")
    # Custom NSIS warning macro (cmake/nsis/AtemRuntimeWarning.nsh) can be wired via
    # CPACK_NSIS_INCLUDES when makensis on the target CI image accepts the script.
    if(CMAKE_CONFIGURATION_TYPES)
        set(CPACK_INSTALL_CMAKE_PROJECTS "${PROJECT_NAME};ALL;ALL;")
    else()
        set(CPACK_INSTALL_CONFIG_NAME Release)
    endif()

    set(CPACK_VERBATIM_VARIABLES TRUE)
    include(CPack)
endif()

if(BUILD_TESTING)
    enable_testing()
    add_executable(tst_coloranalysis tests/tst_coloranalysis.cpp)
    target_link_libraries(tst_coloranalysis PRIVATE atem_color ${QT_TEST_LIBS})
    target_compile_definitions(tst_coloranalysis PRIVATE QT_NO_KEYWORDS)
    add_test(NAME coloranalysis COMMAND tst_coloranalysis)

    add_executable(tst_atem_paths tests/tst_atem_paths.cpp
        src/platform/AtemAppSettings.cpp
        src/platform/AtemPathValidation.cpp)
    target_include_directories(tst_atem_paths PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_link_libraries(tst_atem_paths PRIVATE ${QT_LIBS} ${QT_TEST_LIBS})
    target_compile_definitions(tst_atem_paths PRIVATE QT_NO_KEYWORDS)
    add_test(NAME atem_paths COMMAND tst_atem_paths)
endif()