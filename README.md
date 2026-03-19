# Maya Gaussian Splatting Viewport Plugin

A C++ Maya plugin for real-time 3D Gaussian Splatting (`.ply`) rendering
in Autodesk Maya 2024 Viewport 2.0.

![Screenshot](screenshot/Annotation%202026-03-20%20020134.jpg)

## Features

- Load standard 3DGS `.ply` files (position, rotation, scale, opacity, SH DC)
- EWA Splatting: full GPU projection of 3D covariance to 2D ellipses
- GPU Bitonic Sort: per-frame depth sort for correct transparency blending
- Maya scene integration: Reversed-Z depth test compatible, non-destructive
- Maya node attributes: `filePath`, `splatScale`, `opacityMult`

## Requirements

| Tool | Version |
| ---- | ------- |
| Autodesk Maya | 2024 |
| Maya DevKit | 2024 |
| MSVC | 2019 or 2022 |
| CMake | ≥ 3.20 |
| vcpkg + GLEW | x64-windows |

## Build

```bat
:: 1. Install GLEW (if not already installed)
vcpkg install glew:x64-windows

:: 2. Configure and build
cmake -B build -DMAYA_DEVKIT="C:/Users/<user>/devkitBase2024"
cmake --build build --config Release
```

Output is placed in `build/Release/`:

- `GaussianSplatPlugin.mll`
- `shaders/` directory (copied automatically by CMake)

## Installation

1. Copy `build/Release/GaussianSplatPlugin.mll` and `build/Release/shaders/`
   to your Maya plug-ins directory, or load directly from `build/Release/`.
2. Ensure `glew32.dll` is in the same directory or on the system `PATH`.
3. In Maya: **Windows → Settings/Preferences → Plug-in Manager**
   → load `GaussianSplatPlugin.mll`.

## Usage

```python
# Run scripts/create_gaussian_splat.py, or create the node manually:
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

- Only SH degree 0 (DC term) is used; view-dependent color (degree 1+)
  is not yet implemented.
- Large scenes (3M+ splats) require ~250 GPU dispatches per frame,
  which may trigger TDR on lower-end GPUs.
- Z-occlusion against Maya opaque geometry is not fully integrated.

## License

MIT — see [LICENSE](LICENSE)
