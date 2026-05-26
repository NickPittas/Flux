# ***** BEGIN LICENSE BLOCK *****
# This file is part of Natron <https://natrongithub.github.io/>,
# (C) 2018-2023 The Natron developers
# (C) 2013-2018 INRIA and Alexandre Gauthier-Foichat
#
# Natron is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.
#
# Natron is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with Natron.  If not, see <http://www.gnu.org/licenses/gpl-2.0.html>
# ***** END LICENSE BLOCK *****

if(NOT glog_FOUND)
    add_subdirectory(gflags)
    add_subdirectory(glog)
endif()
add_subdirectory(hoedown)
add_subdirectory(libtess)
if(NOT Ceres_FOUND)
    add_subdirectory(ceres)
endif()
add_subdirectory(libmv)
add_subdirectory(openMVG)
add_subdirectory(qhttpserver)

function(natron_suppress_third_party_warnings target_name)
    if(TARGET ${target_name} AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(${target_name} PRIVATE
            $<$<COMPILE_LANGUAGE:CXX>:-Wno-sign-compare>
            $<$<COMPILE_LANGUAGE:CXX>:-Wno-unused-parameter>
            $<$<COMPILE_LANGUAGE:CXX>:-Wno-unused-but-set-variable>
            $<$<COMPILE_LANGUAGE:CXX>:-Wno-unused-function>
            $<$<COMPILE_LANGUAGE:CXX>:-Wno-unused-result>
            $<$<COMPILE_LANGUAGE:CXX>:-Wno-missing-declarations>
            $<$<COMPILE_LANGUAGE:CXX>:-Wno-deprecated-declarations>
            $<$<COMPILE_LANGUAGE:CXX>:-Wno-maybe-uninitialized>
            $<$<COMPILE_LANGUAGE:CXX>:-Wno-unknown-pragmas>
            $<$<COMPILE_LANGUAGE:CXX>:-Wno-comment>
            $<$<COMPILE_LANGUAGE:CXX>:-Wno-reorder>
            $<$<COMPILE_LANGUAGE:CXX>:-Wno-class-memaccess>
        )
    endif()
endfunction()

function(natron_suppress_third_party_c_warnings target_name)
    if(TARGET ${target_name} AND CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(${target_name} PRIVATE
            $<$<COMPILE_LANGUAGE:C>:-Wno-sign-compare>
            $<$<COMPILE_LANGUAGE:C>:-Wno-unused-parameter>
            $<$<COMPILE_LANGUAGE:C>:-Wno-unused-but-set-variable>
            $<$<COMPILE_LANGUAGE:C>:-Wno-unused-function>
            $<$<COMPILE_LANGUAGE:C>:-Wno-unused-result>
            $<$<COMPILE_LANGUAGE:C>:-Wno-missing-prototypes>
            $<$<COMPILE_LANGUAGE:C>:-Wno-unknown-pragmas>
            $<$<COMPILE_LANGUAGE:C>:-Wno-comment>
        )
    endif()
endfunction()

foreach(target_name IN ITEMS gflags glog ceres mv openMVG qhttpserver)
    natron_suppress_third_party_warnings(${target_name})
endforeach()
foreach(target_name IN ITEMS hoedown tess)
    natron_suppress_third_party_c_warnings(${target_name})
endforeach()
