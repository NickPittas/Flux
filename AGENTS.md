# Flux — Project Agent Guidelines

This file governs all Forge agent behavior within the Flux workspace.

## Rule 0: Read First

Every agent working in this workspace MUST read this `AGENTS.md` file on **every single user request** before taking any action. Do not rely on memory of prior reads; re-read it every turn/request.

Every agent working in this workspace MUST also read:
3. `ARCHITECTURE.md` — Current project architecture (this file IS the source of truth for tech decisions)
4. `plans/PHASES.md` — Current phase status and breakdown
5. `tasks/TASKS.md` — Active task list with status

## Rule 1: Nick Owns High-Level Decisions

The agent MUST NOT make high-level product, UX, architecture, workflow, or repository-policy decisions without Nick's explicit approval.

Before any non-trivial change, the agent MUST:

1. Present a concrete plan.
2. State expected user-visible behavior and risks.
3. Wait for Nick's explicit approval of that plan.
4. Implement only the approved scope.

If new information invalidates the approved plan, STOP and ask Nick before changing direction.

## Rule 1A: Source-of-Truth Hierarchy for Delegated Work

Nick's direct commands and the original approved plan outrank task packets, subagent prompts, summaries, and review notes. Every subagent prompt and review must explicitly require checking work against that hierarchy. Any drift from Nick's commands or the approved plan is a blocker and must be escalated to Nick instead of silently substituting another workflow.

## Rule 2: Git Requires Explicit Approval

NEVER commit, amend, revert, reset, push, stage broad changes, or otherwise alter git history/state unless Nick explicitly asks for that exact git action.

- Do not commit because a task seems complete.
- Do not commit because validation passed.
- Do not revert commits or working-tree files as a recovery strategy without approval.
- If git state matters, inspect and report it; then wait for instructions.

## Rule 3: Scrutinize Is Not Git Diff Review

When asked to scrutinize/review/debug, do not rely only on `git diff`.

Required review behavior:

1. Read the underlying source files and relevant surrounding code.
2. Trace the actual runtime path end-to-end.
3. Infer and state the intent before judging the code.
4. Verify behavior against real code paths, not snippets.
5. Report findings with file/line evidence and concrete suggested changes.

`git diff` may be used only as an entry point, never as the full scope of review.

## Project Identity

**Flux** is a 2D motion graphics compositor for Linux, built as a fork of Natron (GPL2).

- Fork: Natron RB-2.6 (C++17, CMake, Qt5/6)
- Direction: Replace Natron's node-graph-only UI with a layer-based timeline UI (After Effects paradigm)
- The node graph remains accessible for power users

## Architecture Decision Record

### ADR-001: Qt over Electron (2026-05-20)

- **Decision**: Use Qt for the UI instead of Electron + React
- **Rationale**: Natron Engine is deeply coupled to Qt (QObject, signals/slots, QThread, QMutex). Removing Qt from the engine would be weeks of refactoring with high risk. Qt eliminates the IPC bridge bottleneck for pixel data transfer. No validation spikes needed — everything already works.
- **Consequence**: We keep Qt as a dependency. The UI is Qt widgets (modernized, restyled), not web technologies.

### ADR-002: Fork Natron, don't build from scratch (2026-05-20)

- **Decision**: Fork Natron RB-2.6 under GPL2
- **Rationale**: Natron provides years of battle-tested engine code: 32-bit float pipeline, OCIO, OIIO, FFmpeg, OpenFX host, cache, animation, roto, tracking. Building from scratch would take months before rendering a single frame.
- **Consequence**: Flux is GPL2. We inherit Natron's code quality (C++98 heritage, now C++17). Some technical debt comes with it.

### ADR-003: Layer-based timeline over node graph (2026-05-20)

- **Decision**: Primary UI is a layer-based timeline. Node graph is secondary (power-user feature).
- **Rationale**: After Effects proved this model works for motion graphics. Layers are UI abstractions over Natron nodes. The engine doesn't change — we add a translation layer.
- **Consequence**: Need to build: Timeline widget, Layer-to-Node bridge, Effects stack panel, Shape/Text layer types.

## Workflow

### Task Lifecycle (MANDATORY)

Every task follows this exact cycle:

```
1. READ task from tasks/TASKS.md
2. IMPLEMENT the task
3. REVIEW the code (self-review or subagent review)
4. TEST the code with real-world artifacts (images, videos)
5. REVIEW the test results
6. DEBUG if needed
7. REVIEW again
8. MARK task complete in tasks/TASKS.md
9. UPDATE plans/PHASES.md if phase progress changed
10. MOVE to next task
```

### Subagent Usage

- Use `forge` agents for implementation work
- Use `muse` agents for planning, analysis, and code review
- Always review subagent output before accepting
- Never trust subagent output blindly — verify with builds and tests

### File Organization

```
plans/
  PHASES.md          — Phase breakdown, status, dependencies
  phase-1.md         — Detailed phase 1 plan
  phase-2.md         — Detailed phase 2 plan (created when phase 1 nears completion)
  ...

tasks/
  TASKS.md           — Master task list with status
  T001-task-name.md  — Individual task detail files (when task is complex)
  ...
```

### Task Status Values

- `PENDING` — Not started
- `IN_PROGRESS` — Currently being worked on
- `REVIEW` — Implementation done, needs review
- `TESTING` — Being tested with real artifacts
- `BLOCKED` — Waiting on something
- `DONE` — Complete and verified
- `CANCELLED` — No longer needed

### Validation Requirements

Every feature must be validated with real-world test artifacts:

- **Import**: Test with actual .mov, .mp4, .mxf, .exr, .tiff, .psd, .png, .jpg, .svg files
- **Rendering**: Verify output matches expected results (visual comparison)
- **Performance**: Measure frame rates, cache hit rates, memory usage
- **Color**: Verify OCIO transforms produce correct colors (compare with reference)

For user-facing GUI work, **screenshot or it never happened**:

- A completed UI step must include screenshots and/or recordings of the actual controls working.
- Proof must show UI truth, not just rendered pixels: open dropdowns, changed controls, properties panel state, and viewport response when relevant.
- Do not use "validated" for GUI-control behavior unless the proof artifact shows the control itself and the resulting behavior.

Test assets should be stored in `tests/assets/` when the project reaches that phase.

## Code Standards

- **Language**: C++17 (matching Natron RB-2.6)
- **Build system**: CMake (matching Natron)
- **Qt version**: Qt5 (5.15+) or Qt6 (6.3+), configurable via CMake flag
- **Max file size**: 500 lines (from ARCHITECTURE.md — carry this forward)
- **Formatting**: Follow existing Natron `.clang-format` in the forked codebase
- **New code**: Follow the same patterns as the surrounding Natron code for consistency
- **Commits**: Conventional commits (`feat`, `fix`, `refactor`, `docs`, `test`, `chore`)

## What We Keep from Natron

| Directory | Action | Reason |
|---|---|---|
| `Engine/` | KEEP as-is | Core rendering, caching, nodes, effects, animation |
| `Global/` | KEEP as-is | Shared utilities |
| `HostSupport/` | KEEP as-is | OpenFX host abstraction |
| `libs/` | KEEP as-is | Eigen3, OpenFX, ceres, openMVG, etc. |
| `Renderer/` | KEEP as-is | Headless render entry point |
| `BreakpadClient/` | KEEP as-is | Crash reporting |
| `CrashReporter/` | KEEP as-is | Crash reporting |
| `Tests/` | KEEP as-is | Existing test suite |
| `Documentation/` | KEEP as reference | Natron docs |
| `Gui/` | REDESIGN | Strip and rebuild with Flux UI |
| `App/` | REDESIGN | New application shell |
| `Shiboken/` | EVALUATE | May keep if we want Python bindings |
| `PythonBin/` | EVALUATE | May keep for scripting |

## What We Build New

1. **Flux Application Shell** (`App/` replacement) — New main window, menu bar, panel management
2. **Timeline Panel** — Layer-based timeline widget (the core differentiator)
3. **Layer-to-Node Bridge** — Translates timeline operations to Natron node graph operations
4. **Effects Stack Panel** — Per-layer effect list (vertical stack, not node graph)
5. **Modern Dark Theme** — Qt stylesheet, After Effects-inspired look
6. **Shape Layers** — Rect, ellipse, star, bezier paths
7. **Text Layers** — Text rendering with per-character animation
8. **Improved Cache** — Persistent disk cache, background rendering
9. **Export Templates** — Saveable export configurations

## Key Natron Classes to Understand

| Class | File | Purpose |
|---|---|---|
| `AppManager` | `Engine/AppManager.h` | Application controller singleton |
| `AppInstance` | `Engine/AppInstance.h` | Per-project instance |
| `Node` | `Engine/Node.h` | A node in the compositing graph |
| `EffectInstance` | `Engine/EffectInstance.h` | Base class for all effects/nodes |
| `Image` | `Engine/Image.h` | 32-bit float image buffer |
| `Cache<>` | `Engine/Cache.h` | LRU cache (RAM + disk) |
| `TimeLine` | `Engine/TimeLine.h` | Timeline state (time, frame range) |
| `ViewerInstance` | `Engine/ViewerInstance.h` | Viewer node (renders to display) |
| `OfxEffectInstance` | `Engine/OfxEffectInstance.h` | OpenFX plugin wrapper |
| `Knob` | `Engine/Knob.h` | Parameter system (animatable properties) |
| `Curve` | `Engine/Curve.h` | Animation curve (keyframes + interpolation) |
| `RotoContext` | `Engine/RotoContext.h` | Rotoscoping context |
