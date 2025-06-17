cmake_minimum_required(VERSION 3.25)

project(comm C)

set(OBJ_NAME lge-${PROJECT_NAME}-static)

file(GLOB_RECURSE SRC ${CMAKE_CURRENT_SOURCE_DIR}/src/*.c)
file(GLOB_RECURSE HDR ${CMAKE_CURRENT_SOURCE_DIR}/src/*.h)

add_library(${OBJ_NAME} STATIC ${SRC} ${HDR})

target_include_directories(${OBJ_NAME} PUBLIC include)
target_link_libraries(${OBJ_NAME}
    PUBLIC
        pthread
)
