cmake_minimum_required(VERSION 3.9.4)
project(tb VERSION 0.1.0)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_VERBOSE_MAKEFILE ON)

file(GLOB source_files ${PROJECT_SOURCE_DIR}/src/*.cpp)

set(CMAKE_CPP_COMPILER clang++)

add_executable(tb ${source_files})
target_compile_features(tb PUBLIC cxx_std_23)

set(LZ4_BUNDLED_MODE ON)
set(LZ4_BUILD_CLI OFF)

add_subdirectory(mimalloc)

add_subdirectory(lz4/build/cmake)
target_link_libraries(tb lz4_static mimalloc-static)
target_include_directories(tb PRIVATE ${PROJECT_SOURCE_DIR}/lz4/lib)

set_property(TARGET tb PROPERTY CXX_STANDARD 23)
set_property(TARGET tb PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
set(CMAKE_CXX_FLAGS "-march=native -fconstexpr-steps=10000000 -Wno-microsoft-template")
set(CMAKE_CXX_FLAGS_RELEASE "-Ofast -DNDEBUG")
set(CMAKE_CXX_FLAGS_RELWITHDEBINFO "${CMAKE_CXX_FLAGS_RELEASE} -g -D_DLL")

# set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fprofile-instr-generate")
# set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fprofile-use=${PROJECT_SOURCE_DIR}/tb6.profdata")

# clangd / IDE: compile_commands.json lives in the build dir; link from project root.
if(CMAKE_EXPORT_COMPILE_COMMANDS AND NOT PROJECT_SOURCE_DIR STREQUAL PROJECT_BINARY_DIR)
  set(_compile_commands_link "${PROJECT_SOURCE_DIR}/compile_commands.json")
  file(REMOVE "${_compile_commands_link}")
  file(RELATIVE_PATH _compile_commands_build_rel "${PROJECT_SOURCE_DIR}" "${PROJECT_BINARY_DIR}")
  file(CREATE_LINK "${_compile_commands_build_rel}/compile_commands.json" "${_compile_commands_link}" SYMBOLIC)
endif()
unset(_compile_commands_link)
unset(_compile_commands_build_rel)

set(CPACK_PROJECT_NAME ${PROJECT_NAME})
set(CPACK_PROJECT_VERSION ${PROJECT_VERSION})
include(CPack)
