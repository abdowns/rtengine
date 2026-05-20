cmake_minimum_required(VERSION 3.24)
project(rtengine_consumer LANGUAGES CXX)
find_package(rtengine CONFIG REQUIRED)

add_executable(minimal main.cpp)
target_link_libraries(minimal PRIVATE rtengine::core)
