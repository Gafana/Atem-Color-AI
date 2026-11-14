# FindATEMSDK.cmake — locate locally installed Blackmagic ATEM Switchers SDK.
#
# Tested layout: "Blackmagic ATEM Switchers SDK 11.0.1" (Win + Mac OS X trees).
#
#   cmake -DATEM_SDK_DIR="/path/to/Blackmagic ATEM Switchers SDK 11.0.1" ...
#
# Windows: BMDSwitcherAPI.idl under Windows/include; link BMDSwitcherAPI.lib from
#          the installed SDK (or use MIDL-generated headers from a sample project).
# macOS:   Mac OS X/include/BMDSwitcherAPI.h + BMDSwitcherAPIDispatch.cpp
# Linux:   headers may be present for reference, but linking/runtime is unsupported — mock build.

include(FindPackageHandleStandardArgs)

set(_ATEM_SEARCH_HINTS)
if(ATEM_SDK_DIR)
    list(APPEND _ATEM_SEARCH_HINTS "${ATEM_SDK_DIR}")
endif()
list(APPEND _ATEM_SEARCH_HINTS
    "${CMAKE_SOURCE_DIR}/third_party/atem-sdk"
    "${CMAKE_SOURCE_DIR}/third_party/atem-sdk-extract"
    "$ENV{ATEM_SDK_DIR}"
)

set(ATEMSDK_INCLUDE_DIR "")
set(ATEMSDK_LIBRARIES "")
set(ATEMSDK_PLATFORM "")
set(ATEMSDK_DISPATCH_CPP "" CACHE FILEPATH "BMDSwitcherAPIDispatch.cpp when using macOS headers")

foreach(_root IN LISTS _ATEM_SEARCH_HINTS)
    if(NOT _root OR NOT EXISTS "${_root}")
        continue()
    endif()
    file(GLOB _sdk_children LIST_DIRECTORIES true "${_root}/*")
    foreach(_child IN LISTS _sdk_children)
        if(IS_DIRECTORY "${_child}")
            list(APPEND _ATEM_SEARCH_HINTS "${_child}")
        endif()
    endforeach()
endforeach()
list(REMOVE_DUPLICATES _ATEM_SEARCH_HINTS)

if(WIN32 OR APPLE)
    find_path(ATEMSDK_INCLUDE_DIR
        NAMES BMDSwitcherAPI.h
        HINTS ${_ATEM_SEARCH_HINTS}
        PATH_SUFFIXES
            "Mac OS X/include"
            "include"
            "Windows/Samples/DeviceInfo"
    )

    if(NOT ATEMSDK_INCLUDE_DIR)
        find_path(ATEMSDK_IDL_DIR
            NAMES BMDSwitcherAPI.idl
            HINTS ${_ATEM_SEARCH_HINTS}
            PATH_SUFFIXES "Windows/include" "include"
        )
        if(ATEMSDK_IDL_DIR)
            message(STATUS "ATEM SDK IDL found at ${ATEMSDK_IDL_DIR}; generate BMDSwitcherAPI.h with MIDL "
                           "or point ATEM_SDK_DIR to a tree that includes Windows/Samples/DeviceInfo.")
        endif()
    endif()
else()
    find_path(ATEMSDK_HEADER_REF
        NAMES BMDSwitcherAPI.h
        HINTS ${_ATEM_SEARCH_HINTS}
        PATH_SUFFIXES "Mac OS X/include" "include"
    )
    if(ATEMSDK_HEADER_REF)
        message(STATUS "ATEM SDK headers at ${ATEMSDK_HEADER_REF} ignored on Linux (no official runtime).")
    endif()
    set(ATEMSDK_INCLUDE_DIR "")
endif()

if(WIN32 AND ATEMSDK_INCLUDE_DIR)
    find_library(ATEMSDK_LIBRARY
        NAMES BMDSwitcherAPI
        HINTS ${_ATEM_SEARCH_HINTS}
        PATH_SUFFIXES lib Win x64 Lib/x64
    )
    if(ATEMSDK_LIBRARY)
        set(ATEMSDK_LIBRARIES "${ATEMSDK_LIBRARY}")
        set(ATEMSDK_PLATFORM "WIN32_COM")
    elseif(ATEMSDK_INCLUDE_DIR MATCHES "DeviceInfo")
        set(ATEMSDK_PLATFORM "WIN32_COM_HEADERS")
        message(STATUS "ATEM SDK: using sample-generated headers; install/link BMDSwitcherAPI.lib from Blackmagic.")
    endif()
elseif(APPLE AND ATEMSDK_INCLUDE_DIR)
    find_path(ATEMSDK_FRAMEWORK
        NAMES BMDSwitcherAPI.framework
        HINTS ${_ATEM_SEARCH_HINTS}
        PATH_SUFFIXES lib "Mac OS X" Mac
    )
    if(ATEMSDK_FRAMEWORK)
        set(ATEMSDK_INCLUDE_DIR "${ATEMSDK_FRAMEWORK}/Headers")
        get_filename_component(_fw_parent "${ATEMSDK_FRAMEWORK}" DIRECTORY)
        set(ATEMSDK_LIBRARIES "-F${_fw_parent}" "-framework" "BMDSwitcherAPI")
        set(ATEMSDK_PLATFORM "MACOS_FRAMEWORK")
    else()
        find_file(ATEMSDK_DISPATCH_CPP
            NAMES BMDSwitcherAPIDispatch.cpp
            HINTS ${_ATEM_SEARCH_HINTS}
            PATH_SUFFIXES "Mac OS X/include" include
        )
        set(ATEMSDK_PLATFORM "MACOS_HEADERS")
    endif()
endif()

find_package_handle_standard_args(ATEMSDK
    REQUIRED_VARS ATEMSDK_INCLUDE_DIR
)

if(ATEMSDK_FOUND AND NOT ATEMSDK_PLATFORM)
    set(ATEMSDK_PLATFORM "UNKNOWN")
endif()

if(ATEMSDK_FOUND)
    if(NOT TARGET ATEMSDK::ATEMSDK)
        add_library(ATEMSDK::ATEMSDK INTERFACE IMPORTED)
        set_target_properties(ATEMSDK::ATEMSDK PROPERTIES
            INTERFACE_INCLUDE_DIRECTORIES "${ATEMSDK_INCLUDE_DIR}"
        )
        if(ATEMSDK_LIBRARIES)
            set_target_properties(ATEMSDK::ATEMSDK PROPERTIES
                INTERFACE_LINK_LIBRARIES "${ATEMSDK_LIBRARIES}"
            )
        endif()
    endif()
endif()

mark_as_advanced(ATEMSDK_INCLUDE_DIR ATEMSDK_LIBRARY ATEMSDK_FRAMEWORK ATEMSDK_DISPATCH_CPP ATEMSDK_HEADER_REF)