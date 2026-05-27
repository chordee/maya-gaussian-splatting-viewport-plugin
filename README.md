# Maya Gaussian Splatting Viewport Plugin

[![Build Plugin](https://github.com/chordee/maya-gaussian-splatting-viewport-plugin/actions/workflows/build.yml/badge.svg)](https://github.com/chordee/maya-gaussian-splatting-viewport-plugin/actions/workflows/build.yml)

A C++ Maya plugin for real-time 3D Gaussian Splatting (`.ply`) rendering
in Autodesk Maya Viewport 2.0.

## Features

- Load standard 3DGS `.ply` files (position, rotation, scale, opacity, SH coefficients)
- View-dependent color via SH degrees 0–3 (auto-detected from PLY, runtime-capped via attribute)
- EWA Splatting: full GPU projection of 3D covariance to 2D ellipses
- GPU Bitonic Sort: depth sort skipped when camera is static (performance optimization)
- Maya scene integration: Reversed-Z depth test compatible, non-destructive
- Maya node attributes: `filePath`, `splatScale`, `opacityMult`, `shDegree`

## Requirements

| Tool | Version |
| ---- | ------- |
| Autodesk Maya | 2024, 2025, 2026, or 2027 |
| Maya DevKit | matching the Maya version |
| MSVC | 2022 (or 2019 for Maya 2024–2026) |
| CMake | ≥ 3.20 |
| vcpkg | any version |

## Pre-built binaries

Each tagged release on the
[Releases page](https://github.com/chordee/maya-gaussian-splatting-viewport-plugin/releases)
ships a per-version zip:

- `GaussianSplatPlugin-maya2024-<tag>.zip`
- `GaussianSplatPlugin-maya2025-<tag>.zip`
- `GaussianSplatPlugin-maya2026-<tag>.zip`
- `GaussianSplatPlugin-maya2027-<tag>.zip`

Download the zip matching your Maya version, unzip, and skip to
[Installation](#installation). Compiling from source (below) is only needed
if you want to modify the plugin or target a Maya version that is not in
the release matrix.

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

For Maya 2027, append `-DCMAKE_CXX_STANDARD=20` to the `cmake -B build`
line — Maya 2027's DevKit requires C++20. Maya 2024–2026 default to C++17.

Output is placed in `build/Release/`:

- `GaussianSplatPlugin.mll`
- `shaders/` directory (copied automatically by CMake)

## Installation

1. Copy `GaussianSplatPlugin.mll` and `shaders/` to your Maya plug-ins
   directory (or load directly from where they sit). The `.mll` is built
   against a specific Maya version's DevKit and is **not interchangeable**
   across Maya versions — use the zip or source build matching your Maya.
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
- `shDegree`   — SH evaluation cap, `0`–`3` (default `3`). Capped at the highest
  degree present in the loaded PLY; lower it to trade view-dependent fidelity
  for vertex-shader cost.

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

- Large scenes (3M+ splats) require ~250 GPU dispatches per frame when the camera
  moves, which may trigger TDR on lower-end GPUs.

## License

MIT — see [LICENSE](LICENSE)
