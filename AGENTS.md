# RacingGame

- Native C++17 racing game built with CMake, GLFW, and OpenGL. `src/car.cpp` loads Blender GLBs, `src/driving.cpp` simulates the cars, and `src/renderer.cpp` draws them.
- Build with `cmake --build build --config Release` and test with `ctest --test-dir build -C Release --output-on-failure`. Use the CMake bundled with Visual Studio 2026 for the existing Visual Studio 18 build directory.
- Blender source is `C:/Programming/CarGame/cars.blend`. Keep the original SlimRider export at `assets/cars.glb` and install Gale exports at `assets/gale.glb`. Press 1 for SlimRider or 2 for Gale.
- For a Gale-only export, select the Gale collection, both wheel empties, and the shared `SimpleWheel` from Rims. Blender .001 suffixes are supported. For a combined export, parent Gale's mirrors, emblems, exhaust, and engine parts under `Body.001`, and use `FrontLeftWheel.001` and `RearLeftWheel.001` for Gale's hubs.
- Export as glTF Binary (.glb) with Y Up, Apply Modifiers, and Custom Properties enabled. The game uses +X forward, +Y up, and +Z for left-side hubs. Gale is a mid-engine flat-plane V8 with 30:70 front-to-rear AWD.

## WebAssembly playtest

With [Emscripten](https://emscripten.org/docs/getting_started/downloads.html), CMake, and Ninja on PATH, build and serve the WebGL 2 version from the repository root.

```sh
emcmake cmake -S . -B build/web -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build/web
python -m http.server 8765 --bind 127.0.0.1 --directory build/web/bin/Release
```

Open [the local playtest](http://127.0.0.1:8765/) and click the canvas to focus it. The WebAssembly build preloads the GLBs, so rebuild when an asset changes.

GitHub Actions builds the same game for changes on `main` and uploads `racinggame-web`. To publish on a Pages-eligible repository, set the Pages source to **GitHub Actions**, then run **WebAssembly playtest** from `main` with **Publish the compiled game and bundled car assets to GitHub Pages** enabled.
