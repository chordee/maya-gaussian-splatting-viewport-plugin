# Maya Gaussian Splatting Viewport Plugin

A C++ Maya plugin for real-time 3D Gaussian Splatting (`.ply`) rendering
in Autodesk Maya 2024 Viewport 2.0.

## Features

- Load standard 3DGS `.ply` files (position, rotation, scale, opacity, SH coefficients)
- View-dependent color via SH degree 0 and 1 (auto-detected from PLY)
- EWA Splatting: full GPU projection of 3D covariance to 2D ellipses
- GPU Bitonic Sort: depth sort skipped when camera is static (performance optimization)
- Maya scene integration: Reversed-Z depth test compatible, non-destructive
- Maya node attributes: `filePath`, `splatScale`, `opacityMult`

## Requirements

| Tool | Version |
| ---- | ------- |
| Autodesk Maya | 2024 |
| Maya DevKit | 2024 |
| MSVC | 2019 or 2022 |
| CMake | ≥ 3.20 |
| vcpkg | any version |

## Build

GLEW is declared in `vcpkg.json` and installed automatically on first configure
when `VCPKG_ROOT` is set. It is linked statically — no `glew32.dll` needed at runtime.

```bat
cmake -B build ^
    -DMAYA_DEVKIT="C:/Users/you/devkitBase2024" ^
    -DCMAKE_TOOLCHAIN_FILE="%VCPKG_ROOT%/scripts/buildsystems/vcpkg.cmake" ^
    -DVCPKG_TARGET_TRIPLET="x64-windows-static-md"
cmake --build build --config Release
```

Output is placed in `build/Release/`:

- `GaussianSplatPlugin.mll`
- `shaders/` directory (copied automatically by CMake)

## Installation

1. Copy `build/Release/GaussianSplatPlugin.mll` and `build/Release/shaders/`
   to your Maya plug-ins directory, or load directly from `build/Release/`.
2. **Set Viewport 2.0 to OpenGL Core Profile** — this plugin uses OpenGL and will
   not render under DirectX 11 (Maya's default on Windows):
   **Windows → Settings/Preferences → Preferences → Display → Viewport 2.0**
   → set *Rendering engine* to **OpenGL Core Profile (Compatibility)**.
   Restart Maya after changing this setting.
3. In Maya: **Windows → Settings/Preferences → Plug-in Manager**
   → load `GaussianSplatPlugin.mll`.

## Usage

```python
import maya.cmds as cmds
node = cmds.createNode("gaussianSplatNode")
cmds.setAttr(node + ".filePath", "/path/to/scene.ply", type="string")
```

Adjustable attributes:

- `splatScale` — global size multiplier (default `1.0`)
- `opacityMult` — opacity multiplier (default `1.0`)

## Architecture

```text
plugin.cpp              → initializePlugin / uninitializePlugin
GaussianNode            → MPxLocatorNode, owns SplatData
GaussianDrawOverride    → MPxDrawOverride, manages OpenGL state
GaussianRenderer        → OpenGL VAO/SSBO, shader management, sort, draw
PlyLoader               → tinyply wrapper, parses .ply, converts scale/opacity
src/shaders/
  gaussian.vert/frag    → EWA splatting + pre-multiplied alpha
  depth.comp            → per-splat camera depth calculation
  sort.comp             → GPU Bitonic Sort
third_party/tinyply     → PLY parsing library
```

## Known Limitations

- SH degree 2 and 3 (higher-frequency view-dependent color) are not yet implemented.
- Large scenes (3M+ splats) require ~250 GPU dispatches per frame when the camera
  moves, which may trigger TDR on lower-end GPUs.

## License

MIT — see [LICENSE](LICENSE)
