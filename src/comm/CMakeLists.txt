cmake_minimum_required(VERSION 3.25)

project(comm CXX)

set(OBJ_NAME lge-${PROJECT_NAME}-static)

file(GLOB_RECURSE SRC ${CMAKE_CURRENT_SOURCE_DIR}/src/*.c)
file(GLOB_RECURSE HDR ${CMAKE_CURRENT_SOURCE_DIR}/src/*.h)

add_library(${OBJ_NAME} STATIC ${SRC} ${HDR})

target_include_directories(${OBJ_NAME} PUBLIC include)
target_link_libraries(${OBJ_NAME}
    PUBLIC
        pthread
)

target_compile_options(${OBJ_NAME} PRIVATE -D_XOPEN_SOURCE=600 -std=c++17)
