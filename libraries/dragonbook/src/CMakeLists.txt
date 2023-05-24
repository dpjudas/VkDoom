cmake_minimum_required(VERSION 3.3.2)

set(LIB_SOURCES
	dragonbook.h
	dragonbook/IR.h
	dragonbook/IRContext.h
	dragonbook/IRBuilder.h
	dragonbook/IRValue.h
	dragonbook/IRType.h
	dragonbook/IRInst.h
	dragonbook/IRInstVisitor.h
	dragonbook/IRBasicBlock.h
	dragonbook/IRFunction.h
	dragonbook/IRInstValidator.h
	dragonbook/JITRuntime.h
	ir/IRContext.cpp
	ir/IRBuilder.cpp
	ir/IRFunction.cpp
	ir/IRInstValidator.cpp
	jit/JITRuntime.cpp
	jit/NativeSymbolResolver.cpp
	jit/NativeSymbolResolver.h
	jit/StackTrace.cpp
	jit/StackTrace.h
	mc/RegisterAllocator.cpp
	mc/RegisterAllocator.h
	mc/MachineInstSelection.cpp
	mc/MachineInstSelection.h
	mc/MachineInst.h
	mc/MachineCodeHolder.cpp
	mc/MachineCodeHolder.h
	mc/MachineCodeWriter.cpp
	mc/MachineCodeWriter.h
	mc/AssemblyWriter.cpp
	mc/AssemblyWriter.h
	mc/UnwindInfoUnix.cpp
	mc/UnwindInfoUnix.h
	mc/UnwindInfoWindows.cpp
	mc/UnwindInfoWindows.h
)

set(LIB_LIBS
)

include_directories(.)

set(NOT_COMPILED_SOURCES
)

if (WIN32)
	add_definitions(-DWIN32 -D_WIN32)
endif()

# Set up flags for MSVC
if (MSVC)
	set(CMAKE_CXX_FLAGS "/MP ${CMAKE_CXX_FLAGS}") # Use all cores for compilation
else()
	set(CMAKE_CXX_FLAGS "-std=gnu++14 ${CMAKE_CXX_FLAGS}")
endif()

add_library(dragonbook STATIC ${LIB_SOURCES} ${NOT_COMPILED_SOURCES})

target_link_libraries(dragonbook ${LIB_LIBS})

target_compile_definitions(dragonbook PUBLIC "$<$<CONFIG:RELEASE>:RELEASE>")
target_compile_definitions(dragonbook PUBLIC "$<$<CONFIG:MINSIZEREL>:RELEASE>")
target_compile_definitions(dragonbook PUBLIC "$<$<CONFIG:RELWITHDEBINFO>:RELEASE>")

target_compile_definitions(dragonbook PUBLIC "$<$<CONFIG:RELEASE>:NDEBUG>")
target_compile_definitions(dragonbook PUBLIC "$<$<CONFIG:MINSIZEREL>:NDEBUG>")
target_compile_definitions(dragonbook PUBLIC "$<$<CONFIG:RELWITHDEBINFO>:NDEBUG>")

target_compile_definitions(dragonbook PUBLIC "$<$<CONFIG:DEBUG>:DEBUG>")

set_source_files_properties(${NOT_COMPILED_SOURCES} PROPERTIES HEADER_FILE_ONLY TRUE)

install(TARGETS dragonbook LIBRARY DESTINATION lib)
install(FILES dragonbook.h DESTINATION include)
install(DIRECTORY dragonbook/ DESTINATION include/dragonbook FILES_MATCHING PATTERN "*.h*")

source_group("include" REGULAR_EXPRESSION "${CMAKE_CURRENT_SOURCE_DIR}/dragonbook.h")
source_group("dragonbook" REGULAR_EXPRESSION "${CMAKE_CURRENT_SOURCE_DIR}/dragonbook/.+")
source_group("ir" REGULAR_EXPRESSION "${CMAKE_CURRENT_SOURCE_DIR}/ir/.+")
source_group("jit" REGULAR_EXPRESSION "${CMAKE_CURRENT_SOURCE_DIR}/jit/.+")
source_group("mc" REGULAR_EXPRESSION "${CMAKE_CURRENT_SOURCE_DIR}/mc/.+")
