# Root CMakeLists.txt for scikit-build-core
# This just includes the actual CMakeLists.txt in cpp/
cmake_minimum_required(VERSION 3.15)
project(rkyv LANGUAGES CXX)

# Include the actual build configuration
add_subdirectory(cpp)
