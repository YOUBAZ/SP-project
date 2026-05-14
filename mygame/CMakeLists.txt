cmake_minimum_required(VERSION 3.19)
project(SP-PROJECT)

# Find SFML 3.0 components
find_package(SFML 3 COMPONENTS Graphics Audio Network REQUIRED)

add_executable(SP-PROJECT mygame.cpp)

# Link using namespaced targets
target_link_libraries(SP-PROJECT PRIVATE SFML::Graphics SFML::Audio SFML::Network)
