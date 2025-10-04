# Wind Tunnel: real-time virtual aerodynamics in C++23 / Qt 6

A desktop fluid simulator that puts the reference Mustang (`references/25-mustang-13.04.2021-obj.zip`)
in a virtual wind tunnel. A multithreaded **D3Q19 lattice-Boltzmann LES solver** computes the flow,
and a Blender-style UI (dark, blue) built on **`QGraphicsView` + OpenGL 3.3** shows it.

![layout](references/Screenshot%202026-09-23%20154609.png)

## Build & run

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release   # also extracts the sample OBJ into build/assets
cmake --build build -j
./build/tests/run-test                            # Catch2 unit tests (core library)
./build/app/windtunnel                            # launches with the Mustang loaded and the solver running
```

Useful options: `--resolution 224`, `--workspace 0..4`, `--paused`, `--model file.obj`,
`--screenshot out.png` / `--report out.pdf` (with `--delay s`) for headless captures.
Requires Qt ≥ 6.2 (Widgets, OpenGL, OpenGLWidgets, Concurrent). spdlog, argparse and Catch2 are fetched by CMake.

## Using it

| Area | What it does |
|---|---|
| **Viewport** (`QGraphicsView`) | LMB orbit · Shift/RMB pan · wheel zoom · double-click frames · `1/3/7` views (Ctrl = opposite) · `5` ortho · `N` sidebar · `Space` run/pause |
| **Tool shelf** (left) | select/orbit, pan, zoom, **probe** (click the car or the slice to read speed, Cp and vorticity) |
| **Gizmo** (top right) | click an axis to snap the view, drag to orbit; buttons for zoom, frame, ¾ view, ortho, screenshot |
| **Fluid Flow sidebar** | simulation type presets, *Create New Simulation*, vehicle placement (nose axis, length, **yaw / cross-wind**), collision objects, solver settings (wind speed, Reynolds, iterations, resolution, Smagorinsky, turbulence, rolling road, tunnel size), emitter rake. *View* tab: shading, streamlines, particles, slice, volume, quality |
| **Workspaces** (top bar) | Layout · Aerodynamics (surface Cp + slice) · Flow Structures (volume-rendered vorticity) · Smoke · Wind Tunnel (side slice) |
| **Outliner** | collections with visibility 👁 and collision 🛡 toggles and a search filter |
| **Properties** | 🎨 scalar field, range, colormap and an interactive **transfer-function editor** (a `QGraphicsView` with draggable control points over a live histogram) · 📈 Cd/Cl/Cs, forces, drag power, convergence · 📄 PDF report with preview |
| **Timeline** | run / step / reset, iteration counter, end iteration (auto-pause) and a Cd/Cl trace under the playhead |

All numeric fields are Blender-style: drag to scrub (Shift = fine, Ctrl = snap), click to type, Ctrl+wheel to step.

## Architecture

```
core/   fluidcore: pure C++23, no Qt, unit tested
  ObjLoader        fast OBJ parser (from_chars, open-addressing vertex dedup, progress/cancel, std::expected)
  MeshSimplify     vertex-clustering LOD that keeps objects/materials and creases
  SceneLayout      model→world placement (nose axis, scale, yaw) and wind-tunnel fitting
  Voxelizer        surface rasterization + 6-ray enclosure vote (robust to open, non-watertight meshes)
  LbmSolver        D3Q19 BGK + Smagorinsky, fused pull streaming, equilibrium inlet, pressure outlet,
                   rolling road, free-slip walls, outlet sponge, momentum-exchange forces (gauge pressure)
  FlowField        trilinear sampling, vorticity, RK4 streamlines, particle advection
  TransferFunction colormaps + opacity curve, baked lookup tables
  ThreadPool       fork-join parallelFor used by solver, voxelizer and tracers
app/    Qt 6 application
  model/SceneDocument          single source of truth (model, settings, transfer function), signals out
  simulation/                  SimulationWorker on its own QThread; immutable FlowSnapshots with
                               back-pressure (one frame in flight), stale-domain filtering
  services/MeshProvider        async loading via QtConcurrent + QPromise (progress, cancel, LOD build)
  services/ReportService       PDF rendering on a worker thread (QPdfWriter), shared with the preview
  viewport/ViewportView        QGraphicsView with a QOpenGLWidget viewport: GL scene in drawBackground(),
                               QGraphicsItem overlays and the sidebar as a QGraphicsProxyWidget
  viewport/SceneRenderer       GL 3.3 core: MSAA FBO, PBR-ish studio shading / clay / surface field,
                               3D field texture, MPR slice with iso-contours, volume ray casting,
                               screen-space streamline ribbons with animated pulses, particles, FXAA
  widgets/, panels/, ui/       ValueField, CollapsibleSection, TransferFunctionEditor, Timeline,
                               Outliner, Properties, theme + vector icon factory (no image assets)
```

**Rendering quality** is adaptive. On a GPU the viewport uses the full 1.47M-triangle mesh with 4× MSAA.
On a software rasterizer (for example `llvmpipe` in a container without GPU passthrough) it switches to
the 70k-triangle LOD with FXAA, about 4× faster. You can override this under *View → Overlays → Quality*.
Set `WT_PROFILE=1` to log the 3D render time per frame.

**Physics notes.** The simulated Reynolds number is set explicitly: real air around a car reaches
Re ≈ 10⁷, which no real-time grid can resolve, and the Smagorinsky model stands in for the unresolved
scales. At the default 160-cell resolution the car is about 40 cells long. Coefficients such as Cd ≈ 0.7–0.8
are therefore indicative only; they converge towards the real value as the resolution goes up.
