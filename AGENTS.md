# RacingGame

- Native C++17 racing game built with CMake, GLFW, and OpenGL. `src/car.cpp` loads Blender GLBs, `src/driving.cpp` simulates the cars, and `src/renderer.cpp` draws them.
- Build with `cmake --build build --config Release` and test with `ctest --test-dir build -C Release --output-on-failure`. Use the CMake bundled with Visual Studio 2026 for the existing Visual Studio 18 build directory.
- Blender source is `C:/Programming/CarGame/cars.blend`. Keep the original SlimRider export at `assets/cars.glb` as a fallback. Install the combined export at `assets/gale.glb`. Choose the car, customize it in the garage, then pick a map under **Play**.
- Parent Gale's stock mirrors, emblems, exhaust, and engine parts under `Body.001`. Use `FrontLeftWheel.001` and `RearLeftWheel.001` for Gale's hubs. Export the full `GaleWheel` assembly and SlimRider's `SimpleWheel`. Keep the rim faces toward Blender +Y and in the Blender Y=0 plane.
- Keep customizable paint assigned to `Paint` and preserve the named `HoodIntake`, `Wing`, `Splitter`, and exhaust marker empties. In the combined export, use the `.001` add-on meshes and the two rear `Exhaust.001` and `Exhaust.002` markers for Gale. SlimRider uses the center `Exhaust` marker and the side `DieselExhaust` marker. Gale's `Diesel` mesh swaps into the engine bay and uses its rear outlets.
- Export as glTF Binary (.glb) with Y Up, Apply Modifiers, and Custom Properties enabled. The game uses +X forward, +Y up, and +Z for left-side hubs. Gale is a mid-engine flat-plane V8 with 30:70 front-to-rear AWD.
- Arrow keys, Enter, and Esc navigate menus. A mapped controller uses its D-pad or left stick, A to confirm, and B to go back. Settings stores separate gamepad and keyboard driving bindings. Hold B to cancel a gamepad rebind.

## WebAssembly playtest

With [Emscripten](https://emscripten.org/docs/getting_started/downloads.html), CMake, and Ninja on PATH, build and serve the WebGL 2 version from the repository root.

```sh
emcmake cmake -S . -B build/web -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build/web
python -m http.server 8765 --bind 127.0.0.1 --directory build/web/bin/Release
```

Open [the local playtest](http://127.0.0.1:8765/) and click the canvas to focus it. The WebAssembly build preloads the GLBs, so rebuild when an asset changes.

GitHub Actions rebuilds the playtest when the game changes on `main`, uploads `racinggame-web`, and deploys the browser files to [GitHub Pages](https://joelweikel.github.io/RacingGame/). Run **WebAssembly playtest** manually from `main` to redeploy it.
