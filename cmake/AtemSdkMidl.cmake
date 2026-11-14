# Generate BMDSwitcherAPI headers/sources from Blackmagic IDL (Windows / CI only).

function(atem_sdk_add_midl_target _idl_path _out_dir)
    if(NOT WIN32)
        message(FATAL_ERROR "atem_sdk_add_midl_target is Windows-only")
    endif()
    if(NOT EXISTS "${_idl_path}")
        message(FATAL_ERROR "ATEM SDK IDL not found: ${_idl_path}")
    endif()

    find_program(MIDL_EXECUTABLE midl REQUIRED)
    file(MAKE_DIRECTORY "${_out_dir}")

    set(_header "${_out_dir}/BMDSwitcherAPI.h")
    set(_iid "${_out_dir}/BMDSwitcherAPI_i.c")
    set(_tlb "${_out_dir}/BMDSwitcherAPI.tlb")

    add_custom_command(
        OUTPUT "${_header}" "${_iid}" "${_tlb}"
        COMMAND "${MIDL_EXECUTABLE}"
            /nologo
            /char signed
            /env win64
            /h "${_header}"
            /iid "${_iid}"
            /tlb "${_tlb}"
            "${_idl_path}"
        DEPENDS "${_idl_path}"
        COMMENT "MIDL: BMDSwitcherAPI.idl"
        VERBATIM
    )

    add_library(atem_sdk_midl STATIC "${_iid}")
    target_include_directories(atem_sdk_midl PUBLIC "${_out_dir}")
    set_target_properties(atem_sdk_midl PROPERTIES LINKER_LANGUAGE C)

    set(ATEMSDK_MIDL_INCLUDE_DIR "${_out_dir}" PARENT_SCOPE)
    set(ATEMSDK_MIDL_TARGET atem_sdk_midl PARENT_SCOPE)
endfunction()

function(atem_sdk_find_idl _sdk_root _idl_out)
    set(_candidates
        "${_sdk_root}/Windows/include/BMDSwitcherAPI.idl"
        "${_sdk_root}/include/BMDSwitcherAPI.idl"
    )
    file(GLOB _children LIST_DIRECTORIES true "${_sdk_root}/*")
    foreach(_child IN LISTS _children)
        list(APPEND _candidates
            "${_child}/Windows/include/BMDSwitcherAPI.idl"
            "${_child}/include/BMDSwitcherAPI.idl")
    endforeach()
    foreach(_c IN LISTS _candidates)
        if(EXISTS "${_c}")
            set(${_idl_out} "${_c}" PARENT_SCOPE)
            return()
        endif()
    endforeach()
endfunction()