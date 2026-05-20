# Natron Engine: Rendering & Caching System

> **Scope**: Deep-dive into the Natron (Flux) C++ engine's rendering pipeline, cache system, image representation, viewer, and timeline. Each section covers key classes, data flow, thread safety, and integration points for Flux's layer-based timeline.

---

## 1. Rendering Pipeline

### Overview

The rendering pipeline converts a node graph into pixel data. The flow is:

```
User interaction (scrub/play/parameter change)
  → RenderEngine schedules work
    → OutputSchedulerThread manages frame ordering
      → RenderThreadTask per frame
        → EffectInstance::renderRoI() per node (recursive)
          → Cache lookup / Image allocation
            → Effect render (CPU via OpenFX, or GPU via OpenGL)
              → Image stored in cache
                → ViewerInstance::updateViewer() (if viewer output)
```

### Key Classes

| Class | File | Role |
|---|---|---|
| `RenderEngine` | `Engine/OutputSchedulerThread.h:625` | Top-level render orchestrator |
| `OutputSchedulerThread` | `Engine/OutputSchedulerThread.h:189` | Schedules frames in order, manages thread pool |
| `RenderThreadTask` | `Engine/OutputSchedulerThread.h:93` | Worker thread that renders one frame |
| `EffectInstance` | `Engine/EffectInstance.h` | Base class for all nodes; `renderRoI()` is the core render entry |
| `ViewerRenderEngine` | `Engine/OutputSchedulerThread.h:861` | Viewer-specific render engine subclass |

### RenderEngine (`Engine/OutputSchedulerThread.h:625-858`)

**Key public methods:**
- `renderCurrentFrame(bool enableRenderStats, bool canAbort)` — renders the frame at the current timeline position (used for interactive viewer updates)
- `renderFrameRange(...)` — renders a frame range (used for playback and disk writes)
- `renderFromCurrentFrame(...)` — renders from current frame to end of range
- `abortRenderingNoRestart()` / `abortRenderingAutoRestart()` — cancel ongoing renders
- `quitEngine(bool allowRestarts)` — shutdown all threads
- `waitForEngineToQuit_*()` — blocking/non-blocking wait variants

**Data flow:**
- Input: frame range, view list, direction (forward/backward)
- Output: signals `frameRendered(int time, double progress)`, `fpsChanged(double, double)`, `renderFinished(int retCode)`

**Thread safety:**
- `RenderEngine` itself is not thread-safe; it's owned by the output effect and accessed from the main thread for control, and from worker threads for frame production.
- Internal thread pool managed by `OutputSchedulerThread`.

### OutputSchedulerThread (`Engine/OutputSchedulerThread.h:189-464`)

**Key methods:**
- `appendToBuffer(double time, ViewIdx view, RenderStatsPtr stats, BufferableObjectPtr frame)` — render threads append completed frames here for ordered output
- `renderFrameRange(...)` / `renderFromCurrentFrame(...)` — start sequential rendering
- `pickFrameToRender(RenderThreadTask* thread, ...)` — worker threads call this to get the next frame to render
- `setDesiredFPS(double)` — target playback FPS
- `notifyFrameRendered(...)` — callback when a frame completes

**Virtual methods subclasses must implement:**
- `processFrame(const BufferedFrames& frames)` — consume rendered frames (display or write)
- `timelineStepOne(RenderDirectionEnum)` — advance timeline by one frame
- `timelineGoTo(int time)` — seek timeline
- `getFrameRangeToRender(int& first, int& last)` — get the frame bounds
- `createRunnable()` — factory for `RenderThreadTask` instances

**Key subclasses:**
- `ViewerDisplayScheduler` (`OutputSchedulerThread.h:506`) — for viewer playback; `isFPSRegulationNeeded()` returns true; uses `eSchedulingPolicyOrdered`
- `DefaultScheduler` (`OutputSchedulerThread.h:467`) — for writers/disk output

**Data flow:**
- Render threads produce `BufferableObject` (images) → append to buffer → scheduler thread calls `processFrame()` in order

**Thread safety:**
- Buffer access is mutex-protected. Frame ordering is guaranteed by the scheduler thread consuming from the buffer in time order.

### EffectInstance::renderRoI() (`Engine/EffectInstanceRenderRoI.cpp:313`)

This is the **heart of the rendering pipeline**. It is a ~2000-line function that:

1. **Gets the Region of Definition (RoD)** — calls `getRegionOfDefinition_public()` or uses pre-computed data
2. **Determines needed components** — which image planes (RGBA, Alpha, etc.) are needed
3. **Handles pass-through planes** — if a node doesn't produce certain planes, fetches them from upstream
4. **Identity check** — `isIdentity_public()` determines if this node is a pass-through; if so, recurses to the identity input
5. **Transform concatenation** — merges transform nodes for efficiency
6. **Cache lookup** — for each requested plane, looks up in the node cache using `ImageKey`
7. **Determine rectangles left to render** — uses bitmap system to find unrendered regions
8. **Pre-render input images** — recursively calls `renderInputImagesForRoI()` on all upstream nodes
9. **Allocate image planes** — creates images in cache or resizes existing cached images
10. **Render** — calls `renderRoIInternal()` which invokes the actual OpenFX/GPU render
11. **Post-render** — downscales if needed, marks bitmap, returns output planes

**Key data structures:**
- `RenderRoIArgs` — input: time, view, roi, mipmapLevel, components, bitdepth, scale
- `ImagePlanesToRender` — tracks planes, their images, rects to render, input images
- `RectToRender` — a rectangle with identity flag and input images

**Thread safety model:**
- Thread safety depends on the plugin's `RenderSafetyEnum`:
  - `eRenderSafetyUnsafe` — global plugin lock (recursive mutex)
  - `eRenderSafetyInstanceSafe` — per-node mutex
  - `eRenderSafetyFullySafe` — per-image lock (bitmap system)
  - `eRenderSafetyFullySafeFrame` — host splits RoI into tiles for SMP
- OpenGL renders use a separate context per thread from the `GPUContextPool`
- Render clones are used for `InstanceSafe` plugins to avoid blocking

### Flux Layer-Based Timeline Interaction

The Natron rendering pipeline is **node-graph centric** — each node produces one output at a given time/view. Flux's layer-based timeline will interact as follows:

- **Layer evaluation**: Each layer in Flux maps to a branch of the node graph. The timeline evaluates layers bottom-to-top, compositing each.
- **Frame rendering**: `renderRoI()` is called per-node per-frame. The layer timeline determines which layers are active/visible at each time.
- **Playback**: `OutputSchedulerThread` drives frame-by-frame rendering. The Flux timeline would provide the frame range and current time.
- **GPU pipeline**: Flux replaces the OpenFX+OpenGL path with a wgpu compute pipeline. The `renderRoI()` logic (cache lookup, identity, tiling) can be adapted, but the actual pixel rendering moves to GPU shaders.

---

## 2. Cache System

### Overview

Natron uses a **two-tier LRU cache**: an in-memory portion (RAM) and a disk portion (memory-mapped files). The cache is templated on entry type — the primary instantiations are `Cache<Image>` (node cache) and `Cache<FrameEntry>` (viewer cache).

### Key Classes

| Class | File | Role |
|---|---|---|
| `Cache<EntryType>` | `Engine/Cache.h:382` | LRU cache with RAM + disk tiers |
| `CacheAPI` | `Engine/CacheEntry.h:159` | Interface exposed to cache entries |
| `CacheEntryHelper<T, KeyType, ParamsType>` | `Engine/CacheEntry.h` | Base class for cacheable entries |
| `ImageKey` | `Engine/ImageKey.h:37` | Cache key for images |
| `ImageParams` | `Engine/ImageParams.h:101` | Non-key parameters (RoD, bit depth, components) |
| `KeyHelper<HashType>` | `Engine/KeyHelper.h:57` | Base for cache keys with lazy hash computation |
| `LRUHashTable` | `Engine/LRUHashTable.h` | LRU eviction data structure |

### Cache Template (`Engine/Cache.h:382-1909`)

**Construction:**
```cpp
Cache(cacheName, version, maximumCacheSize, maximumInMemoryPercentage)
```
- `maximumCacheSize` — total bytes across RAM + disk
- `maximumInMemoryPercentage` — fraction kept in RAM (rest spills to disk)

**Key public methods:**
- `get(key, returnValue)` — lookup by key; returns list of entries matching hash
- `getOrCreate(key, params, locker, returnValue)` — lookup or create new entry; returns `true` if found, `false` if created
- `removeEntry(entry)` / `removeEntry(hash)` — remove specific entries
- `clear()` — clear both RAM and disk portions
- `clearDiskPortion()` — clear only disk
- `clearInMemoryPortion()` — evict RAM entries to disk
- `clearExceedingEntries()` — evict until below 90% capacity
- `save(CacheTOC*)` / `restore(CacheTOC&)` — persist/restore cache table of contents
- `setTiled(bool, tileSizeBytes)` — enable tile-based caching mode

**Eviction policy:**
- LRU (Least Recently Used) via `LRUHashTable`
- Eviction triggers at `NATRON_CACHE_LIMIT_PERCENT` (90% of capacity)
- RAM entries evicted to disk first; disk entries deleted when disk is full
- Entries with `use_count > 1` (actively referenced) are **not evictable**

**Two-tier storage:**
1. **RAM** (`_memoryCache`) — entries held in `RamBuffer<T>` (malloc'd memory)
2. **Disk** (`_diskCache`) — entries stored via `MemoryFile` (mmap'd files)
3. **OpenGL texture** — entries can also be GL textures (`eStorageModeGLTex`), not cached

**Tile-based caching** (`Cache.h:511-784`):
- When `setTiled(true, tileSizeBytes)` is called, the cache uses a fixed set of large files (`NATRON_TILE_CACHE_FILE_SIZE_BYTES` = 2GB each)
- Each file is divided into tiles of `tileSizeBytes`
- A `TileCacheFile` tracks which tiles are used via a `vector<bool>`
- `allocTile()` finds a free tile slot; `freeTile()` releases it
- Used for viewer cache where all tiles are the same size

**Notification system:**
- `CacheSignalEmitter` emits Qt signals: `addedEntry(time)`, `removedEntry(time, storage)`, `entryStorageChanged(time, oldStorage, newStorage)`
- Used by the UI to update cache indicators

### Cache Key Structure: ImageKey (`Engine/ImageKey.h:37-85`)

```
ImageKey fields:
  _nodeHashKey      : U64       — hash of the node's parameter state (the "tree version")
  _time             : double    — frame time
  _pixelAspect      : double    — pixel aspect ratio
  _view             : int       — view index (for stereo)
  _draftMode        : bool      — draft/preview quality flag
  _frameVaryingOrAnimated : bool — whether the node produces different output per frame
  _fullScaleWithDownscaleInputs : bool — render quality hint
```

The hash is computed via `fillHash(Hash64*)` which combines all fields. Two keys are equal if all fields match (`operator==`).

**Cache holder ID**: Each key also stores `_holderID` — the cache ID of the node that owns this entry. This allows bulk invalidation when a node's parameters change.

### ImageParams (`Engine/ImageParams.h:101-272`)

Non-key parameters stored alongside the cached image:
- `_rod` (RectD) — Region of Definition in canonical coordinates
- `_par` (double) — pixel aspect ratio
- `_components` (ImagePlaneDesc) — RGBA, RGB, Alpha, etc.
- `_bitdepth` (ImageBitDepthEnum) — byte, short, float
- `_fielding` — field order
- `_premult` — premultiplication state
- `_mipmapLevel` — mipmap level for proxy rendering
- `_isRoDProjectFormat` — whether RoD equals project format

### Thread Safety

| Component | Lock | Purpose |
|---|---|---|
| `_lock` | QMutex | Protects `_memoryCache` and `_diskCache` containers |
| `_sizeLock` | QMutex | Protects `_memoryCacheSize`, `_diskCacheSize`, `_maximumInMemorySize`, `_maximumCacheSize` |
| `_getLock` | QMutex | Serializes `get()` and `getOrCreate()` to prevent duplicate creation |
| `_tileCacheMutex` | QMutex | Protects tile cache file allocation |
| `_memoryFullCondition` | QWaitCondition | Wakes threads waiting for memory when cache is full |

**DeleterThread** (`Cache.h:80-194`): Entries are deleted asynchronously in a dedicated thread to avoid blocking the calling thread during expensive image deallocation.

**CacheCleanerThread** (`Cache.h:201-321`): Removes stale entries (different node hash) asynchronously.

### Flux Integration Notes

- The cache system is **node-hash based** — cache invalidation happens when a node's parameter hash changes. Flux's layer system will need a similar invalidation mechanism when layer properties change.
- Tile-based caching is ideal for the viewer cache where tiles are uniform size. Flux's GPU texture pool serves a similar purpose.
- The two-tier RAM/disk approach is valuable for large compositions. Flux should consider a similar approach for its GPU texture cache (VRAM + system RAM fallback).

---

## 3. Image Class

### Overview

`Image` is the fundamental pixel data container. It extends `CacheEntryHelper<unsigned char, ImageKey, ImageParams>` and `BufferableObject`, meaning it is cacheable and can be backed by RAM, disk (mmap), or OpenGL textures.

### Key Classes

| Class | File | Role |
|---|---|---|
| `Image` | `Engine/Image.h:176` | Pixel buffer with cache integration |
| `Bitmap` | `Engine/Image.h:67` | Tracks which pixels have been rendered |
| `Image::ReadAccess` | `Engine/Image.h:365` | RAII read lock on image |
| `Image::WriteAccess` | `Engine/Image.h:425` | RAII write lock on image |
| `ImagePlaneDesc` | `Engine/ImagePlaneDesc.h` | Describes image components (RGBA, RGB, Alpha, etc.) |

### Image Construction

Two constructors:
1. **Cache-managed**: `Image(ImageKey, ImageParams, CacheAPI*)` — created by the cache system, backed by cache storage
2. **Local**: `Image(ImagePlaneDesc, RectD rod, RectI bounds, mipmapLevel, par, bitdepth, premult, fielding, useBitmap, storage, textureTarget)` — standalone, not cached

### Image Coordinate System

- **RoD (Region of Definition)**: `RectD` in canonical (floating-point) coordinates — the "meaningful" area of the image
- **Bounds**: `RectI` in pixel coordinates — the actual allocated pixel buffer size. Computed as `rod.toPixelEnclosing(mipmapLevel, par)`
- **Mipmap levels**: Proxy rendering uses scaled-down images. `getMipmapLevel()` returns the current level (0 = full resolution)

### Pixel Access

```cpp
// RAII pattern - lock held for lifetime of accessor
Image::ReadAccess acc = image->getReadRights();
const unsigned char* pixels = acc.pixelAt(x, y);  // cast to appropriate type

Image::WriteAccess acc = image->getWriteRights();
unsigned char* pixels = acc.pixelAt(x, y);
```

- `pixelAt(x, y)` returns a pointer to the first component of the pixel at (x, y). Must be cast to the appropriate type (float, unsigned short, unsigned char) based on bit depth.
- The buffer is tightly packed: row stride = `width * numComponents * sizeof(pixelType)`

### Bitmap System (`Engine/Image.h:67-174`)

The bitmap tracks render progress per-pixel:
- 0 = not rendered
- 1 = rendered
- 2 = currently being rendered (when `NATRON_ENABLE_TRIMAP` is defined)

**Key methods:**
- `minimalNonMarkedRects(roi, ret)` — returns minimal list of rectangles within `roi` that are not yet rendered
- `markForRendered(roi)` — marks a region as rendered (sets to 1)
- `clear(roi)` — resets a region to unrendered (sets to 0)
- `isNonMarked(roi)` — checks if a region is entirely unrendered

This enables **incremental rendering**: when a cached image partially covers the requested RoI, only the missing portions are rendered.

### Bit Depth Support

| Enum | Type | Bytes per component |
|---|---|---|
| `eImageBitDepthByte` | unsigned char | 1 |
| `eImageBitDepthShort` | unsigned short | 2 |
| `eImageBitDepthFloat` | float | 4 |
| `eImageBitDepthHalf` | half float | 2 |

### Component Support

| Enum | Components | Count |
|---|---|---|
| `eImageComponentNone` | — | 0 |
| `eImageComponentAlpha` | A | 1 |
| `eImageComponentRGB` | RGB | 3 |
| `eImageComponentRGBA` | RGBA | 4 |
| `eImageComponentXY` | XY | 2 |

### Key Image Operations

| Method | Purpose |
|---|---|
| `fill(roi, r, g, b, a)` | Fill region with color |
| `fillZero(roi)` | Fill with black/transparent |
| `pasteFrom(src, srcRoi)` | Copy pixels from another image |
| `convertToFormat(roi, srcCS, dstCS, ...)` | Convert components + bit depth + color space |
| `downscaleMipmap(rod, roi, fromLevel, toLevel, ...)` | Generate mipmap level |
| `upscaleMipmap(roi, fromLevel, toLevel, ...)` | Upscale from lower mipmap |
| `applyMaskMix(roi, mask, original, ...)` | Apply mask and mix with original |
| `copyUnProcessedChannels(roi, ...)` | Copy unprocessed channels from original |
| `premultImage(roi)` / `unpremultImage(roi)` | Premultiply/unpremultiply by alpha |
| `ensureBounds(newBounds, ...)` | Resize buffer, copying existing content |
| `getRestToRender(roi, ret)` | Get unrendered rectangles within roi |

### Storage Modes

| Mode | Description |
|---|---|
| `eStorageModeRAM` | malloc'd buffer (`RamBuffer<unsigned char>`) |
| `eStorageModeDisk` | mmap'd file (`MemoryFile`) |
| `eStorageModeGLTex` | OpenGL texture (`Texture` class) |
| `eStorageModeNone` | Not allocated |

### Thread Safety

- `QReadWriteLock _entryLock` — protects the image buffer and bitmap
- `ReadAccess` takes a read lock (multiple readers allowed)
- `WriteAccess` takes a write lock (exclusive access)
- The lock is **recursive** to handle cases where a plugin requests the same image it's producing
- Bitmap operations use the same lock

### How Images Pass Between Nodes

1. **Downstream node requests image**: `EffectInstance::renderRoI()` is called on the downstream node
2. **Cache lookup**: The downstream node's output image is looked up in cache via `ImageKey`
3. **If not cached**: `renderRoI()` recursively calls upstream nodes' `renderRoI()` to get input images
4. **Input images passed**: Input images are stored in `RectToRender::imgs` map (input number → list of images)
5. **Render**: The plugin's render action receives input images and writes to the output image
6. **Output cached**: The rendered image is sealed in the cache
7. **Returned**: The output image (shared_ptr) is returned to the caller

### Flux Integration Notes

- Flux's GPU-first approach means images will primarily live as GPU textures rather than CPU buffers
- The bitmap/trimap system for incremental rendering is valuable and should be adapted for GPU render targets
- The mipmap system maps well to GPU mipmaps
- Component conversion (RGBA↔RGB↔Alpha) will be handled by GPU shaders rather than CPU templates

---

## 4. ViewerInstance

### Overview

`ViewerInstance` is the node that displays rendered images in the viewport. It extends `OutputEffectInstance` and is the terminal node of a compositing tree.

### Key Classes

| Class | File | Role |
|---|---|---|
| `ViewerInstance` | `Engine/ViewerInstance.h:61` | Viewer node — renders to display |
| `ViewerInstancePrivate` | `Engine/ViewerInstancePrivate.h:118` | Internal state, texture locks, render params |
| `ViewerDisplayScheduler` | `Engine/OutputSchedulerThread.h:506` | Playback scheduler for viewer |
| `ViewerCurrentFrameRequestScheduler` | `Engine/OutputSchedulerThread.h:550` | Single-frame render scheduler |
| `OpenGLViewerI` | `Engine/OpenGLViewerI.h` | Abstract interface for the OpenGL viewer widget |
| `UpdateViewerParams` | `Engine/UpdateViewerParams.h` | Parameters for updating the viewer texture |
| `RenderViewerArgs` | `Engine/ViewerInstancePrivate.h:72` | Render parameters for the viewer functor |

### ViewerInstance Key Methods

**Render control:**
- `renderViewer(view, singleThreaded, isSequentialRender, ...)` — main render entry point; returns `ViewerRenderRetCode`
- `getViewerArgsAndRenderViewer(time, canAbort, view, ...)` — convenience: get args + render
- `forceFullComputationOnNextFrame()` — bypass cache for next frame
- `clearLastRenderedImage()` — clear cached output

**Viewer parameters (MT-safe):**
- `getGain()` / `setGain()` — exposure multiplier
- `getGamma()` / `setGamma()` — gamma correction
- `getLutType()` / `onColorSpaceChanged()` — color space (sRGB, Rec709, Linear, etc.)
- `getMipmapLevel()` — proxy level
- `getChannels(int texIndex)` — display channels (RGB, RGBA, Alpha, etc.)
- `isAutoContrastEnabled()` — auto-contrast mode
- `setDisplayChannels()` / `setActiveLayer()` — channel/layer selection

**Viewer state:**
- `getUiContext()` — returns `OpenGLViewerI*` (the GUI viewer widget)
- `setUiContext(OpenGLViewerI*)` — set the GUI context
- `disconnectViewer()` — disconnect from input
- `redrawViewer()` / `redrawViewerNow()` — trigger OpenGL redraw

**Render return codes:**
```cpp
enum ViewerRenderRetCode {
    eViewerRenderRetCodeFail,     // Render failed, clear to black, stop playback
    eViewerRenderRetCodeRedraw,   // Just redraw (e.g., aborted)
    eViewerRenderRetCodeBlack,    // Clear to black but don't stop playback
    eViewerRenderRetCodeRender,   // Texture updated, needs display
};
```

### Render Flow

1. **Trigger**: User scrubs timeline, changes parameter, or playback advances
2. **`renderViewer()`** is called with the current time, view, and viewer hash
3. **Cache check**: `getRenderViewerArgsAndCheckCache()` looks up the viewer cache (`FrameEntry` cache) for a matching texture
4. **If cached**: The cached texture data is copied directly to the PBO (Pixel Buffer Object) for display
5. **If not cached**: `renderRoI()` is called on the active input to produce the image, then the image is converted to the viewer's display format (8-bit or 32-bit BGRA)
6. **Texture update**: `updateViewer()` is called, which uploads the texture to the OpenGL viewer
7. **Display**: `redrawViewer()` triggers a repaint

### Dual Input System

The viewer supports two inputs (A/B) for comparison:
- `setInputA(int)` / `setInputB(int)` — select which node feeds each input
- `getActiveInputs(int& a, int& b)` — get current active inputs
- Each input has its own texture index, render age, and display channels

### OpenGL Context

- The viewer uses `OpenGLViewerI` as an abstract interface to the actual GL widget
- `ViewerInstancePrivate` manages a `FrameEntry` lock system (`LockManagerI<FrameEntry>`) to prevent concurrent texture access
- The render functor (`renderFunctor()` in `ViewerInstance.cpp:112`) converts the rendered image to the display texture format (BGRA 8-bit or RGBA 32-bit float)
- Color space conversion uses LUTs (`Color::Lut`): sRGB, Rec709, BT1886, or Linear

### Thread Safety

- `forceRenderMutex` — protects the force-render flags
- `viewerParamsMutex` — protects gain, gamma, LUT, channels, layer selection
- `lastRenderParamsMutex` — protects cached render parameters
- `renderAgeMutex` — protects render age tracking
- `textureBeingRenderedMutex` + `textureBeingRenderedCond` — prevents concurrent writes to the same frame entry
- All UI operations (setUiContext, getUiContext, etc.) must be called on the main thread

### Flux Integration Notes

- Flux replaces the OpenGL viewer with an Electron canvas + wgpu rendering. The `OpenGLViewerI` interface will be replaced with a similar abstraction for the Electron viewport.
- The dual-input A/B comparison system is useful and should be preserved.
- The viewer cache (`FrameEntry`) stores downsampled textures for fast scrubbing. Flux should implement a similar GPU texture cache.
- The render-to-texture-then-display pattern maps directly to Flux's GPU pipeline.

---

## 5. Timeline / Playback

### Overview

`TimeLine` is a simple class representing the current time position in a sequence. It is owned by the `Project` and shared across all viewers and writers.

### Key Classes

| Class | File | Role |
|---|---|---|
| `TimeLine` | `Engine/TimeLine.h:51` | Current frame state + signals |
| `OutputSchedulerThread` | `Engine/OutputSchedulerThread.h:189` | Drives playback frame-by-frame |
| `RenderEngine` | `Engine/OutputSchedulerThread.h:625` | Top-level playback control |

### TimeLine (`Engine/TimeLine.h:51-93`)

**State:**
- `_currentFrame` (SequenceTime) — current frame number, protected by `_lock` (QMutex)
- `_project` (Project*) — back-reference to the owning project

**Key methods:**
- `currentFrame()` — returns current frame (MT-safe)
- `seekFrame(frame, updateLastCaller, caller, reason)` — set current frame
- `incrementCurrentFrame()` — advance by one frame
- `decrementCurrentFrame()` — go back one frame
- `onFrameChanged(frame)` — slot called when the GUI timeline changes

**Signals:**
- `frameAboutToChange()` — emitted before frame changes (allows cleanup)
- `frameChanged(SequenceTime, int reason)` — emitted after frame changes; `reason` is `TimelineChangeReasonEnum`:
  - `eTimelineChangeReasonPlaybackSeek` — from playback
  - `eTimelineChangeReasonUserSeek` — from user interaction
  - `eTimelineChangeReasonOther` — other

**Note:** The `TimeLine` class is intentionally minimal — it only tracks the current frame. Frame range boundaries (`firstFrame`, `lastFrame`) are managed separately by the project and the viewer/writer nodes.

### Playback Architecture

Playback is driven by `OutputSchedulerThread`:

1. `RenderEngine::renderFromCurrentFrame()` is called with direction (forward/backward)
2. `OutputSchedulerThread` creates `RenderThreadTask` instances (one per thread)
3. Each task calls `pickFrameToRender()` to get the next frame number
4. The task calls `renderFrame(time, viewsToRender)` which triggers `renderRoI()` on the output node
5. Completed frames are appended to the buffer via `appendToBuffer()`
6. The scheduler thread calls `processFrame()` in time order
7. For viewers, `processFrame()` calls `ViewerInstance::updateViewer()`
8. `timelineStepOne()` advances the timeline for the next frame

**FPS regulation** (viewer only):
- `ViewerDisplayScheduler::isFPSRegulationNeeded()` returns true
- The scheduler regulates output to match the desired FPS
- Actual FPS is reported via `RenderEngine::fpsChanged(double actual, double desired)`

**Playback modes** (`PlaybackModeEnum`):
- `ePlaybackModeLoop` — loop from first to last
- `ePlaybackModeBounce` — ping-pong
- `ePlaybackModeOnce` — stop at end

### Thread Safety

- `TimeLine::_lock` (QMutex) protects `_currentFrame`
- All public methods are MT-safe
- Signals are emitted **outside** the lock to prevent deadlocks
- `seekFrame()` checks if the frame actually changed before emitting `frameChanged`

### Flux Integration Notes

Flux's layer-based timeline extends Natron's simple frame counter significantly:

- **Natron**: Single global timeline, single current frame, node graph evaluated per-frame
- **Flux**: Layer-based timeline where each layer has its own timing (in-point, out-point, speed, time-remap)

**Integration approach:**
1. Keep `TimeLine` as the global composition time reference
2. Add a **layer evaluation system** that, for each global time, determines:
   - Which layers are active (within their in/out points)
   - What source time each layer references (accounting for speed/remap)
   - What effects are applied per-layer
3. The `OutputSchedulerThread` playback mechanism can be reused — it already handles frame-by-frame scheduling with FPS regulation
4. The `RenderEngine` signals (`frameRendered`, `fpsChanged`, `renderFinished`) map directly to Flux's IPC channels (`timeline:evaluate`, `render:preview-frame`)

**Key difference**: Natron evaluates a node graph (tree), Flux evaluates a layer stack (list). The rendering pipeline (`renderRoI`) is tree-shaped and recursive. Flux's layer compositing is linear (bottom-to-top). The cache system and incremental rendering concepts transfer directly.

---

## Summary: Data Flow Diagram

```
┌─────────────────────────────────────────────────────────────────┐
│                         USER INTERACTION                         │
│  (parameter change, timeline scrub, zoom, playback)             │
└─────────────┬───────────────────────────────────────────────────┘
              │
              ▼
┌─────────────────────────┐
│      RenderEngine        │  ← renderCurrentFrame() / renderFrameRange()
│  (OutputSchedulerThread) │  ← manages thread pool, frame ordering
└─────────────┬───────────┘
              │
              ▼
┌─────────────────────────┐
│   RenderThreadTask       │  ← picks frame from scheduler
│   (per-thread worker)    │  ← calls renderFrame(time, views)
└─────────────┬───────────┘
              │
              ▼
┌─────────────────────────────────────────────────────────────┐
│              EffectInstance::renderRoI()                      │
│  1. Get RoD           6. Determine rects to render           │
│  2. Get components    7. Pre-render input images (recurse)   │
│  3. Pass-through?     8. Allocate image planes in cache      │
│  4. Identity check    9. Render (OpenFX / OpenGL)            │
│  5. Cache lookup     10. Mark bitmap, return output          │
└─────────────┬───────────────────────────────────────────────┘
              │
              ▼
┌─────────────────────────┐     ┌──────────────────────┐
│   Cache<Image>           │◄───►│   Image               │
│  (RAM + Disk LRU)        │     │  (pixels + bitmap)    │
│  ImageKey → ImagePtr     │     │  Storage: RAM/Disk/GL │
└─────────────────────────┘     └──────────────────────┘
              │
              ▼ (if viewer output)
┌─────────────────────────┐
│    ViewerInstance        │  ← renderViewer() → convert to display format
│    updateViewer()        │  ← upload texture to OpenGL/Electron
│    redrawViewer()        │  ← trigger repaint
└─────────────────────────┘
              │
              ▼
┌─────────────────────────┐
│    TimeLine              │  ← currentFrame() / seekFrame()
│    (global time state)   │  ← frameChanged signal
└─────────────────────────┘
```

---

## Appendix: Thread Safety Summary

| Component | Primary Lock | Pattern |
|---|---|---|
| `Cache<>` | `_lock`, `_sizeLock`, `_getLock` | Multiple mutexes for different concerns |
| `Image` | `_entryLock` (QReadWriteLock) | Readers-writer lock; recursive |
| `TimeLine` | `_lock` (QMutex) | Simple mutex; signals emitted outside lock |
| `ViewerInstance` | Multiple mutexes per concern | Fine-grained locking; main-thread affinity for UI |
| `RenderEngine` | Internal to scheduler | Thread pool + condition variables |
| `OutputSchedulerThread` | Buffer mutex + condition | Producer-consumer with ordered output |
| `EffectInstance::renderRoI()` | Plugin-safety-dependent lock | Unsafe→global, InstanceSafe→per-node, FullySafe→per-image |
