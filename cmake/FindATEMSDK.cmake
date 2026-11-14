# FindATEMSDK.cmake — locate locally installed Blackmagic ATEM Switchers SDK.
#
# Set ATEM_SDK_DIR to the root of the unpacked SDK before configuring:
#   cmake -DATEM_SDK_DIR=/path/to/ATEM SDK ...
#
# Windows: expects Include/BMDSwitcherAPI.idl or generated headers + BMDSwitcherAPI.lib
# macOS:   expects BMDSwitcherAPI.framework or Include/BMDSwitcherAPI.h
# Linux:   no official SDK; module sets ATEMSDK_FOUND FALSE unless headers are provided manually.

include(FindPackageHandleStandardArgs)

set(_ATEM_SEARCH_HINTS)
if(DEFINED ATEM_SDK_DIR)
    list(APPEND _ATEM_SEARCH_HINTS "${ATEM_SDK_DIR}")
endif()
list(APPEND _ATEM_SEARCH_HINTS
    "${CMAKE_SOURCE_DIR}/third_party/atem-sdk"
    "$ENV{ATEM_SDK_DIR}"
)

set(ATEMSDK_INCLUDE_DIR "")
set(ATEMSDK_LIBRARIES "")
set(ATEMSDK_PLATFORM "")

if(WIN32)
    find_path(ATEMSDK_INCLUDE_DIR
        NAMES BMDSwitcherAPI_h.h BMDSwitcherAPI.idl
        HINTS ${_ATEM_SEARCH_HINTS}
        PATH_SUFFIXES Include include
    )
    find_library(ATEMSDK_LIBRARY
        NAMES BMDSwitcherAPI
        HINTS ${_ATEM_SEARCH_HINTS}
        PATH_SUFFIXES lib Win x64
    )
    if(ATEMSDK_INCLUDE_DIR AND ATEMSDK_LIBRARY)
        set(ATEMSDK_LIBRARIES "${ATEMSDK_LIBRARY}")
        set(ATEMSDK_PLATFORM "WIN32_COM")
    endif()
elseif(APPLE)
    find_path(ATEMSDK_FRAMEWORK
        NAMES BMDSwitcherAPI.framework
        HINTS ${_ATEM_SEARCH_HINTS}
        PATH_SUFFIXES lib Mac
    )
    if(ATEMSDK_FRAMEWORK)
        set(ATEMSDK_INCLUDE_DIR "${ATEMSDK_FRAMEWORK}/Headers")
        set(ATEMSDK_LIBRARIES "-F${ATEMSDK_FRAMEWORK}/.." -framework BMDSwitcherAPI)
        set(ATEMSDK_PLATFORM "MACOS_FRAMEWORK")
    else()
        find_path(ATEMSDK_INCLUDE_DIR
            NAMES BMDSwitcherAPI.h
            HINTS ${_ATEM_SEARCH_HINTS}
            PATH_SUFFIXES Include Mac include
        )
        if(ATEMSDK_INCLUDE_DIR)
            set(ATEMSDK_PLATFORM "MACOS_HEADERS")
        endif()
    endif()
else()
    # Linux: unofficial/community headers only
    find_path(ATEMSDK_INCLUDE_DIR
        NAMES BMDSwitcherAPI.h
        HINTS ${_ATEM_SEARCH_HINTS}
        PATH_SUFFIXES include Include
    )
    if(ATEMSDK_INCLUDE_DIR)
        set(ATEMSDK_PLATFORM "LINUX_HEADERS")
        message(WARNING "ATEM SDK is not officially supported on Linux; using headers only if found.")
    endif()
endif()

find_package_handle_standard_args(ATEMSDK
    REQUIRED_VARS ATEMSDK_INCLUDE_DIR
)

if(ATEMSDK_FOUND)
    if(NOT ATEMSDK_PLATFORM)
        set(ATEMSDK_PLATFORM "UNKNOWN")
    endif()
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

mark_as_advanced(ATEMSDK_INCLUDE_DIR ATEMSDK_LIBRARY ATEMSDK_FRAMEWORK)