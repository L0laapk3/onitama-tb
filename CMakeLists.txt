cmake_minimum_required(VERSION 3.0.0)
project(Onitama-TB VERSION 0.1.0)
set(CMAKE_VERBOSE_MAKEFILE ON)

set(source_files
${PROJECT_SOURCE_DIR}/src/Index.cpp
${PROJECT_SOURCE_DIR}/src/Main.cpp
)

set(CMAKE_CPP_COMPILER clang++)

add_executable(Onitama-TB ${source_files})
target_compile_features(Onitama-TB PUBLIC cxx_std_20)
target_include_directories(Onitama-TB PRIVATE ${PROJECT_SOURCE_DIR}/Include)

#target_include_directories(Onitama-TB PUBLIC include)

set(CMAKE_CXX_FLAGS "-march=native")
set(CMAKE_CXX_FLAGS_RELEASE "-Ofast -DNDEBUG -D_MT -Xclang --dependent-lib=msvcrt -Wmicrosoft-goto")
set(CMAKE_CXX_FLAGS_RELWITHDEBINFO "-Ofast -g -DNDEBUG -Xclang -gcodeview -D_DLL -D_MT -Xclang --dependent-lib=msvcrt -Wmicrosoft-goto")

set(CPACK_PROJECT_NAME ${PROJECT_NAME})
set(CPACK_PROJECT_VERSION ${PROJECT_VERSION})
include(CPack) 
 