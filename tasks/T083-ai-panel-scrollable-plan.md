# Planner Report

## Status
ready

## Rationale
Locator evidence and authorized context isolate this to `FluxAiPanel::setupUi()` in `Gui/FluxAiPanel.cpp`: the AI Panel currently installs a direct `QVBoxLayout` on the panel widget, with no `QScrollArea` and no progress-control surface. A single scoped worker can add scroll containment without touching AI worker/progress behavior or changing existing Run/Apply/Load/Unload/Live Preview/history/log logic.

# Task Packet

## User Goal
Make the Flux AI Panel scrollable so its existing controls keep natural height and are not compacted in a cramped dock. This is Packet C1 scroll-only: do not add temporal worker/progress controls yet.

## Mode
general-coding

## Relevant Locations
- file: `Gui/FluxAiPanel.cpp`
  symbol: `FluxAiPanel::setupUi()`
  approximate lines: 312-405
  stable anchor: `void FluxAiPanel::setupUi()` followed by `QVBoxLayout* layout = new QVBoxLayout(this);`
  reason: Constructs all AI Panel controls directly on the panel layout; must be wrapped in a scrollable content widget while preserving existing controls, connections, and behavior.
  confidence: high
- file: `Gui/FluxAiPanel.cpp`
  symbol: include block
  approximate lines: 1-18
  stable anchor: `#include <QVBoxLayout>` / `#include <QHBoxLayout>`
  reason: Add the Qt widget include(s) required for scroll containment, such as `QScrollArea`, if used by the implementation.
  confidence: high
- file: `Gui/FluxAiPanel.h`
  symbol: `class FluxAiPanel` members
  approximate lines: 33-130
  stable anchor: member declarations ending in `_log` and worker/process fields
  reason: Confirms no persistent scroll-area member is currently declared; avoid header changes unless absolutely necessary for lifetime/access needs.
  confidence: high

## Allowed Edit Files
- `Gui/FluxAiPanel.cpp`
- `Gui/FluxAiPanel.h` only if the implementation truly requires a persistent member; prefer no header change for a local scroll area parented by Qt.

## Read-Only Context Files
- `Gui/FluxAiPanel.cpp`
- `Gui/FluxAiPanel.h`
- `ARCHITECTURE.md` (mandatory project context; Qt widgets are the approved UI stack)
- `plans/PHASES.md` (mandatory project phase context)
- `tasks/TASKS.md` (mandatory project task context)

## Required Change
In `FluxAiPanel::setupUi()`, replace the direct top-level content layout on `this` with a scroll-area structure:

1. Create a top-level `QVBoxLayout` on `this` with the existing margins/spacing or equivalent outer spacing.
2. Create a `QScrollArea` parented to the panel, set it widget-resizable, and configure it for vertical scrolling when content exceeds dock height.
3. Create a content `QWidget` inside the scroll area and move the current AI Panel layout/content onto that content widget.
4. Preserve all existing widgets and signal/slot connections exactly: task/model/source/output/status/prompt labels, Run/Apply/Cancel, result history, Preview Again, Remove Entry, Show Log, log visibility/toggle behavior, and any existing Load/Unload/Live Preview controls reachable through this panel/task path.
5. Keep controls at natural/minimum height; do not squeeze the entire panel contents to fit a short dock. The content should scroll instead.
6. The log may remain the resizable/stretchy area inside the content, but the overall panel content must scroll when the dock is too short.
7. Do not add `QProgressBar`, progress controls, temporal worker UX, or behavior changes in this packet.

Implementation hint: use an outer layout such as `QVBoxLayout* outerLayout = new QVBoxLayout(this);`, a `QScrollArea`, and a `QWidget* content = new QWidget(scrollArea);` with the existing `QVBoxLayout` attached to `content`; add the scroll area to the outer layout. Keep ownership through Qt parents.

## Non-Goals
- Do not add a `QProgressBar` or any temporal worker/progress UI.
- Do not alter AI execution, SAM3 load/unload, live preview, history, manifest, logging, or button enablement logic.
- Do not redesign labels, wording, model/task choices, history behavior, or log toggle behavior.
- Do not edit unrelated AI worker, viewer, timeline, serialization, build, or task-tracking files.
- Do not commit, stage, revert, or otherwise alter git state.

## Validation
Commands:
- `cmake --build build --target Natron -j$(nproc)`
- Launch Flux/Natron from the existing build/run workflow and open the AI Panel in a cramped/narrow dock.

Expected result:
- Build passes with no new Qt include or layout compile errors.
- GUI proof artifact is captured: screenshot or short recording showing the AI Panel in a cramped dock with a visible vertical scrollbar and existing controls still present/not compacted. The proof must show UI truth, not only rendered output; include enough of the panel to confirm Run/Apply/Cancel/history/log area or equivalent existing AI controls remain accessible by scrolling.
- Manual smoke check confirms Run/Apply/Cancel, result history selection/buttons, log toggle, and any existing Load/Unload/Live Preview controls still appear and behave as before.

## Stop Conditions
Stop and report if:
- `FluxAiPanel::setupUi()` or the direct-layout anchor is missing or has materially changed.
- The scroll-only fix requires edits outside the allowed files.
- Preserving existing controls requires product/design decisions not specified here.
- The implementation appears to require adding progress/worker controls to satisfy the request.
- Build or GUI launch validation cannot run in the available environment.
- Existing architecture contradicts using Qt `QScrollArea` for this widget.

## Planner Self-Check
- locator evidence sufficient: yes — run e37084e1 and authorized reads identify `FluxAiPanel::setupUi()` as direct `QVBoxLayout` with no `QScrollArea`/`QProgressBar`.
- allowed edit files minimal and explicit: yes — implementation should be `Gui/FluxAiPanel.cpp` only; `Gui/FluxAiPanel.h` is conditional and discouraged unless needed.
- read-only context minimal: yes — limited to authorized AI Panel files plus mandatory project context files.
- anchors/lines included: yes — include block and `setupUi()` line ranges are cited.
- validation concrete: yes — build command plus GUI screenshot/recording proof and smoke checks.
- parallelization decision explicit and safe: yes — single task; no parallelization needed because the change is one coherent widget-layout edit in one source file.
- non-goals and stop conditions sufficient: yes — explicitly blocks temporal progress controls and unrelated behavior changes.
- reviewer findings addressed, if revision: not applicable — no reviewer findings were supplied for this plan.

## Required Return Contract
Return only a task-focused summary. Do not include transcript, tool logs, raw file dumps, large code blocks, or broad unrelated issues. Include status, files inspected/changed, validation evidence including screenshot/recording path if produced, blockers, and task-specific risks.
