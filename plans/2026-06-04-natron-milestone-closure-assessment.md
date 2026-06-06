# Natron Milestone Closure Assessment

Date: 2026-06-04

## Request

Assess the open Natron GitHub milestones, especially 2.6, 3.0, and 4.0, and identify what can realistically be done to close open issues.

Primary source: <https://github.com/NatronGitHub/Natron/milestones>

## Current Milestone Reality

| Milestone | GitHub source | Observed state | Realistic closure read |
|---|---|---:|---|
| 2.6 | <https://github.com/NatronGitHub/Natron/milestone/17> | 6 open / 4 closed | Closeable if narrowed to Qt5/Python3 release readiness plus FFmpeg performance. Several items should be moved out. |
| 3.0 | <https://github.com/NatronGitHub/Natron/milestone/5> | 60 open / 12 closed | Not a release checklist. It is a broad backlog. Close by extracting a small engine/cache/performance plus real-bugs subset and moving the rest. |
| 4.0 | <https://github.com/NatronGitHub/Natron/milestone/2> | 15 open / 5 closed | Mostly long-horizon architecture/ecosystem requests. Do not treat as a near-term closure target. |

## Highest-Value Executable Candidates

| Priority | Issue / PR | Concrete work | Why it matters to Flux | Risk |
|---:|---|---|---|---|
| 1 | [#1047 ReadFFmpeg performance](https://github.com/NatronGitHub/Natron/issues/1047) | Add sequential decode cache/prefetch around video reads; benchmark real `.mov`/`.mp4` preview and render FPS. | Very high. Flux uses ReadFFmpeg and P8 already targets cache/performance. | Medium: reader/cache behavior must not break random-access renders. |
| 2 | [#1019 Qt6 support PR](https://github.com/NatronGitHub/Natron/pull/1019) | Review/harvest `gui-sbk6` Qt6 work, close portability/runtime gaps, align Flux with upstream Qt6 direction. | Very high. Flux already runs on Qt6/gui-sbk6. | Medium-high: broad UI/build surface. |
| 3 | [#264 ReadSVG continuous rasterization](https://github.com/NatronGitHub/Natron/issues/264) | Make SVG/PDF/vector import resolution-independent under transforms/render scale. | High. Direct overlap with Flux T082 Illustrator/vector import. | Medium-high: import/plugin/render-scale semantics. |
| 4 | [#113 SMPTE timecode](https://github.com/NatronGitHub/Natron/issues/113) + [#161 FFmpeg options](https://github.com/NatronGitHub/Natron/issues/161) | Add practical media ingest/export controls: timecode handling, FFmpeg option exposure. | High for production media workflows. | Medium. FFmpeg option UX can sprawl if not scoped. |
| 5 | [#105 metadata preservation](https://github.com/NatronGitHub/Natron/issues/105) | Preserve/copy/set source metadata through Write, especially EXR/video delivery metadata. | High for VFX delivery correctness. | Medium. Needs format-specific validation. |
| 6 | [#156 proxy mode for disk rendering](https://github.com/NatronGitHub/Natron/issues/156) | Add GUI/render proxy-scale control for output renders. | Medium-high; aligns with export/performance work. | Medium. Must avoid changing final-quality defaults accidentally. |

## Solid Bug-Fix Candidates

These are realistic closures without changing the product direction.

| Issue | Work |
|---|---|
| [#383](https://github.com/NatronGitHub/Natron/issues/383) | Fix floating window restore on monitors with negative coordinates. |
| [#270](https://github.com/NatronGitHub/Natron/issues/270) | Fix Python file-dialog filter handling in `GuiApp::getFilenameDialog()` / `SequenceFileDialog`. |
| [#254](https://github.com/NatronGitHub/Natron/issues/254) | Harden host behavior when plugin `isIdentity()` conflicts with `getFramesNeeded()`. |
| [#216](https://github.com/NatronGitHub/Natron/issues/216) | Restore/readably expose Read/Write Info panel. |
| [#209](https://github.com/NatronGitHub/Natron/issues/209) | Debug ViewerGL 8-bit texture upload/shader blank-image path. |
| [#135](https://github.com/NatronGitHub/Natron/issues/135) | Fix NodeGraph drag position while autoscrolling. |

## Manageable UX / Feature Cleanup

| Issue(s) | Work |
|---|---|
| [#170](https://github.com/NatronGitHub/Natron/issues/170) | Roto with no RGB input should output alpha-only. |
| [#146](https://github.com/NatronGitHub/Natron/issues/146), [#160](https://github.com/NatronGitHub/Natron/issues/160) | Show Roto/tracker keyframes in Dope Sheet / Curve Editor. Flux has related timeline keyframe work already. |
| [#126](https://github.com/NatronGitHub/Natron/issues/126) | Render individual frames/ranges from Write node GUI. |
| [#151](https://github.com/NatronGitHub/Natron/issues/151) | Add flipbooker-style CLI open/playback args. |
| [#138](https://github.com/NatronGitHub/Natron/issues/138), [#123](https://github.com/NatronGitHub/Natron/issues/123) | Curve smoothing / freehand curve sketching. |
| [#84](https://github.com/NatronGitHub/Natron/issues/84), [#96](https://github.com/NatronGitHub/Natron/issues/96), [#101](https://github.com/NatronGitHub/Natron/issues/101), [#111](https://github.com/NatronGitHub/Natron/issues/111), [#117](https://github.com/NatronGitHub/Natron/issues/117) | Small viewer/shortcut/overlay improvements. |
| [#155](https://github.com/NatronGitHub/Natron/issues/155) | Improve channel selector UI for multi-layer EXR. |

## Move or De-Scope From Milestone Closure

- [#115 Natron as Python module](https://github.com/NatronGitHub/Natron/issues/115) and [#153 embedded IPython](https://github.com/NatronGitHub/Natron/issues/153): large Python embedding/product-surface decisions, not 2.6 release blockers.
- [#741 Linux Qt5 binaries](https://github.com/NatronGitHub/Natron/issues/741), [#743 Windows Qt5 binaries](https://github.com/NatronGitHub/Natron/issues/743): upstream release engineering, not Flux feature work. Flux already has installer/deploy coverage via T070/T084.
- [#163 Cryptomatte](https://github.com/NatronGitHub/Natron/issues/163), [#176 tiled multi-resolution TIFF/EXR](https://github.com/NatronGitHub/Natron/issues/176), [#177 JPEG2000](https://github.com/NatronGitHub/Natron/issues/177), [#79 waveform/vectorscope](https://github.com/NatronGitHub/Natron/issues/79), [#277 ARM OpenFX](https://github.com/NatronGitHub/Natron/issues/277): worthwhile, but bigger subsystem/plugin projects.
- Most 4.0 items — [#252 CUDA](https://github.com/NatronGitHub/Natron/issues/252), [#108 NodeGraph OpenGL rewrite](https://github.com/NatronGitHub/Natron/issues/108), [#32 live I/O](https://github.com/NatronGitHub/Natron/issues/32), [#6 OFX parameter interacts](https://github.com/NatronGitHub/Natron/issues/6), [#7 field rendering](https://github.com/NatronGitHub/Natron/issues/7), [#436 Mocha integration](https://github.com/NatronGitHub/Natron/issues/436) — are architectural/ecosystem projects, not quick milestone closers.

## Recommended Priority

### Do First

1. [#1047 ReadFFmpeg performance](https://github.com/NatronGitHub/Natron/issues/1047)
2. [#1019 Qt6 alignment](https://github.com/NatronGitHub/Natron/pull/1019)
3. [#264 vector/SVG/PDF rasterization](https://github.com/NatronGitHub/Natron/issues/264)
4. [#113](https://github.com/NatronGitHub/Natron/issues/113) / [#161](https://github.com/NatronGitHub/Natron/issues/161) media ingest/export controls
5. [#105 metadata preservation](https://github.com/NatronGitHub/Natron/issues/105)

### Do Later

- Bounded GUI/viewer/Curve Editor fixes.
- Roto/tracker keyframe visibility.
- Proxy render mode.

### Do Not Touch Now

- Python module/IPython.
- Upstream Natron binary packaging unless Flux release engineering specifically needs it.
- Most 4.0 architectural requests. Move them out of closure scope.

## Sources Used

- Milestones API: <https://api.github.com/repos/NatronGitHub/Natron/milestones?state=all&per_page=100>
- 2.6 open issues API: <https://api.github.com/repos/NatronGitHub/Natron/issues?state=open&milestone=17&per_page=100>
- 3.0 open issues API: <https://api.github.com/repos/NatronGitHub/Natron/issues?state=open&milestone=5&per_page=100>
- 4.0 open issues API: <https://api.github.com/repos/NatronGitHub/Natron/issues?state=open&milestone=2&per_page=100>
- Qt6 PR API: <https://api.github.com/repos/NatronGitHub/Natron/pulls/1019>
- Local Flux context: `AGENTS.md`, `ARCHITECTURE.md`, `plans/PHASES.md`, `tasks/TASKS.md` where available in the working tree/context.
