# Maya Gaussian Splatting Viewport Plugin — Implementation Blueprint

## Project Overview

Build a Maya 2024/2025 C++ plugin that loads a 3D Gaussian Splatting `.ply` file and renders it
correctly inside Viewport 2.0 using GPU instancing, GPU bitonic sort, and Spherical Harmonics
(degree 1, camera-dependent color). Target platform: Windows x64.

---

## Constraints & Non-Goals

- **Render output**: NOT required. Viewport display only.
- **Maya version**: 2024 and 2025 only. No legacy viewport support.
- **Platform**: Windows x64, MSVC toolchain.
- **SH degree**: Implement degree 0 (base color) + degree 1 (view-dependent tint). Degree 2+ not required.
- **No USD/Hydra**: Pure MPxLocatorNode + MPxDrawOverride approach.
- **No Python draw code**: Python for MEL commands and UI only. All draw/GPU code in C++.

---

## Repository Layout

```
GaussianSplatPlugin/
├── CMakeLists.txt
├── src/
│   ├── plugin.cpp                  # initializePlugin / uninitializePlugin
│   ├── GaussianNode.h/.cpp         # MPxLocatorNode
│   ├── GaussianDrawOverride.h/.cpp # MPxDrawOverride
│   ├── GaussianRenderer.h/.cpp     # OpenGL buffer management + draw
│   ├── PlyLoader.h/.cpp            # .ply parser (wraps tinyply)
│   ├── DepthSorter.h/.cpp          # GPU bitonic sort via compute shader
│   └── shaders/
│       ├── gaussian.vert
│       ├── gaussian.frag
│       └── sort.comp               # Bitonic sort compute shader
├── third_party/
│   └── tinyply/
│       └── tinyply.h               # Header-only, include as-is
└── scripts/
    └── gaussian_shelf.py           # Maya shelf button to load .ply
```

---

## Dependencies

| Dependency | Version | Notes |
|---|---|---|
| Maya DevKit | 2024 or 2025 | Match installed Maya version |
| MSVC | 2019 or 2022 | `/std:c++17` |
| CMake | 3.20+ | |
| tinyply | 2.3.4 | Header-only, commit to repo |
| OpenGL | 4.5+ | Already available via Maya |
| GLEW | bundled in Maya | Link against Maya's GLEW |

---

## Part 1: CMakeLists.txt

```cmake
cmake_minimum_required(VERSION 3.20)
project(GaussianSplatPlugin)

set(CMAKE_CXX_STANDARD 17)
set(MAYA_ROOT "C:/Program Files/Autodesk/Maya2024" CACHE PATH "Maya install root")

file(GLOB_RECURSE SOURCES "src/*.cpp")

add_library(GaussianSplatPlugin SHARED ${SOURCES})

target_include_directories(GaussianSplatPlugin PRIVATE
    ${MAYA_ROOT}/include
    third_party/tinyply
    src
)

target_link_directories(GaussianSplatPlugin PRIVATE
    ${MAYA_ROOT}/lib
)

target_link_libraries(GaussianSplatPlugin PRIVATE
    OpenMaya
    OpenMayaUI
    OpenMayaRender
    Foundation
    clew          # Maya's bundled GLEW
)

set_target_properties(GaussianSplatPlugin PROPERTIES
    SUFFIX ".mll"
    PREFIX ""
)

target_compile_definitions(GaussianSplatPlugin PRIVATE
    NT_PLUGIN
    REQUIRE_IOSTREAM
    _USE_MATH_DEFINES
)
```

---

## Part 2: PlyLoader

### PlyLoader.h

```cpp
#pragma once
#include <vector>
#include <string>

struct SplatData {
    // Per-splat GPU data (layout matches GPU buffer)
    struct Splat {
        float px, py, pz;           // position
        float rot_w, rot_x, rot_y, rot_z;  // quaternion (w first)
        float sx, sy, sz;           // log-scale (exp before upload)
        float opacity;              // pre-sigmoid
        float sh[12];               // SH degree 0 (3 floats) + degree 1 (9 floats)
    };

    std::vector<Splat> splats;
    bool load(const std::string& path, std::string& errorOut);
};
```

### PlyLoader.cpp

```cpp
#include "PlyLoader.h"
#include "tinyply.h"
#include <fstream>
#include <cmath>

bool SplatData::load(const std::string& path, std::string& errorOut) {
    try {
        std::ifstream file(path, std::ios::binary);
        if (!file) { errorOut = "Cannot open file: " + path; return false; }

        tinyply::PlyFile ply;
        ply.parse_header(file);

        auto pos      = ply.request_properties_from_element("vertex", {"x","y","z"});
        auto rot      = ply.request_properties_from_element("vertex", {"rot_0","rot_1","rot_2","rot_3"});
        auto scale    = ply.request_properties_from_element("vertex", {"scale_0","scale_1","scale_2"});
        auto opac     = ply.request_properties_from_element("vertex", {"opacity"});
        auto sh_dc    = ply.request_properties_from_element("vertex", {"f_dc_0","f_dc_1","f_dc_2"});
        // SH degree 1: 9 coefficients
        std::shared_ptr<tinyply::PlyData> sh_rest;
        try {
            sh_rest = ply.request_properties_from_element("vertex",
                {"f_rest_0","f_rest_1","f_rest_2",
                 "f_rest_3","f_rest_4","f_rest_5",
                 "f_rest_6","f_rest_7","f_rest_8"});
        } catch (...) { /* optional */ }

        ply.read(file);

        const size_t N = pos->count;
        splats.resize(N);

        auto* P  = reinterpret_cast<float*>(pos->buffer.get());
        auto* R  = reinterpret_cast<float*>(rot->buffer.get());
        auto* S  = reinterpret_cast<float*>(scale->buffer.get());
        auto* O  = reinterpret_cast<float*>(opac->buffer.get());
        auto* DC = reinterpret_cast<float*>(sh_dc->buffer.get());

        for (size_t i = 0; i < N; ++i) {
            auto& sp = splats[i];
            sp.px = P[i*3+0]; sp.py = P[i*3+1]; sp.pz = P[i*3+2];
            // quaternion: tinyply stores w,x,y,z
            sp.rot_w = R[i*4+0]; sp.rot_x = R[i*4+1];
            sp.rot_y = R[i*4+2]; sp.rot_z = R[i*4+3];
            // scale: stored as log, convert to exp
            sp.sx = std::exp(S[i*3+0]);
            sp.sy = std::exp(S[i*3+1]);
            sp.sz = std::exp(S[i*3+2]);
            // opacity: stored as log-odds (pre-sigmoid)
            sp.opacity = 1.0f / (1.0f + std::exp(-O[i]));
            // SH dc band
            sp.sh[0] = DC[i*3+0];
            sp.sh[1] = DC[i*3+1];
            sp.sh[2] = DC[i*3+2];
            // SH degree 1 (9 floats)
            if (sh_rest) {
                auto* REST = reinterpret_cast<float*>(sh_rest->buffer.get());
                for (int k = 0; k < 9; ++k)
                    sp.sh[3+k] = REST[i*9+k];
            }
        }
        return true;
    } catch (const std::exception& e) {
        errorOut = e.what();
        return false;
    }
}
```

---

## Part 3: GaussianNode

### GaussianNode.h

```cpp
#pragma once
#include <maya/MPxLocatorNode.h>
#include <maya/MObject.h>
#include "PlyLoader.h"

class GaussianNode : public MPxLocatorNode {
public:
    static MTypeId  typeId;
    static MString  typeName;

    static MObject  aFilePath;
    static MObject  aSplatScale;   // uniform scale multiplier for splat size
    static MObject  aOpacityMult;  // global opacity multiplier

    SplatData       splatData;
    bool            dirty = true;
    MString         loadedPath;

    static void*    creator();
    static MStatus  initialize();

    void            postConstructor() override;
    MStatus         compute(const MPlug&, MDataBlock&) override;
    bool            isBounded() const override { return true; }
    MBoundingBox    boundingBox() const override;

    // Called when filePath attribute changes
    MStatus         setDependentsDirty(const MPlug& plug, MPlugArray& plugArray) override;
};
```

### GaussianNode.cpp

```cpp
#include "GaussianNode.h"
#include <maya/MFnTypedAttribute.h>
#include <maya/MFnNumericAttribute.h>
#include <maya/MFnStringData.h>
#include <maya/MGlobal.h>
#include <maya/MBoundingBox.h>
#include <maya/MDataHandle.h>
#include <limits>

MTypeId GaussianNode::typeId(0x0013BE00);  // Register a unique ID at Autodesk
MString GaussianNode::typeName("gaussianSplatNode");

MObject GaussianNode::aFilePath;
MObject GaussianNode::aSplatScale;
MObject GaussianNode::aOpacityMult;

void* GaussianNode::creator() { return new GaussianNode; }

MStatus GaussianNode::initialize() {
    MFnTypedAttribute   tAttr;
    MFnNumericAttribute nAttr;
    MFnStringData       strData;

    MObject defaultStr = strData.create("");
    aFilePath = tAttr.create("filePath", "fp", MFnData::kString, defaultStr);
    tAttr.setKeyable(false);
    tAttr.setStorable(true);

    aSplatScale = nAttr.create("splatScale", "ss", MFnNumericData::kFloat, 1.0);
    nAttr.setKeyable(true);
    nAttr.setMin(0.01); nAttr.setMax(10.0);

    aOpacityMult = nAttr.create("opacityMult", "om", MFnNumericData::kFloat, 1.0);
    nAttr.setKeyable(true);
    nAttr.setMin(0.0); nAttr.setMax(1.0);

    addAttribute(aFilePath);
    addAttribute(aSplatScale);
    addAttribute(aOpacityMult);

    return MS::kSuccess;
}

void GaussianNode::postConstructor() {
    // Nothing needed at construction; load triggered by attribute change
}

MStatus GaussianNode::compute(const MPlug& plug, MDataBlock& block) {
    return MS::kUnknownParameter;
}

MStatus GaussianNode::setDependentsDirty(const MPlug& plug, MPlugArray& plugArray) {
    if (plug == aFilePath) {
        dirty = true;
        // Trigger redraw
        MHWRender::MRenderer::setGeometryDrawDirty(thisMObject());
    }
    return MPxNode::setDependentsDirty(plug, plugArray);
}

MBoundingBox GaussianNode::boundingBox() const {
    if (splatData.splats.empty())
        return MBoundingBox(MPoint(-1,-1,-1), MPoint(1,1,1));

    float mn[3] = { FLT_MAX,  FLT_MAX,  FLT_MAX };
    float mx[3] = {-FLT_MAX, -FLT_MAX, -FLT_MAX };
    for (auto& s : splatData.splats) {
        mn[0] = std::min(mn[0], s.px); mx[0] = std::max(mx[0], s.px);
        mn[1] = std::min(mn[1], s.py); mx[1] = std::max(mx[1], s.py);
        mn[2] = std::min(mn[2], s.pz); mx[2] = std::max(mx[2], s.pz);
    }
    return MBoundingBox(MPoint(mn[0],mn[1],mn[2]), MPoint(mx[0],mx[1],mx[2]));
}
```

---

## Part 4: GaussianRenderer (OpenGL Buffer Management)

### GaussianRenderer.h

```cpp
#pragma once
#include "PlyLoader.h"
#include <GL/gl.h>
#include <cstdint>

class GaussianRenderer {
public:
    GaussianRenderer();
    ~GaussianRenderer();

    void uploadSplats(const SplatData& data);
    void sort(const float* viewMatrix);   // triggers GPU bitonic sort
    void draw(const float* mvp,
              const float* camPos,
              float splatScale,
              float opacityMult,
              int   shDegree);

    bool isReady() const { return vao_ != 0 && splatCount_ > 0; }
    int  splatCount() const { return splatCount_; }

private:
    void initGL();
    void buildShaderProgram();
    void buildSortProgram();
    void destroyGL();

    GLuint vao_         = 0;
    GLuint splatBuf_    = 0;   // SSBO: per-splat data
    GLuint indexBuf_    = 0;   // SSBO: sorted indices (uint32)
    GLuint keyBuf_      = 0;   // SSBO: sort keys (float depth)
    GLuint drawProgram_ = 0;
    GLuint sortProgram_ = 0;
    GLuint quadVBO_     = 0;   // unit quad [-1,1]

    int    splatCount_  = 0;
};
```

### GaussianRenderer.cpp — Key sections

```cpp
// ---------------------------------------------------------------
// Quad VBO: two triangles forming a [-1,1] billboard
// ---------------------------------------------------------------
static const float kQuad[] = {
    -1,-1,  1,-1,  1,1,
    -1,-1,  1, 1, -1,1
};

void GaussianRenderer::uploadSplats(const SplatData& data) {
    if (vao_ == 0) initGL();
    splatCount_ = static_cast<int>(data.splats.size());

    // Upload splat data as SSBO
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, splatBuf_);
    glBufferData(GL_SHADER_STORAGE_BUFFER,
        splatCount_ * sizeof(SplatData::Splat),
        data.splats.data(), GL_STATIC_DRAW);

    // Initialize index buffer: [0, 1, 2, ..., N-1]
    std::vector<uint32_t> indices(splatCount_);
    std::iota(indices.begin(), indices.end(), 0);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, indexBuf_);
    glBufferData(GL_SHADER_STORAGE_BUFFER,
        splatCount_ * sizeof(uint32_t),
        indices.data(), GL_DYNAMIC_DRAW);

    // Initialize key buffer
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, keyBuf_);
    glBufferData(GL_SHADER_STORAGE_BUFFER,
        splatCount_ * sizeof(float), nullptr, GL_DYNAMIC_DRAW);
}
```

---

## Part 5: Shaders

### gaussian.vert

```glsl
#version 450

// Per-vertex (quad corners)
layout(location = 0) in vec2 quadPos;

// SSBOs
struct Splat {
    vec3  position;
    vec4  rotation;   // quaternion (w,x,y,z)
    vec3  scale;
    float opacity;
    float sh[12];     // SH dc(3) + degree1(9)
};
layout(std430, binding = 0) readonly buffer SplatBuffer  { Splat splats[]; };
layout(std430, binding = 1) readonly buffer IndexBuffer  { uint  indices[]; };

// Uniforms
uniform mat4  u_mvp;
uniform mat4  u_mv;        // model-view (for screen-space covariance)
uniform mat4  u_proj;
uniform vec3  u_camPos;    // world-space camera position
uniform float u_splatScale;
uniform int   u_viewport[4]; // x, y, width, height

out vec2  v_uv;
out vec4  v_color;
out float v_opacity;

mat3 quatToMat(vec4 q) {
    float w=q.x, x=q.y, y=q.z, z=q.w;
    return mat3(
        1-2*(y*y+z*z), 2*(x*y+w*z),   2*(x*z-w*y),
        2*(x*y-w*z),   1-2*(x*x+z*z), 2*(y*z+w*x),
        2*(x*z+w*y),   2*(y*z-w*x),   1-2*(x*x+y*y)
    );
}

// Evaluate SH degree 0 + 1
vec3 evalSH(float sh[12], vec3 dir) {
    const float C0 = 0.28209479177387814;
    const float C1 = 0.4886025119029199;
    vec3 col = C0 * vec3(sh[0], sh[1], sh[2]);
    col += C1 * (-dir.y) * vec3(sh[3],  sh[4],  sh[5]);
    col += C1 * ( dir.z) * vec3(sh[6],  sh[7],  sh[8]);
    col += C1 * (-dir.x) * vec3(sh[9],  sh[10], sh[11]);
    return col + 0.5;   // bias to [0, 1]
}

void main() {
    uint idx   = indices[gl_InstanceID];
    Splat sp   = splats[idx];

    // World position
    vec4 worldPos = vec4(sp.position, 1.0);

    // View direction for SH
    vec3 dir = normalize(sp.position - u_camPos);
    v_color  = vec4(clamp(evalSH(sp.sh, dir), 0.0, 1.0), 1.0);
    v_opacity = sp.opacity;

    // Reconstruct 3D covariance: Sigma = R * S^2 * R^T
    mat3 R     = quatToMat(sp.rotation);
    mat3 S     = mat3(sp.scale.x, 0, 0,  0, sp.scale.y, 0,  0, 0, sp.scale.z);
    mat3 Sigma = R * S * S * transpose(R);

    // Project to screen-space 2D covariance (Zwicker et al. 2001)
    mat4 vm    = u_mv;
    vec3 t     = (vm * worldPos).xyz;
    float lim  = 1.3 * 0.5 * float(u_viewport[2]);  // fov-aware clamp

    // Jacobian of perspective projection
    mat3 J = mat3(
        u_proj[0][0] / t.z, 0.0, -(u_proj[0][0] * t.x) / (t.z * t.z),
        0.0, u_proj[1][1] / t.z, -(u_proj[1][1] * t.y) / (t.z * t.z),
        0.0, 0.0, 0.0
    );
    mat3 W    = mat3(vm);
    mat3 T    = J * W;
    mat3 cov3 = T * Sigma * transpose(T);

    // Extract 2x2 screen covariance
    float a = cov3[0][0] + 0.3;
    float b = cov3[0][1];
    float c = cov3[1][1] + 0.3;

    // Eigendecomposition for ellipse axes
    float det = a*c - b*b;
    float tr  = a + c;
    float disc = sqrt(max(0.0, tr*tr/4.0 - det));
    float l1   = tr/2.0 + disc;
    float l2   = tr/2.0 - disc;
    vec2  v1   = normalize(vec2(b, l1 - a));
    vec2  v2   = vec2(-v1.y, v1.x);

    // Billboard corners in screen space
    vec2 halfSize = u_splatScale * 3.0 * vec2(sqrt(max(l1,0.0)), sqrt(max(l2,0.0)));
    vec2 screenOffset = quadPos.x * halfSize.x * v1
                      + quadPos.y * halfSize.y * v2;

    // NDC position
    vec4 clipPos = u_proj * vm * worldPos;
    clipPos.xy  += screenOffset * clipPos.w
                   / vec2(u_viewport[2], u_viewport[3]) * 2.0;

    gl_Position = clipPos;
    v_uv = quadPos;
}
```

### gaussian.frag

```glsl
#version 450

in  vec2  v_uv;
in  vec4  v_color;
in  float v_opacity;

out vec4  fragColor;

void main() {
    // Gaussian falloff in [-1,1] quad space
    float r2    = dot(v_uv, v_uv);
    if (r2 > 1.0) discard;
    float alpha = v_opacity * exp(-0.5 * r2 * 9.0);
    if (alpha < 0.004) discard;
    fragColor   = vec4(v_color.rgb, alpha);
}
```

### sort.comp (GPU Bitonic Sort)

```glsl
#version 450

layout(local_size_x = 256) in;

layout(std430, binding = 0) buffer Keys    { float keys[]; };
layout(std430, binding = 1) buffer Indices { uint  indices[]; };

uniform uint u_stage;
uniform uint u_pass;

void main() {
    uint tid = gl_GlobalInvocationID.x;
    uint N   = keys.length();
    if (tid >= N) return;

    uint pairDist = 1u << (u_pass);
    uint blockSize = pairDist << 1u;
    uint blockBase = (tid / blockSize) * blockSize;
    uint i = blockBase + (tid % pairDist);
    uint j = i + pairDist;

    if (j >= N) return;

    bool ascending = ((tid >> u_stage) & 1u) == 0u;
    if (ascending ? (keys[i] > keys[j]) : (keys[i] < keys[j])) {
        float tk = keys[i]; keys[i] = keys[j]; keys[j] = tk;
        uint  ti = indices[i]; indices[i] = indices[j]; indices[j] = ti;
    }
}
```

**Dispatch loop** (in GaussianRenderer::sort):

```cpp
void GaussianRenderer::sort(const float* viewMatrix) {
    // First pass: compute depth keys
    // Use a simple compute shader that writes dot(pos, viewDir) per splat
    // [implementation detail: separate key-generation compute shader]

    // Bitonic sort: O(N log^2 N) passes
    int n = splatCount_;
    int stages = 0;
    while ((1 << stages) < n) ++stages;

    glUseProgram(sortProgram_);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, keyBuf_);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, indexBuf_);

    for (int stage = 0; stage < stages; ++stage) {
        for (int pass = stage; pass >= 0; --pass) {
            glUniform1ui(glGetUniformLocation(sortProgram_, "u_stage"), stage);
            glUniform1ui(glGetUniformLocation(sortProgram_, "u_pass"),  pass);
            glDispatchCompute((n + 255) / 256, 1, 1);
            glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
        }
    }
}
```

---

## Part 6: GaussianDrawOverride

### GaussianDrawOverride.h

```cpp
#pragma once
#include <maya/MPxDrawOverride.h>
#include "GaussianRenderer.h"

class GaussianDrawOverride : public MHWRender::MPxDrawOverride {
public:
    static MHWRender::MPxDrawOverride* creator(const MObject& obj);
    explicit GaussianDrawOverride(const MObject& obj);
    ~GaussianDrawOverride() override;

    MHWRender::DrawAPI supportedDrawAPIs() const override;

    bool hasUIDrawables() const override { return false; }
    bool alwaysDirty() const override { return true; }  // CRITICAL

    MUserData* prepareForDraw(const MDagPath& objPath,
                              const MDagPath& cameraPath,
                              const MHWRender::MFrameContext& ctx,
                              MUserData* oldData) override;

    static void draw(const MHWRender::MDrawContext& ctx,
                     const MUserData* data);

private:
    GaussianRenderer renderer_;
    bool             uploaded_ = false;
    MString          lastPath_;
};
```

### GaussianDrawOverride.cpp — Key implementation notes

```cpp
MHWRender::DrawAPI GaussianDrawOverride::supportedDrawAPIs() const {
    return MHWRender::kOpenGL | MHWRender::kOpenGLCoreProfile;
}

MUserData* GaussianDrawOverride::prepareForDraw(
    const MDagPath& objPath,
    const MDagPath& cameraPath,
    const MHWRender::MFrameContext& ctx,
    MUserData* oldData)
{
    // prepareForDraw runs on MAIN THREAD — safe to access Maya API
    MObject node = objPath.node();
    MFnDependencyNode fn(node);

    // Read filePath attribute
    MString path = fn.findPlug("filePath", false).asString();

    // Reload if path changed
    if (path != lastPath_) {
        GaussianNode* gNode = dynamic_cast<GaussianNode*>(fn.userNode());
        if (gNode && gNode->dirty) {
            std::string err;
            if (gNode->splatData.load(path.asChar(), err)) {
                renderer_.uploadSplats(gNode->splatData);
                gNode->dirty = false;
                lastPath_ = path;
            } else {
                MGlobal::displayError(MString("GaussianSplat: ") + err.c_str());
            }
        }
    }

    // Pack camera data into MUserData for draw()
    // ... (extract MMatrix camMatrix, pass splatScale, opacityMult)
    return oldData; // reuse existing MUserData object
}

void GaussianDrawOverride::draw(const MHWRender::MDrawContext& ctx,
                                const MUserData* data)
{
    // draw() runs on RENDER THREAD — do NOT access Maya scene API here
    // Only use OpenGL calls and data already extracted in prepareForDraw

    // 1. Get MVP matrices from MDrawContext
    MHWRender::MStateManager* stateMgr = ctx.getStateManager();

    // 2. Set GL state: blend enabled, depth test enabled, depth write disabled
    // 3. Call renderer_.sort(viewMatrix)
    // 4. Call renderer_.draw(mvp, camPos, splatScale, opacityMult, 1)
    // 5. Restore GL state
}
```

---

## Part 7: plugin.cpp

```cpp
#include <maya/MFnPlugin.h>
#include <maya/MDrawRegistry.h>
#include "GaussianNode.h"
#include "GaussianDrawOverride.h"

MStatus initializePlugin(MObject obj) {
    MFnPlugin plugin(obj, "YourStudio", "1.0", "2024");

    MStatus s;
    s = plugin.registerNode(
        GaussianNode::typeName,
        GaussianNode::typeId,
        GaussianNode::creator,
        GaussianNode::initialize,
        MPxNode::kLocatorNode,
        &GaussianNode::drawDBClassification  // "drawdb/geometry/gaussianSplat"
    );
    CHECK_MSTATUS_AND_RETURN_IT(s);

    s = MHWRender::MDrawRegistry::registerDrawOverrideCreator(
        GaussianNode::drawDBClassification,
        GaussianNode::drawRegistrantId,       // "gaussianSplatDrawOverride"
        GaussianDrawOverride::creator
    );
    CHECK_MSTATUS_AND_RETURN_IT(s);

    return MS::kSuccess;
}

MStatus uninitializePlugin(MObject obj) {
    MFnPlugin plugin(obj);
    MHWRender::MDrawRegistry::deregisterDrawOverrideCreator(
        GaussianNode::drawDBClassification,
        GaussianNode::drawRegistrantId
    );
    plugin.deregisterNode(GaussianNode::typeId);
    return MS::kSuccess;
}
```

---

## Part 8: Python Shelf Script

```python
# gaussian_shelf.py
# Shelf button: creates a GaussianSplatNode and opens a file dialog

import maya.cmds as mc
import maya.mel as mel

def create_gaussian_splat():
    path = mc.fileDialog2(
        fileFilter="PLY Files (*.ply)",
        dialogStyle=2,
        fileMode=1,
        caption="Load Gaussian Splat .ply"
    )
    if not path:
        return

    node = mc.createNode("gaussianSplatNode")
    mc.setAttr(f"{node}.filePath", path[0], type="string")
    mc.select(node)
    print(f"[GaussianSplat] Loaded: {path[0]}")

create_gaussian_splat()
```

---

## Part 9: OpenGL State Management

Set these states in `draw()` before issuing the draw call, and restore them after:

```cpp
// Save current state (use MStateManager where possible)
// Then set:
glEnable(GL_BLEND);
glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);  // premultiplied alpha
glEnable(GL_DEPTH_TEST);
glDepthMask(GL_FALSE);   // splats write color but not depth
glDisable(GL_CULL_FACE);

// Issue draw
glUseProgram(drawProgram_);
glBindVertexArray(vao_);
glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, splatBuf_);
glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, indexBuf_);
glDrawArraysInstanced(GL_TRIANGLES, 0, 6, splatCount_);

// Restore
glDepthMask(GL_TRUE);
glDisable(GL_BLEND);
```

---

## Part 10: Known Issues & Edge Cases

| Issue | Cause | Fix |
|---|---|---|
| Viewport not refreshing on camera move | `alwaysDirty()` not returning true | Confirm override returns `true` |
| Splats invisible but no error | Blend state not set / depth write not disabled | Check GL state in draw() |
| Crash on plugin reload | VAO/VBO not deleted in destructor | Implement full `destroyGL()` |
| Sort artifacts (pops) | Sort dispatched before key generation finishes | Add `glMemoryBarrier` between passes |
| Black splats (no SH color) | SH eval returning negative before clamp | Verify `+ 0.5` bias in evalSH |
| Wrong quaternion order | tinyply stores w,x,y,z but shader expects x,y,z,w | Match storage order in PlyLoader |
| PLY load fails silently | Exception swallowed | Ensure catch block calls `MGlobal::displayError` |
| Splat count too large for sort | Bitonic sort requires power-of-2 N | Pad `keyBuf_` and `indexBuf_` to next power of 2 |

---

## Implementation Order (for AI agent)

Execute in this exact order. Do not proceed to the next step until the current one compiles and runs:

1. **CMakeLists.txt + plugin.cpp boilerplate** — empty node that loads/unloads without crash
2. **PlyLoader** — test standalone (outside Maya) with a known .ply file, print first 5 splat positions
3. **GaussianNode** — register node, confirm it appears in Maya's node editor
4. **GaussianRenderer::initGL + uploadSplats** — call from DrawOverride, no draw yet
5. **gaussian.vert + gaussian.frag (no SH, constant white color)** — confirm splats appear as white quads
6. **Correct covariance projection** — splats should appear as oriented ellipses
7. **sort.comp + GaussianRenderer::sort** — add GPU sort, confirm back-to-front order
8. **SH evaluation in vertex shader** — add color, confirm view-dependent tint
9. **GaussianNode attributes (splatScale, opacityMult)** — wire to shader uniforms
10. **Python shelf script** — file dialog → node creation
11. **Edge case handling** (see table above)

---

## Testing Checklist

- [ ] Plugin loads and unloads without crash or memory leak
- [ ] .ply file loads; splatCount printed to Script Editor
- [ ] Splats visible in viewport at correct world position
- [ ] Moving camera causes correct perspective shift (not billboard-locked to screen)
- [ ] Rotating camera changes splat color (SH working)
- [ ] No depth-write artifacts when overlapping other geometry
- [ ] splatScale attribute visibly changes splat size
- [ ] Reloading a different .ply path updates the display
- [ ] Plugin survives Maya File > New without crash
