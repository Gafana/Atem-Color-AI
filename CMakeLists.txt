cmake_minimum_required(VERSION 3.16)
project(AtemColorAI VERSION 0.2.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

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
    src/color/ColorMatchMode.h
    src/color/CorrectionParameterMask.h
    src/color/CorrectionParameterMask.cpp
    src/color/ColorCheckerData.h
    src/color/ColorCheckerData.cpp
    src/color/ColorTuning.h
    src/color/ColorTuning.cpp
    src/color/ReferenceFrameStore.h
    src/color/ReferenceFrameStore.cpp
    src/color/ColorMatching.h
    src/color/ColorMatching.cpp
    src/color/ColorScience.h
    src/color/ColorScience.cpp
    src/color/ColorCharts.h
    src/color/ColorCharts.cpp
    src/color/MatchEngine.h
    src/color/MatchEngine.cpp
    src/atem/CameraControlProtocol.h
    src/atem/CameraCorrection.h
    src/atem/CameraCorrection.cpp
    src/atem/AtemCorrectionApply.h
    src/atem/AtemCorrectionApply.cpp
    src/scopes/ScopeTypes.h
    src/scopes/ScopeTypes.cpp
    src/scopes/ScopeMath.h
    src/scopes/ScopeMath.cpp
    src/scopes/ScopeRender.h
    src/scopes/ScopeWaveform.cpp
    src/scopes/ScopeExtra.cpp
    src/scopes/ScopeProcessor.h
    src/scopes/ScopeProcessor.cpp
    src/scopes/ScopeQc.h
    src/scopes/ScopeQc.cpp
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
    src/capture/MultiviewGeometry.h
    src/capture/MultiviewGeometry.cpp
    src/capture/CaptureRoute.h
    src/capture/VideoDevice.h
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
        src/capture/GstDeviceEnumerator.h
        src/capture/GstDeviceEnumerator.cpp
        src/capture/GstLivePreview.h
        src/capture/GstLivePreview.cpp
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
        src/gui/scopes/ScopeComputeWorker.h
        src/gui/scopes/ScopeComputeWorker.cpp
        src/gui/scopes/ScopeCellView.h
        src/gui/scopes/ScopeCellView.cpp
        src/gui/scopes/ScopeSettingsDialog.h
        src/gui/scopes/ScopeSettingsDialog.cpp
        src/gui/docks/ScopeDockManager.h
        src/gui/docks/ScopeDockManager.cpp
        src/gui/docks/ScopeInstrumentWidget.h
        src/gui/docks/ScopeInstrumentWidget.cpp
        src/gui/docks/SnapshotManager.h
        src/gui/docks/SnapshotManager.cpp
        src/gui/PreviewOverlayWidget.h
        src/gui/PreviewOverlayWidget.cpp
        src/platform/CameraPresetStore.h
        src/platform/CameraPresetStore.cpp
    )
endif()

# Application logo: Qt resource (window icon, dialogs) + Windows exe icon (Explorer, taskbar, shortcuts).
list(APPEND APP_SOURCES resources/app.qrc)
if(WIN32)
    list(APPEND APP_SOURCES resources/app.rc)
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
    if(GSTREAMER_LIBRARY_DIRS)
        target_link_directories(atem-color-ai PRIVATE ${GSTREAMER_LIBRARY_DIRS})
    endif()
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

    # Clean systems: the MSVC runtime DLLs are installed app-local into bin/ (above); in addition the
    # official VC++ 2015-2022 x64 redistributable is bundled and run silently (covers the GStreamer
    # plugin scanner in libexec/ and anything loaded outside bin/).
    set(_vcredist "")
    file(TO_CMAKE_PATH "$ENV{VCToolsRedistDir}" _vc_redist_dir)
    if(_vc_redist_dir AND EXISTS "${_vc_redist_dir}/vc_redist.x64.exe")
        set(_vcredist "${_vc_redist_dir}/vc_redist.x64.exe")
        string(REPLACE "//" "/" _vcredist "${_vcredist}")
    else()
        file(GLOB _vcredist_candidates
            "$ENV{ProgramFiles}/Microsoft Visual Studio/*/*/VC/Redist/MSVC/*/vc_redist.x64.exe")
        if(_vcredist_candidates)
            list(GET _vcredist_candidates 0 _vcredist)
        endif()
    endif()
    if(_vcredist)
        message(STATUS "Bundling VC++ redistributable: ${_vcredist}")
        install(PROGRAMS "${_vcredist}" DESTINATION redist)
        set(CPACK_NSIS_EXTRA_INSTALL_COMMANDS "
  DetailPrint \"Installing Microsoft Visual C++ 2015-2022 x64 runtime...\"
  ExecWait '\"$INSTDIR\\redist\\vc_redist.x64.exe\" /install /quiet /norestart' $0
  DetailPrint \"VC++ runtime installer exit code: $0\"")
    else()
        message(WARNING "vc_redist.x64.exe not found — installer relies on app-local CRT DLLs only")
    endif()

    # ATEM Software Control (Blackmagic, not redistributable): the app needs its COM server
    # (CBMDSwitcherDiscovery). Warn after installation when it is not registered.
    string(APPEND CPACK_NSIS_EXTRA_INSTALL_COMMANDS "
  SetRegView 64
  ReadRegStr $1 HKLM \"SOFTWARE\\Classes\\CLSID\\{A9CDC765-3787-409D-A1E5-29F4F034A599}\\InprocServer32\" \"\"
  SetRegView 32
  StrCmp $1 \"\" 0 atem_runtime_ok
    MessageBox MB_YESNO|MB_ICONEXCLAMATION \"Blackmagic ATEM Software Control is not installed (BMDSwitcherAPI COM server not found).$\\r$\\n$\\r$\\nAtem Color AI needs it to talk to the ATEM switcher. It cannot be bundled (Blackmagic license).$\\r$\\n$\\r$\\nOpen the Blackmagic download page now?\" /SD IDNO IDNO atem_runtime_ok
    ExecShell \"open\" \"https://www.blackmagicdesign.com/support/family/atem-live-production-switchers\"
  atem_runtime_ok:")

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
    set(CPACK_NSIS_MUI_ICON "${CMAKE_SOURCE_DIR}/resources/icons/app-icon.ico")
    set(CPACK_NSIS_MUI_UNIICON "${CMAKE_SOURCE_DIR}/resources/icons/app-icon.ico")
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
    target_link_libraries(tst_atem_paths PRIVATE atem_color ${QT_LIBS} ${QT_TEST_LIBS})
    target_compile_definitions(tst_atem_paths PRIVATE QT_NO_KEYWORDS)
    add_test(NAME atem_paths COMMAND tst_atem_paths)

    add_executable(tst_multiview tests/tst_multiview.cpp
        src/capture/MultiviewGeometry.cpp)
    target_include_directories(tst_multiview PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_link_libraries(tst_multiview PRIVATE ${QT_LIBS} ${QT_TEST_LIBS})
    target_compile_definitions(tst_multiview PRIVATE QT_NO_KEYWORDS)
    add_test(NAME multiview COMMAND tst_multiview)

    add_executable(tst_colortuning tests/tst_colortuning.cpp)
    target_link_libraries(tst_colortuning PRIVATE atem_color ${QT_TEST_LIBS})
    target_compile_definitions(tst_colortuning PRIVATE QT_NO_KEYWORDS)
    add_test(NAME colortuning COMMAND tst_colortuning)

    add_executable(tst_matchengine tests/tst_matchengine.cpp)
    target_link_libraries(tst_matchengine PRIVATE atem_color ${QT_TEST_LIBS})
    target_compile_definitions(tst_matchengine PRIVATE QT_NO_KEYWORDS)
    add_test(NAME matchengine COMMAND tst_matchengine)

    add_executable(tst_scopes tests/tst_scopes.cpp)
    target_link_libraries(tst_scopes PRIVATE atem_color ${QT_TEST_LIBS})
    target_compile_definitions(tst_scopes PRIVATE QT_NO_KEYWORDS)
    add_test(NAME scopes COMMAND tst_scopes)
endif()