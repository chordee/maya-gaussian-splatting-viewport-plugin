# Maya Gaussian Splatting Viewport Plugin

在 Autodesk Maya 2024 Viewport 2.0 中即時渲染 3D Gaussian Splatting（.ply 格式）的 C++ 插件。

![截圖](screenshot/Annotation%202026-03-20%20020134.jpg)

## 功能

- 載入標準 3DGS `.ply` 檔案（位置、旋轉、縮放、不透明度、SH DC 係數）
- EWA Splatting：在 GPU 上完整投影 3D 協方差矩陣為 2D 橢圓
- GPU Bitonic Sort：每幀依相機深度排序以正確混合透明度
- 與 Maya 場景整合：支援 Reversed-Z depth test，不影響其他物件的渲染
- Maya 節點屬性：`filePath`、`splatScale`、`opacityMult`

## 需求

| 工具 | 版本 |
| ---- | ---- |
| Autodesk Maya | 2024 |
| Maya DevKit | 2024 |
| MSVC | 2019 或 2022 |
| CMake | ≥ 3.20 |
| vcpkg + GLEW | x64-windows |

## 建置

```bat
:: 1. 安裝 GLEW（若尚未安裝）
vcpkg install glew:x64-windows

:: 2. 設定並建置
cmake -B build -DMAYA_DEVKIT="C:/Users/<user>/devkitBase2024"
cmake --build build --config Release
```

建置完成後，輸出位於 `build/Release/`：

- `GaussianSplatPlugin.mll`
- `shaders/` 目錄（自動複製）

## 安裝

1. 複製 `build/Release/GaussianSplatPlugin.mll` 與 `build/Release/shaders/`
   到 Maya 插件目錄，或直接從 `build/Release/` 載入。
2. 確認 `glew32.dll` 位於相同目錄或系統 PATH 中。
3. 在 Maya 中：**Windows → Settings/Preferences → Plug-in Manager** → 載入 `GaussianSplatPlugin.mll`。

## 使用

```python
# 執行 scripts/create_gaussian_splat.py，或手動建立節點：
import maya.cmds as cmds
node = cmds.createNode("gaussianSplatNode")
cmds.setAttr(node + ".filePath", "/path/to/scene.ply", type="string")
```

可調整屬性：

- `splatScale`：整體縮放倍數（預設 1.0）
- `opacityMult`：不透明度乘數（預設 1.0）

## 架構

```text
plugin.cpp              → initializePlugin / uninitializePlugin
GaussianNode            → MPxLocatorNode，持有 SplatData
GaussianDrawOverride    → MPxDrawOverride，管理 OpenGL 狀態
GaussianRenderer        → OpenGL VAO/SSBO、Shader 管理、Sort、Draw
PlyLoader               → tinyply 包裝，解析 .ply 並轉換 scale/opacity
src/shaders/
  gaussian.vert/frag    → EWA splatting + pre-multiplied alpha
  depth.comp            → 計算每個 splat 的相機深度
  sort.comp             → GPU Bitonic Sort
third_party/tinyply     → PLY 解析庫
```

## 已知限制

- 目前只使用 SH degree 0（DC 項），尚未實作視角相依色彩（degree 1+）
- 大場景（3M+ splats）每幀需約 250 次 GPU dispatch，低階顯卡可能觸發 TDR
- 尚未與 Maya 不透明幾何體做完整的 Z-occlusion 整合

## 授權

MIT
