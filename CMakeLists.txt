cmake_minimum_required(VERSION 3.4 FATAL_ERROR)

project(ibpp)

option(BUILD_TEST "Build test" OFF)

set(SOURCES
	core/_dpb.cpp
	core/_ibpp.cpp
	core/_ibpp.h
	core/_ibs.cpp
	core/_rb.cpp
	core/_spb.cpp
	core/_tpb.cpp
	core/array.cpp
	core/blob.cpp
	core/database.cpp
	core/date.cpp
	core/dbkey.cpp
	core/events.cpp
	core/exception.cpp
	core/ibase.h
	core/iberror.h
	core/ibpp.h
	core/perf.h
	core/row.cpp
	core/service.cpp
	core/statement.cpp
	core/time.cpp
	core/transaction.cpp
	core/user.cpp
)

add_library(ibpp ${SOURCES})
target_include_directories(ibpp PUBLIC core)
target_compile_definitions(ibpp PUBLIC $<$<PLATFORM_ID:Linux>:IBPP_LINUX=1> $<$<PLATFORM_ID:Windows>:IBPP_WINDOWS=1>)

if (BUILD_TEST)
	add_executable(tests tests/tests.cpp)
	target_link_libraries(tests ibpp)
endif()

add_library(ibpp::ibpp ALIAS ibpp)