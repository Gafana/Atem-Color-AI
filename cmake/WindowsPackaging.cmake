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

    set(_bundle_plugins
        gstcoreelements.dll gstvideoconvert.dll gstvideotestsrc.dll gstapp.dll
        gstpng.dll gstsrt.dll gsttypefindfunctions.dll gstplayback.dll
        gstvideoparsersbad.dll gstjpeg.dll gstdshow.dll gstdecklink.dll
        gstcoretracers.dll gstpango.dll
    )
    foreach(_plug IN LISTS _bundle_plugins)
        if(EXISTS "${_gst_plugins}/${_plug}")
            install(FILES "${_gst_plugins}/${_plug}"
                    DESTINATION gstreamer/lib/gstreamer-1.0)
        endif()
    endforeach()

    file(GLOB _gst_core_dlls
        "${_gst_bin}/gst*.dll"
        "${_gst_bin}/libg*.dll"
        "${_gst_bin}/libgcc*.dll"
        "${_gst_bin}/libwinpthread*.dll"
    )

    if(EXISTS "${_gst_libexec}")
        install(DIRECTORY "${_gst_libexec}/"
                DESTINATION gstreamer/libexec/gstreamer-1.0
                FILES_MATCHING
                PATTERN "gst-plugin-scanner.exe")
    endif()

    file(GLOB _gst_bin_extra
        "${_gst_bin}/zlib*.dll"
        "${_gst_bin}/iconv*.dll"
        "${_gst_bin}/intl*.dll"
        "${_gst_bin}/orc*.dll"
        "${_gst_bin}/srtp*.dll"
        "${_gst_bin}/libsrt*.dll"
    )
    install(FILES ${_gst_bin_extra} DESTINATION gstreamer/bin OPTIONAL)
    install(FILES ${_gst_core_dlls} DESTINATION gstreamer/bin OPTIONAL)
endfunction()