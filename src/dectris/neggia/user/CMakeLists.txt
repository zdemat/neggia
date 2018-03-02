#  The MIT License (MIT)
#
#  Copyright (c) 2017 Dectris Ltd., Taefernweg 1, 5405 Baden-Daettwil, Switzerland.
#
#  Permission is hereby granted, free of charge, to any person obtaining a copy
#  of this software and associated documentation files (the "Software"), to deal
#  in the Software without restriction, including without limitation the rights
#  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
#  copies of the Software, and to permit persons to whom the Software is
#  furnished to do so, subject to the following conditions:
#
#  The above copyright notice and this permission notice shall be included in all
#  copies or substantial portions of the Software.
#
#  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
#  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
#  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
#  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
#  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
#  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
#  SOFTWARE.

set(SOURCES "H5File.cpp" "Dataset.cpp" "Synchronization.h" "Synchronization.cpp")
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

set(Boost_USE_MULTITHREADED      ON)

find_package( Boost REQUIRED )

if( ${Boost_VERSION} VERSION_LESS "106500" )
    set(Boost_REQUIRED_COMPONENTS system thread program_options)
    set(System_LIBRARIES pthread rt)
else()
    set(Boost_REQUIRED_COMPONENTS system thread program_options stacktrace_basic stacktrace_addr2line)
    set(System_LIBRARIES pthread rt dl)
endif()

find_package( Boost REQUIRED COMPONENTS ${Boost_REQUIRED_COMPONENTS} )

include_directories( ${Boost_INCLUDE_DIR} )

add_library(NEGGIA_USER OBJECT ${SOURCES})
add_executable( neggia_sync_util neggia_sync_util.cpp )
add_executable( neggia_sync_test neggia_sync_test.cpp )

target_link_libraries( neggia_sync_util LINK_PUBLIC neggia_static ${System_LIBRARIES} ${Boost_LIBRARIES} )
target_link_libraries( neggia_sync_test LINK_PUBLIC neggia_static ${System_LIBRARIES} ${Boost_LIBRARIES} )

install(TARGETS neggia_sync_util neggia_sync_test DESTINATION bin)
