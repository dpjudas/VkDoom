cmake_minimum_required(VERSION 3.3.2)

set(TEST_SOURCES
	Main.cpp
)

set(TEST_LIBS
	dragonbook
)

include_directories(. ../src)

if (WIN32)
	add_definitions(-DWIN32 -D_WIN32)
endif()

# Set up flags for MSVC
if (MSVC)
	set(CMAKE_CXX_FLAGS "/MP ${CMAKE_CXX_FLAGS}") # Use all cores for compilation
endif()

add_executable(dragontest ${TEST_SOURCES})

set_target_properties(dragontest PROPERTIES CXX_STANDARD 17)

target_link_libraries(dragontest ${TEST_LIBS})
