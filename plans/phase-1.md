# Phase 1: Fork & Build — Detailed Plan

**Phase**: P1
**Status**: PENDING
**Depends on**: P0 (Project Setup)
**Estimated duration**: 3-5 days
**Goal**: Fork Natron RB-2.6, build it, verify the engine works, validate all critical capabilities with real-world test artifacts.

---

## Overview

This phase proves that the Natron engine does everything we need before we commit to building the Flux UI on top of it. We fork, build, and systematically validate each capability with real images and videos.

---

## Step 1: Fork Natron (T006)

**Objective**: Get Natron RB-2.6 source code into our repo.

**Approach**:
1. Fork NatronGitHub/Natron on GitHub
2. Clone into the Flux workspace (or set as upstream)
3. Create a `flux/main` branch from `RB-2.6`
4. Verify the branch builds cleanly

**Acceptance Criteria**:
- [ ] Natron source code is in our repo
- [ ] `flux/main` branch exists off RB-2.6
- [ ] Git history is preserved

---

## Step 2: Install Build Dependencies (T007)

**Objective**: Set up the development environment.

**Approach**:
1. Read `INSTALL_LINUX.md` from the Natron repo
2. Install all required dependencies:
   - Qt5 (5.15+) or Qt6 (6.3+)
   - Python3 development headers
   - Shiboken2/6 + PySide2/6
   - Boost
   - OpenColorIO
   - OpenImageIO
   - FFmpeg development libraries
   - OpenEXR
   - CMake 3.16.7+
   - C++17 capable compiler (GCC 9+ or Clang 10+)
   - X11/Wayland development libraries
3. Document any issues encountered

**Acceptance Criteria**:
- [ ] All dependencies installed
- [ ] CMake configure succeeds
- [ ] Document exact package names/versions installed

---

## Step 3: Build Natron (T008)

**Objective**: Compile Natron from source.

**Approach**:
1. Create build directory: `mkdir build && cd build`
2. Run CMake configure
3. Build with `make -j$(nproc)`
4. Document build warnings and errors
5. Fix any build issues (may need patching for modern compilers/libraries)

**Acceptance Criteria**:
- [ ] Natron compiles without errors
- [ ] All targets built: Natron binary, Renderer, Engine library
- [ ] Build warnings documented
- [ ] Any patches applied are committed

---

## Step 4: Run Natron GUI (T009)

**Objective**: Verify the built Natron works as a GUI application.

**Approach**:
1. Run the built Natron binary
2. Create a new project
3. Add a Reader node, load a test image
4. Add a Viewer node, connect to Reader
5. Verify the image displays correctly
6. Test basic operations: pan, zoom, scrub

**Acceptance Criteria**:
- [ ] Natron launches without crashes
- [ ] Can create a project
- [ ] Can import and display an image
- [ ] Viewer works (pan, zoom)

---

## Step 5: Build Engine Standalone (T010)

**Objective**: Verify Engine/ can be built as a shared library without Gui/.

**Approach**:
1. Modify CMakeLists.txt to exclude Gui/ and App/ targets
2. Build only: Engine, Global, HostSupport, libs, Renderer
3. Verify `libNatronEngine.so` is produced
4. Verify Renderer/ still compiles and links against it

**Acceptance Criteria**:
- [ ] Engine builds as shared library
- [ ] No undefined symbol errors
- [ ] Renderer links successfully
- [ ] Library size is reasonable

---

## Step 6: Test Headless Render (T011)

**Objective**: Verify headless rendering works (no GUI needed).

**Approach**:
1. Create a simple Natron project (NTP file) with:
   - A Reader node pointing to a test image
   - A Writer node pointing to an output path
2. Run: `NatronRenderer -b test.ntp`
3. Verify output file is created and correct
4. Measure render time

**Acceptance Criteria**:
- [ ] Headless render completes without errors
- [ ] Output file matches input (pixel-accurate or visually correct)
- [ ] Render time is reasonable for test resolution

---

## Step 7: Document Key Engine Classes (T012)

**Objective**: Map the Engine API surface for Flux development.

**Approach**:
1. Read and document the following classes:
   - `AppManager` — initialization, plugin loading, cache management
   - `AppInstance` — project lifecycle
   - `Node` — node creation, connection, rendering
   - `EffectInstance` — effect lifecycle, render actions
   - `Image` — pixel buffer management
   - `Cache<>` — cache operations
   - `TimeLine` — time management
   - `ViewerInstance` — display rendering
   - `Knob` — parameter system
   - `Curve` — animation curves
2. For each class, document:
   - Public API methods relevant to Flux
   - Qt dependencies (signals, slots, QObjects)
   - Thread safety notes
   - How Flux will interact with it

**Acceptance Criteria**:
- [ ] Each key class documented in `docs/engine-api.md` or similar
- [ ] API surface mapped for Layer-to-Node bridge design
- [ ] Qt coupling points identified

---

## Step 8: Validate Import — Video (T013)

**Objective**: Verify video import works with real files.

**Test Artifacts Needed**:
- A `.mov` file (ProRes or H264)
- A `.mp4` file (H264)
- A `.mxf` file (any codec)
- Various resolutions: SD, HD, 4K
- Various frame rates: 24fps, 25fps, 30fps, 60fps

**Approach**:
1. Import each file via Natron GUI
2. Verify frame count matches source
3. Verify resolution matches source
4. Verify color accuracy (visual inspection)
5. Test frame-accurate seeking
6. Measure decode performance

**Acceptance Criteria**:
- [ ] All video formats import correctly
- [ ] Frame counts match source media
- [ ] Color is accurate (no obvious shifts)
- [ ] Seeking is frame-accurate
- [ ] Decode is fast enough for real-time at 1080p

---

## Step 9: Validate Import — Images (T014)

**Objective**: Verify image import works with real files.

**Test Artifacts Needed**:
- `.exr` — multi-layer, 16-bit half-float, 32-bit float
- `.tiff` — 8-bit, 16-bit, 32-bit float
- `.png` — 8-bit, 16-bit, with alpha
- `.jpg` / `.jpeg` — standard
- `.psd` — layered Photoshop file

**Approach**:
1. Import each file via Natron GUI
2. Verify pixel accuracy (compare with known reference)
3. For PSD: verify individual layers are accessible
4. For EXR: verify multi-layer/channel support
5. For TIFF: verify 16-bit and float support

**Acceptance Criteria**:
- [ ] All image formats import correctly
- [ ] 16-bit and 32-bit float data preserved
- [ ] PSD layers accessible
- [ ] EXR multi-layer support works
- [ ] Alpha channels preserved

---

## Step 10: Validate OCIO (T015)

**Objective**: Verify OCIO color management works correctly.

**Test Artifacts Needed**:
- An ACES OCIO config (e.g., `cg-config-v2.0.0_aces-v1.3_ocio-v2.3`)
- Test images in various color spaces (sRGB, Rec.709, ACEScg, linear)

**Approach**:
1. Configure Natron with ACES OCIO config
2. Import an sRGB image
3. Apply input transform: sRGB → ACEScg
4. Apply display transform: ACEScg → sRGB (for display)
5. Verify round-trip is accurate (input ≈ display output)
6. Test with different input color spaces

**Acceptance Criteria**:
- [ ] OCIO config loads without errors
- [ ] Input transforms produce correct colors
- [ ] Display transforms produce correct colors
- [ ] Round-trip is visually lossless
- [ ] GPU-accelerated transforms work (if applicable)

---

## Step 11: Validate Cache (T016)

**Objective**: Verify RAM and disk cache work correctly.

**Approach**:
1. Import a video file
2. Play through the timeline — frames should be cached to RAM
3. Scrub back and forth — cached frames should be instant
4. Exceed RAM cache size — frames should spill to disk
5. Verify disk cache persists across application restart
6. Measure cache hit rates and performance

**Acceptance Criteria**:
- [ ] RAM cache works (instant replay of cached frames)
- [ ] Disk cache works (frames survive restart)
- [ ] Cache eviction is LRU as expected
- [ ] Cache doesn't cause memory leaks
- [ ] Performance is acceptable for real-time playback

---

## Step 12: Validate Playback Performance (T017)

**Objective**: Verify real-time playback meets performance targets.

**Test Artifacts Needed**:
- 1080p video file (24fps)
- Multiple layers of footage for stress testing

**Approach**:
1. Load a single 1080p video layer
2. Play at native frame rate — measure actual FPS achieved
3. Add effects (blur, color correction) — measure FPS with effects
4. Add more layers (2, 3, 5, 10) — measure FPS with multiple layers
5. Test with proxy rendering (half resolution)
6. Document hardware specs used for testing

**Target**: Real-time 1080p at 24fps for 5+ layers with effects

**Acceptance Criteria**:
- [ ] Single layer 1080p plays at 24fps
- [ ] 5 layers with basic effects plays at 24fps (or close)
- [ ] Proxy rendering improves performance proportionally
- [ ] Performance metrics documented

---

## Step 13: Set Up Development Workflow (T018)

**Objective**: Establish repeatable build/test/run workflow for Flux development.

**Approach**:
1. Document the exact build steps
2. Create a simple build script if helpful
3. Set up test asset directory (`tests/assets/`)
4. Document how to run the test suite
5. Document how to debug (GDB, Qt debugging)

**Acceptance Criteria**:
- [ ] Build steps documented
- [ ] Test asset directory created with sample files
- [ ] Developer can build, run, and test from clean checkout
- [ ] Debug workflow documented

---

## Phase 1 Exit Checklist

Before moving to P2, ALL of the following must be true:

- [ ] Natron builds from our fork on Linux
- [ ] Engine compiles as shared library
- [ ] Headless rendering works
- [ ] Video import works (mov, mp4, mxf)
- [ ] Image import works (exr, tiff, png, jpg, psd)
- [ ] OCIO color management works
- [ ] Cache (RAM + disk) works
- [ ] Real-time playback performance is acceptable
- [ ] Key Engine classes documented
- [ ] Development workflow established
- [ ] All validation tasks have real test results documented
