# T085 — Build warning audit and cleanup

Status: IN_PROGRESS
Owner: forge
Started: 2026-05-26

## Goal

Reduce build warnings by prioritizing warnings most likely to hide correctness bugs or crash sources. Separate Flux-owned source warnings from generated bindings and inherited third-party code.

## Warning Capture

A clean rebuild was captured with visible output and a timeout:

```text
mkdir -p build-logs
LOG="build-logs/flux-clean-build-20260526-145159.log"
timeout 1800 bash -lc '{ cmake --build build --target clean && cmake --build build --target Natron -- -j"$(nproc)" && cmake --build build --target NatronRenderer -- -j"$(nproc)" && cmake --build build/openfx-flux --target clean && cmake --build build/openfx-flux -- -j"$(nproc)"; } 2>&1 | tee "$0"' "$LOG"
grep -nE "warning:|\\[-W[^]]+\\]" "$LOG" > build-logs/flux-clean-build-20260526-145159.warnings.txt
```

Result:

```text
build_status=0
warning_lines=908
```

## Warning Breakdown

Top categories from the clean warning log:

```text
295 -Wcast-function-type      generated Shiboken wrappers
208 -Wsign-compare            mostly third-party/inherited code
179 -Wunused-parameter        mixed, low risk
88  -Wmissing-declarations    generated + inherited + a few source helpers
16  -Wdeprecated-declarations mixed
16  -Wmaybe-uninitialized     Eigen/Ceres/openMVG inherited code
15  -Wunused-but-set-variable mixed
13  -Wunused-function         inherited/legacy
10  -Wunused-result           inherited/legacy
3   -Wformat=                 Flux-owned Gui/FluxTimeline.cpp
1   -Wduplicated-cond         Flux-owned Gui/NewLayerDialog.cpp
```

Classification:

- Generated warnings: Shiboken output under `build/Gui/Qt6` and `build/Engine/Qt6`.
- Third-party/inherited warnings: `libs/**`, Eigen/Ceres/openMVG/hoedown/libtess/gflags.
- Flux-owned high-priority source warnings: fixed first.

## Fixes Applied First

High-risk/correctness fixes:

1. `Gui/NewLayerDialog.cpp`
   - Fixed duplicated condition in `onNumCompsChanged`.
   - Fourth branch now checks `value == 4`, enabling RGBA controls instead of repeating RGB.

2. `Gui/FluxTimeline.cpp`
   - Fixed `fprintf` format mismatches for `qsizetype` values from `QVector::size()`.
   - Cast sizes to `long long` and use `%lld`.

3. `openfx-flux/TextRender/TextRender.cpp`
   - Replaced deprecated `std::auto_ptr<Image>` with `std::unique_ptr<Image>`.

## Validation

Focused rebuilds after fixes:

```text
timeout 600 cmake --build build --target Natron -- -j"$(nproc)"
timeout 300 cmake --build build/openfx-flux -- -j"$(nproc)"
git diff --check
```

Results:

- `Natron` target rebuilt successfully.
- `FluxTextRender` target rebuilt successfully.
- The fixed warning sites did not reappear in focused rebuild logs:
  - `grep -nE 'NewLayerDialog.cpp:232|FluxTimeline.cpp:462|FluxTimeline.cpp:770|warning:' /tmp/flux-warning-fix-build.log` produced no output.
  - `grep -nE 'TextRender.cpp:231|auto_ptr|warning:' /tmp/flux-ofx-warning-fix-build.log` produced no output.
- `git diff --check` passed.

Note: unrelated T084 installer changes were already present in the working tree
before T085 began and are tracked separately in `tasks/T084-plugin-payload-discovery.md`.

## Second Cleanup Pass

Additional avoidable Flux-owned warnings fixed:

- `Gui/Gui05.cpp`: removed unused `mediaDuration`.
- `Gui/NodeCreationDialog.cpp`: removed unused plugin counter `i`.
- `Gui/NodeGui.cpp`: removed unused `emptyInputsCount` bookkeeping.
- `Gui/TableModelView.cpp`: removed unused `left` / `right` locals.
- `Gui/ViewerTabPrivate.cpp`: removed unused `outputNode` local without changing recursion.
- `Gui/Gui30.cpp`: replaced deprecated `QString::count()` with `size()`.
- `Gui/FluxExportPanel.cpp`: uses Qt 6.7+ `QCheckBox::checkStateChanged` with a Qt5/older Qt fallback.
- `Gui/CustomParamInteract.cpp`: uses Qt6 `QMouseEvent::position().toPoint()` with Qt5 `localPos().toPoint()` fallback, preserving the old integer-coordinate behavior.
- `Gui/QtColorTriangle.cpp`: made file-local helper `angleBetweenAngles` static.
- `Engine/Noise.cpp`: made file-local helper `s_curve` static.

Validation:

```text
timeout 900 cmake --build build --target Natron -- -j"$(nproc)"
grep -nE 'Gui05.cpp:2370|NodeCreationDialog.cpp:376|NodeGui.cpp:1516|TableModelView.cpp:922|ViewerTabPrivate.cpp:296|Gui30.cpp:354|FluxExportPanel.cpp:112|CustomParamInteract.cpp:326|CustomParamInteract.cpp:327|CustomParamInteract.cpp:345|CustomParamInteract.cpp:346|CustomParamInteract.cpp:364|CustomParamInteract.cpp:365|QtColorTriangle.cpp:916|Noise.cpp:54' /tmp/flux-warning-fix-pass2-build.log
git diff --check
```

Results:

- `Natron` target rebuilt successfully with the active Qt6 configuration.
- Targeted source warning grep produced no output.
- `git diff --check` passed.
- Qt5 fallback branches were source-inspected for compatibility but not compiled in this environment.

## Target-Scoped Warning Suppression

Generated Shiboken wrapper warnings and third-party/inherited library warnings
were moved out of the main Flux signal by target/source-scoped CMake options:

- `Engine/CMakeLists.txt`: generated `PyEngine_SOURCES` suppress
  `-Wcast-function-type` and `-Wmissing-declarations`.
- `Gui/CMakeLists.txt`: generated `PyGui_SOURCES` suppress
  `-Wcast-function-type` and `-Wmissing-declarations`.
- `libs/CMakeLists.txt`: third-party targets suppress common inherited warning
  classes privately on those targets only.

Validation:

```text
timeout 1800 bash -lc 'cmake --build build --target clean && cmake --build build --target Natron -- -j"$(nproc)" && cmake --build build --target NatronRenderer -- -j"$(nproc)"'
timeout 300 cmake --build build/openfx-flux -- -j"$(nproc)"
git diff --check
```

Results:

```text
Natron/NatronRenderer build_status=0
warning_lines=89
source warnings=65
generated compiler warnings=2
FluxTextRender build_status=0
```

The preserved warning artifacts are:

```text
build-logs/flux-warning-suppression-20260526-151753.log
build-logs/flux-warning-suppression-20260526-151753.warnings.txt
```

This reduced compiler warning lines for the app/renderer path from the initial
908-line clean capture to 89 while keeping Flux-owned source warnings visible.
Remaining warning lines are now mostly actionable Flux/Engine/Gui warnings plus
Shiboken generator diagnostics printed before generated source compilation.

## Third Cleanup Pass

Additional real Flux warning fixes:

- `Engine/OSGLContext_wayland.cpp`: initialized the unused depth argument path and marked unused Wayland callback parameters.
- `Engine/OfxHost.cpp`: checked `QTemporaryFile::open()` before using the temporary cache filename.
- `Engine/ProcessHandler.cpp`: checked `QTemporaryFile::open()` before deriving IPC socket paths; failures now throw instead of silently using an invalid path.
- `Engine/Project.cpp`: checked compatibility-scan `QFile::open()` before reading.
- `Engine/NodeInputs.cpp`: fixed shadowed `ret` locals so `disconnectOutput()` updates the intended return variable only when a matching output is erased, preserving `-1` for not-found.
- `Engine/NodeGroup.cpp`: removed unused parametric curve counter.
- `Engine/RotoPaint.cpp`: removed unused overlay index counter.
- `Engine/RotoContext.cpp`: removed unused feather render index counter.
- `Gui/AboutWindow.cpp`: checked resource `QFile::open()` calls before reading.
- `Gui/CurveWidget.cpp`: checked curve import/export file opens before creating streams and report errors.

Validation:

```text
timeout 900 cmake --build build --target Natron -- -j"$(nproc)"
grep -nE 'OSGLContext_wayland.cpp|OfxHost.cpp:1114|ProcessHandler.cpp:77|ProcessHandler.cpp:397|Project.cpp:302|AboutWindow.cpp:130|AboutWindow.cpp:445|AboutWindow.cpp:460|AboutWindow.cpp:469|CurveWidget.cpp:115|CurveWidget.cpp:2171|CurveWidget.cpp:2251|NodeInputs.cpp:1259|NodeInputs.cpp:1267|NodeGroup.cpp:1763|RotoPaint.cpp:1940|RotoContext.cpp:3564' /tmp/flux-warning-fix-pass3-build.log
git diff --check
```

Results:

- `Natron` target rebuilt successfully.
- Targeted source warning grep produced no output.
- `git diff --check` passed.

## Fourth Cleanup Pass

Additional safe warning cleanup:

- `CMakeLists.txt`: scoped `-Wno-deprecated-copy` to C++ compilation only so C sources no longer receive a C++-only compiler flag.
- `Engine/BezierCP.cpp`: marked assert-only `index` as intentionally used for release builds.
- `Engine/AppManagerPrivate.cpp`: removed dead cache-file counting logic; the function only checks subfolder count.
- `Engine/Lut.cpp`: removed unused loop counter in non-alpha path.
- `Engine/RotoReplaceChannels.cpp`: marked unused `inputNb`.
- `Engine/StandardPaths.cpp`: marked Linux username helper as intentionally unused.
- `Engine/Transform.cpp`: made internal-only `matTranslation()` static.
- `Global/PythonUtils.cpp`: marked saved GIL state as intentionally retained for the documented restore path.
- `Gui/Histogram.cpp`, `Gui/ViewerGL.cpp`: marked unused shared OpenGL widget parameters.
- `Gui/KnobGuiColor.cpp`: marked unused enter-event parameter.
- `Gui/MultiInstancePanel.cpp`: removed an unused empty tracker-transform stub and declaration.
- `Gui/QtEnumConvert.cpp`: uses non-deprecated `Qt::Key_micro` on Qt6 while preserving Qt5 fallback.

Validation:

```text
timeout 900 cmake --build build --target Natron -- -j"$(nproc)"
timeout 1800 bash -lc 'cmake --build build --target clean && cmake --build build --target Natron -- -j"$(nproc)" && cmake --build build --target NatronRenderer -- -j"$(nproc)"'
git diff --check
```

Results:

```text
Natron incremental build_status=0
clean Natron/NatronRenderer build_status=0
warning_lines=32
```

Preserved artifacts:

```text
build-logs/flux-warning-pass4-20260526-160824.log
build-logs/flux-warning-pass4-20260526-160824.warnings.txt
```

Remaining warnings after pass 4:

- Third-party/inherited C/C++ warnings: `libs/libtess`, `libs/hoedown`, `libs/gflags`, `libs/openMVG`, `libs/SequenceParsing`, `libs/OpenFX/HostSupport`.
- Generated Qt moc/SFINAE warnings from bundled `qhttpserver` headers.
- Python 3.14 deprecation warnings in `Engine/AppManager.cpp` and `Global/PythonUtils.cpp` around legacy embedded-Python initialization APIs.

## Remaining Work

Next priority:

1. Flux-owned unused-but-set/unused-variable warnings in `Engine/` and `Gui/`.
2. Flux-owned Qt6 deprecation warnings where replacements are straightforward.
3. Decide whether generated Shiboken warning suppression should be target-scoped rather than source-edited.
4. Decide whether inherited third-party warning cleanup should be suppressed target-scoped or left documented.
