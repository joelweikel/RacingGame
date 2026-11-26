# RacingGame

- Native C++17 racing game built with CMake, GLFW, and OpenGL. `src/car.cpp` loads Blender GLBs, `src/driving.cpp` simulates the cars, and `src/renderer.cpp` draws them.
- Build with `cmake --build build --config Release` and test with `ctest --test-dir build -C Release --output-on-failure`. Use the CMake bundled with Visual Studio 2026 for the existing Visual Studio 18 build directory.
- Blender source is `C:/Programming/CarGame/cars.blend`. Keep the original SlimRider export at `assets/cars.glb` and install Gale exports at `assets/gale.glb`. Press 1 for SlimRider or 2 for Gale.
- For a Gale-only export, select the Gale collection, both wheel empties, and the shared `SimpleWheel` from Rims. Blender .001 suffixes are supported. For a combined export, parent Gale's mirrors, emblems, exhaust, and engine parts under `Body.001`, and use `FrontLeftWheel.001` and `RearLeftWheel.001` for Gale's hubs.
- Export as glTF Binary (.glb) with Y Up, Apply Modifiers, and Custom Properties enabled. The game uses +X forward, +Y up, and +Z for left-side hubs. Gale is a mid-engine flat-plane V8 with 30:70 front-to-rear AWD.
