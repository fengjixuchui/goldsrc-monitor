cmake_minimum_required(VERSION 3.3)

project(goldsrc-monitor)
include("cmake/msvc_winxp_support.cmake")

if (POLICY CMP0069)
    cmake_policy(SET CMP0069 NEW)
endif()

cmake_policy(SET CMP0063 NEW)
if (POLICY CMP0091)
    cmake_policy(SET CMP0091 NEW)
endif()


if (CMAKE_SIZEOF_VOID_P EQUAL 4 AND CMAKE_SYSTEM_PROCESSOR MATCHES "^(i386|i486|i586|i686|x86_64|AMD64)$")
    # OK
else()
    message(FATAL_ERROR "This project can be compiled only in x86 mode on x86/x64 processors (got ${CMAKE_SIZEOF_VOID_P} and ${CMAKE_SYSTEM_PROCESSOR})")
endif()

if (NOT DEFINED CMAKE_POSITION_INDEPENDENT_CODE)
    SET(CMAKE_POSITION_INDEPENDENT_CODE ON)
endif()

add_subdirectory(cmake/loader)
add_subdirectory(cmake/library)

enable_winxp_support()
set_target_properties(goldsrc-monitor-loader goldsrc-monitor-library PROPERTIES 
	ARCHIVE_OUTPUT_DIRECTORY ../../bin
	LIBRARY_OUTPUT_DIRECTORY ../../bin
	RUNTIME_OUTPUT_DIRECTORY ../../bin
)
