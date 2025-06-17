cmake_minimum_required(VERSION 3.25)

project(Flight-Tracker CXX)

set(PROJECT_ROOT_DIR ${CMAKE_CURRENT_SOURCE_DIR})

set(CMAKE_VERBOSE_MAKEFILE on)

add_subdirectory(src/ADS-B)
add_subdirectory(src/SDR)
add_subdirectory(src/comm)
add_subdirectory(src/system)

add_executable(${PROJECT_NAME} ${CMAKE_CURRENT_SOURCE_DIR}/src/FlightTrackerMain.cpp)

target_link_libraries(${PROJECT_NAME}
    PRIVATE
        pthread
)

target_compile_options(${PROJECT_NAME} PRIVATE -D_XOPEN_SOURCE=600 -std=c++17)

install(TARGETS ${PROJECT_NAME} RUNTIME DESTINATION ${CMAKE_BINARY_DIR}/${PROJECT_NAME}Pkg/bin)
