# rtengine

`rtengine` is a lightweight C++20 OpenGL 4.1 engine with PBR, ray tracing, asset importing, and physics.

## Setup

Requirements: CMake 3.24+, OpenGL 4.1

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --parallel
./build/rtengine_demo
```

On Windows, run `build/Release/rtengine_demo.exe`.

Dependencies are fetched automatically with CMake FetchContent.

To use the library from CMake:

```cmake
add_subdirectory(path/to/rtengine)
target_link_libraries(my_app PRIVATE rtengine::rtengine)
```
