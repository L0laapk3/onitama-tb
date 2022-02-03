cmake_minimum_required(VERSION 3.9.4)
project(tb VERSION 0.1.0)
set(CMAKE_VERBOSE_MAKEFILE ON)

set(source_files
${PROJECT_SOURCE_DIR}/src/Index.cpp
${PROJECT_SOURCE_DIR}/src/Card.cpp
${PROJECT_SOURCE_DIR}/src/Board.cpp
${PROJECT_SOURCE_DIR}/src/TableBase.cpp
${PROJECT_SOURCE_DIR}/src/TableBaseCompress.cpp
${PROJECT_SOURCE_DIR}/src/Main.cpp
)

set(CMAKE_CPP_COMPILER clang++)

add_executable(tb ${source_files})
target_compile_features(tb PUBLIC cxx_std_20)
target_include_directories(tb PRIVATE ${PROJECT_SOURCE_DIR}/Include)

#target_include_directories(tb PUBLIC include)

set_property(TARGET tb PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
set(CMAKE_CXX_FLAGS "-march=native -fconstexpr-steps=10000000")
set(CMAKE_CXX_FLAGS_RELEASE "-Ofast -DNDEBUG -D_MT -Xclang --dependent-lib=msvcrt")
set(CMAKE_CXX_FLAGS_RELWITHDEBINFO "${CMAKE_CXX_FLAGS_RELEASE} -g -gcodeview -D_DLL")

# set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fprofile-instr-generate")
# set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fprofile-use=${PROJECT_SOURCE_DIR}/tb6.profdata")

set(CPACK_PROJECT_NAME ${PROJECT_NAME})
set(CPACK_PROJECT_VERSION ${PROJECT_VERSION})
include(CPack) 
 