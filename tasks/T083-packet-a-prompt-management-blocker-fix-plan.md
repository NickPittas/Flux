# Planner Report

## Status
ready

## Rationale
This plan is narrowly scoped to the Packet A review blocker: `AIPaint` public prompt mutation methods currently bypass the persistent `aiPaintPromptStore` and overlay redraw path used by button/overlay interactions. The fix stays inside Packet A Engine files and treats existing Gui/SAM working-tree changes as baseline, with no GUI/SAM edits planned.

# Task Packet

## User Goal
Ensure every public AI Paint prompt mutation API added by Packet A persists through `aiPaintPromptStore` and requests overlay redraw consistently, or remove/narrow public APIs if not needed. Do not touch broader T083 Gui/SAM changes.

## Mode
general-coding

## Relevant Locations
- file: `Engine/AIPaint.h`
  symbol: `AIPaint` public prompt mutation API
  approximate lines: 72-77
  stable anchor: `bool selectPrompt(int id);`, `bool clearPromptSelection();`, `bool deletePrompt(int id);`, `bool deleteSelectedPrompt();`
  reason: Public mutation methods added by Packet A must not leave serialized prompt state stale.
  confidence: high
- file: `Engine/AIPaint.cpp`
  symbol: `AIPaint::selectPrompt`, `AIPaint::clearPromptSelection`, `AIPaint::deletePrompt`, `AIPaint::deleteSelectedPrompt`
  approximate lines: 169-189
  stable anchor: `return _imp->context.selectPrompt(id);`
  reason: These methods directly mutate `_imp->context` without updating `aiPaintPromptStore` or redrawing overlays.
  confidence: high
- file: `Engine/AIPaint.cpp`
  symbol: `AIPaintPrivate::promptStore`, `AIPaint::knobChanged`, overlay prompt add/select paths
  approximate lines: 119, 391-424, 584-651
  stable anchor: `_imp->promptStore`, `store->setValue(_imp->context.serialize(), ViewSpec::all(), 0, true);`, `redrawOverlayInteract();`
  reason: Existing button/overlay paths show the intended persistence/redraw behavior to reuse for public APIs.
  confidence: high
- file: `Engine/AIPaintContext.h`
  symbol: `AIPaintContext` mutation API
  approximate lines: 70-78
  stable anchor: `bool selectPrompt(int id);`, `bool clearSelection();`, `bool deletePrompt(int id);`, `bool deleteSelectedPrompt();`
  reason: Context methods are storage-only mutations; persistence belongs in `AIPaint` wrapper.
  confidence: high
- file: `Engine/AIPaintContext.cpp`
  symbol: `AIPaintContext::selectPrompt`, `clearSelection`, `deletePrompt`, `deleteSelectedPrompt`, `serialize`
  approximate lines: 158-247
  stable anchor: `prompt.selected = selected;`, `_prompts.erase`, `object.insert(QString::fromUtf8("selected"), prompt.selected);`
  reason: Confirms selected/deleted state serializes once `AIPaint` writes the prompt store.
  confidence: high
- file: `tasks/T083-ai-paint-live-sam3-workflow-plan.md`
  symbol: `Task Packet A — AI Paint prompt selection/delete/clear`
  approximate lines: 50-117
  stable anchor: `Allowed Edit Files` and `persisted in AI Paint's prompt store`
  reason: Original approved scope limits this blocker fix to Packet A Engine files and prompt-management behavior.
  confidence: high

## Allowed Edit Files
- `Engine/AIPaint.h`
- `Engine/AIPaint.cpp`

## Read-Only Context Files
- `Engine/AIPaintContext.h`
- `Engine/AIPaintContext.cpp`
- `tasks/T083-ai-paint-live-sam3-workflow-plan.md`

## Required Change
In `Engine/AIPaint.cpp`, centralize the post-mutation behavior for public prompt mutation APIs so any successful mutation writes `_imp->context.serialize()` into `_imp->promptStore` using the existing `store->setValue(..., ViewSpec::all(), 0, true)` pattern and then calls `redrawOverlayInteract()`.

Implement the smallest safe fix:
1. Add a private/internal helper in `AIPaint.cpp` near the existing public prompt methods, e.g. `persistPromptsAndRedraw()` or equivalent, that locks `_imp->promptStore`, writes serialized context when available, and requests overlay redraw.
2. Update `AIPaint::selectPrompt(int id)`, `clearPromptSelection()`, `deletePrompt(int id)`, and `deleteSelectedPrompt()` so they:
   - call the corresponding `AIPaintContext` method,
   - return `false` without side effects if the context did not change,
   - on `true`, persist to `aiPaintPromptStore`, request overlay redraw, and return `true`.
3. Keep `AIPaint::selectedPromptId()` and `AIPaint::getPrompts()` read-only and unchanged.
4. Do not alter `AIPaintContext` unless a compile error proves a signature mismatch; the context layer should remain serialization-capable but not responsible for knob persistence or overlay redraw.
5. Do not edit any `Gui/` files, `tools/ai/` files, SAM worker/protocol files, task status files, generated outputs, or lockfiles.

If the worker determines these public mutation APIs are unused and should be narrowed/removed instead, stop and report that decision point rather than removing them without approval; the reviewer goal allows removal/narrowing only if clearly not needed, but current Packet A/C context likely expects a public Engine API.

## Non-Goals
- No Gui/SAM/AI Panel changes; existing broader T083 working-tree changes are baseline and out of scope.
- No new prompt model behavior, hit-testing changes, UI controls, or serialization schema changes.
- No changes to SAM3 worker, live preview scheduling, AI Work Viewer preview, or generated AI outputs.
- No task status/phase document updates.
- No git staging, commits, reverts, resets, or cleanup of unrelated working-tree changes.

## Validation
Commands:
- `cmake --build /home/npittas/Flux/build --target Natron -j$(nproc)`
- If a focused test target exists for Engine or AI Paint in the local build, run it after the build; otherwise report that no focused automated test was found and perform the manual inspection below.

Manual check:
- Inspect `Engine/AIPaint.cpp` and verify every public mutating method (`selectPrompt`, `clearPromptSelection`, `deletePrompt`, `deleteSelectedPrompt`) persists via `aiPaintPromptStore` and calls `redrawOverlayInteract()` only after a successful mutation.
- Optional GUI smoke if practical: create/select/delete/clear prompts, save/reopen, and verify prompt selection/deletion persists and overlay updates.

Expected result:
Build succeeds; public AI Paint prompt mutation APIs can no longer leave in-memory context diverged from project serialization, and overlays redraw consistently after successful public mutations.

## Stop Conditions
Stop and report if:
- target symbol is missing
- required fix exceeds allowed files
- validation cannot run
- existing architecture contradicts the requested change
- task requires product/design judgment not in packet
- fixing the blocker requires editing `Gui/`, `tools/ai/`, generated outputs, lockfiles, or any file outside `Engine/AIPaint.h` and `Engine/AIPaint.cpp`
- the public mutation APIs appear truly unused and removal/narrowing is preferred over persistence; request orchestrator/Nick approval before API removal

## Planner Self-Check
- locator evidence sufficient: yes — exact current blocker methods and existing persistence/redraw paths were inspected in authorized files.
- allowed edit files minimal and explicit: yes — only `Engine/AIPaint.h` and `Engine/AIPaint.cpp`; likely only `.cpp` needs changes, header included for API narrowing only if approved/necessary.
- read-only context minimal: yes — limited to context storage/serialization files and original Packet A plan.
- anchors/lines included: yes — each relevant location includes path, symbol, approximate lines, stable anchor, reason, and confidence.
- validation concrete: yes — build command plus focused manual inspection and optional GUI smoke check.
- parallelization decision explicit and safe: yes — single task; no parallelization recommended because all work targets one implementation file and validation uses shared build state.
- non-goals and stop conditions sufficient: yes — explicitly excludes Gui/SAM scope contamination and broader T083 changes.
- reviewer findings addressed, if revision: yes — real blocker is addressed by mandating store persistence/redraw for public APIs; scope contamination warning is addressed by limiting edits to Packet A Engine files and no Gui/SAM changes.

## Required Return Contract
Return only a task-focused summary. Do not include transcript, tool logs, raw file dumps, large code blocks, or broad unrelated issues. Include status, files inspected/changed, validation evidence, blockers, and task-specific risks.
