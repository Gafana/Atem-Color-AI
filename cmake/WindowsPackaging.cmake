# CPack / NSIS packaging helpers for Windows (Qt + GStreamer + MSVC runtime).

include(GNUInstallDirs)
include(InstallRequiredSystemLibraries)

set(CMAKE_INSTALL_UCRT_LIBRARIES TRUE)
set(CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS_SKIP TRUE)

if(QT_VERSION_MAJOR EQUAL 6)
    find_program(WINDEPLOYQT_EXECUTABLE windeployqt HINTS "${Qt6_DIR}/../../../bin")
else()
    find_program(WINDEPLOYQT_EXECUTABLE windeployqt HINTS "${Qt5_DIR}/../../../bin")
endif()

if(NOT WINDEPLOYQT_EXECUTABLE)
    message(WARNING "windeployqt not found — Qt DLLs must be deployed manually for the installer")
endif()

function(install_windeployqt _executable)
    if(NOT WINDEPLOYQT_EXECUTABLE)
        return()
    endif()
    install(CODE "
        message(STATUS \"windeployqt: ${_executable}\")
        execute_process(
            COMMAND \"${WINDEPLOYQT_EXECUTABLE}\"
                --no-translations
                --no-compiler-runtime
                \"\$ENV{DESTDIR}\${CMAKE_INSTALL_PREFIX}/bin/${_executable}\"
            RESULT_VARIABLE _windeploy_result
            COMMAND_ERROR_IS_FATAL ANY
        )
    ")
endfunction()

function(install_gstreamer_bundle _gst_root)
    if(NOT _gst_root OR NOT EXISTS "${_gst_root}")
        message(STATUS "GSTREAMER_ROOT unset — skipping bundled GStreamer in installer")
        return()
    endif()

    set(_gst_bin "${_gst_root}/bin")
    set(_gst_plugins "${_gst_root}/lib/gstreamer-1.0")
    set(_gst_libexec "${_gst_root}/libexec/gstreamer-1.0")

    if(NOT EXISTS "${_gst_plugins}")
        message(WARNING "GStreamer plugins dir missing: ${_gst_plugins}")
        return()
    endif()

    if(NOT EXISTS "${_gst_bin}")
        message(WARNING "GStreamer bin dir missing: ${_gst_bin}")
        return()
    endif()

    # Runtime DLLs must live next to atem-color-ai.exe (Windows loads them at process start).
    file(GLOB _gst_bin_dlls "${_gst_bin}/*.dll")
    if(_gst_bin_dlls)
        install(FILES ${_gst_bin_dlls} DESTINATION bin)
    else()
        message(WARNING "No GStreamer DLLs found in ${_gst_bin}")
    endif()

    set(_bundle_plugins
        gstcoreelements.dll gstvideoconvert.dll gstvideotestsrc.dll gstapp.dll
        gstpng.dll gstsrt.dll gsttypefindfunctions.dll gstplayback.dll
        gstvideoparsersbad.dll gstjpeg.dll gstdshow.dll gstdecklink.dll
        gstcoretracers.dll gstpango.dll
        # GStreamer >= 1.22: videoconvert + videoscale live in gstvideoconvertscale; capture sources
        gstvideoconvertscale.dll gstwinks.dll gstmediafoundation.dll gstd3d11.dll gstautodetect.dll
    )
    foreach(_plug IN LISTS _bundle_plugins)
        if(EXISTS "${_gst_plugins}/${_plug}")
            install(FILES "${_gst_plugins}/${_plug}" DESTINATION lib/gstreamer-1.0)
        endif()
    endforeach()

    if(EXISTS "${_gst_libexec}")
        install(DIRECTORY "${_gst_libexec}/"
                DESTINATION libexec/gstreamer-1.0
                FILES_MATCHING
                PATTERN "gst-plugin-scanner.exe")
    endif()
endfunction()

# NSIS Start Menu + desktop shortcuts (must match install(TARGETS …) executable name).
function(atem_configure_cpack_nsis_shortcuts)
    set(_exe "atem-color-ai")
    set(_label "Atem Color AI")

    set(CPACK_PACKAGE_EXECUTABLES "${_exe};${_label}" PARENT_SCOPE)
    set(CPACK_CREATE_DESKTOP_LINKS "${_exe}" PARENT_SCOPE)
    set(CPACK_NSIS_INSTALLED_ICON_NAME "bin\\\\${_exe}.exe" PARENT_SCOPE)

    # CPACK_CREATE_DESKTOP_LINKS relies on the install-options page tied to PATH; with
    # CPACK_NSIS_MODIFY_PATH OFF the checkbox does not create icons (CMake #15982).
    # Forward slashes in INSTDIR paths — valid for NSIS and avoid CMake escape errors in CPackConfig.
    set(_desktop_shortcut
        "CreateShortCut \"\$DESKTOP/${_label}.lnk\" \"\$INSTDIR/bin/${_exe}.exe\"")
    set(CPACK_NSIS_CREATE_ICONS_EXTRA "${_desktop_shortcut}" PARENT_SCOPE)
    set(CPACK_NSIS_DELETE_ICONS_EXTRA "Delete \"\$DESKTOP/${_label}.lnk\"" PARENT_SCOPE)
endfunction()