cmake_minimum_required(VERSION 3.9.4)
project(tb VERSION 0.1.0)
set(CMAKE_VERBOSE_MAKEFILE ON)

file(GLOB source_files ${PROJECT_SOURCE_DIR}/src/*.cpp)

set(CMAKE_CPP_COMPILER clang++)

add_executable(tb ${source_files})
target_compile_features(tb PUBLIC cxx_std_20)

set(LZ4_BUNDLED_MODE ON)
set(LZ4_BUILD_CLI OFF)

add_subdirectory(mimalloc)

add_subdirectory(lz4/build/cmake)
target_link_libraries(tb lz4_static mimalloc-static)
target_include_directories(tb PRIVATE ${PROJECT_SOURCE_DIR}/lz4/lib)

add_compile_options("-stdlib=libc++" "-lc++abi" "-v")

set_property(TARGET tb PROPERTY CXX_STANDARD 20)
set_property(TARGET tb PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
set(CMAKE_CXX_FLAGS "-march=native -fconstexpr-steps=10000000 -Wno-microsoft-template")
set(CMAKE_CXX_FLAGS_RELEASE "-Ofast -DNDEBUG")
set(CMAKE_CXX_FLAGS_RELWITHDEBINFO "${CMAKE_CXX_FLAGS_RELEASE} -g -gcodeview -D_DLL")

set(CMAKE_CROSSCOMPILING TRUE)
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR x86_64)

# # specify the cross compiler
# set(CMAKE_C_COMPILER_TARGET "x86_64-linux-gnu")
# set(CMAKE_C_COMPILER clang)
# set(CMAKE_CXX_COMPILER_TARGET "x86_64-linux-gnu")
# set(CMAKE_CXX_COMPILER clang++)
# set(CMAKE_ASM_COMPILER_TARGET "x86_64-linux-gnu")
# set(CMAKE_ASM_COMPILER clang)

# set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fprofile-instr-generate")
# set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fprofile-use=${PROJECT_SOURCE_DIR}/tb6.profdata")

set(CPACK_PROJECT_NAME ${PROJECT_NAME})
set(CPACK_PROJECT_VERSION ${PROJECT_VERSION})
include(CPack) 
 