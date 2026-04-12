# SPDX-License-Identifier: MIT
#
# FindNTL
# --------
# Locate the NTL (Number Theory Library) headers and library.
#
# Imported target:
#   NTL::ntl

find_path(NTL_INCLUDE_DIR
          NAMES NTL/ZZ.h
          DOC "NTL include directory")

find_library(NTL_LIBRARY
             NAMES ntl
             DOC "NTL library")

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(NTL
    REQUIRED_VARS NTL_LIBRARY NTL_INCLUDE_DIR)

if(NTL_FOUND)
    set(NTL_INCLUDE_DIRS "${NTL_INCLUDE_DIR}")
    set(NTL_LIBRARIES "${NTL_LIBRARY}")

    if(NOT TARGET NTL::ntl)
        add_library(NTL::ntl UNKNOWN IMPORTED)
        set_target_properties(NTL::ntl PROPERTIES
            IMPORTED_LOCATION "${NTL_LIBRARY}"
            INTERFACE_INCLUDE_DIRECTORIES "${NTL_INCLUDE_DIR}")
    endif()
endif()

mark_as_advanced(NTL_INCLUDE_DIR NTL_LIBRARY)
