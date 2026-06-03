# Locator Report

## Summary
T085 is mostly complete technically; remaining closure is a final clean warning baseline/classification artifact plus task/status documentation, not more broad code cleanup.

## Confidence
high

## Relevant Locations
1. `file:///home/npittas/Flux/tasks/T085-build-warning-audit.md`
   - symbol: T085 task doc
   - approximate lines: 1-245
   - stable anchor: `## Remaining Work`
   - why relevant: canonical task detail still says `IN_PROGRESS`; records baseline reduction 908 → 32 warning lines.
   - evidence: pass4 baseline artifact listed as `build-logs/flux-warning-pass4-20260526-160824.warnings.txt`; remaining work list still stale/partly resolved.

2. `file:///home/npittas/Flux/tasks/TASKS.md`
   - symbol: T085 row
   - approximate lines: Cross-phase Infrastructure Tasks
   - stable anchor: `T085 | Build warning audit and cleanup`
   - why relevant: master status still `IN_PROGRESS`.

3. `file:///home/npittas/Flux/build-logs/flux-warning-pass4-20260526-160824.warnings.txt`
   - symbol: final current warning baseline
   - approximate lines: 1-32
   - stable anchor: `Engine/AppManager.cpp:2945`
   - why relevant: current best preserved clean baseline has 32 warning lines.
   - evidence: remaining warnings are inherited libs/qhttpserver plus Python 3.14 deprecations.

4. `file:///home/npittas/Flux/CMakeLists.txt`
   - symbol: global warning flags
   - approximate lines: 118-126
   - stable anchor: `$<$<COMPILE_LANGUAGE:CXX>:-Wno-deprecated-copy>`
   - why relevant: confirms C-only warning was fixed via CXX-scoped option.

5. `file:///home/npittas/Flux/Engine/CMakeLists.txt`
   - symbol: `PyEngine_SOURCES`
   - approximate lines: 42-49
   - stable anchor: `set_property(SOURCE ${PyEngine_SOURCES} APPEND PROPERTY COMPILE_OPTIONS`
   - why relevant: generated Shiboken suppression is already source-scoped.

6. `file:///home/npittas/Flux/Gui/CMakeLists.txt`
   - symbol: `PyGui_SOURCES`
   - approximate lines: 56-63
   - stable anchor: `-Wno-cast-function-type`
   - why relevant: generated GUI Shiboken warning suppression is already source-scoped.

7. `file:///home/npittas/Flux/libs/CMakeLists.txt`
   - symbol: `natron_suppress_third_party_warnings`, `natron_suppress_third_party_c_warnings`
   - approximate lines: 29-65
   - stable anchor: `function(natron_suppress_third_party_warnings target_name)`
   - why relevant: inherited/third-party warning suppression exists but pass4 still shows unsuppressed C warnings/classes.

## Allowed Edit Scope Recommendation
- Documentation/status only unless Nick explicitly wants another cleanup pass:
  - `tasks/T085-build-warning-audit.md`
  - `tasks/TASKS.md`
  - optionally `plans/PHASES.md` only if phase progress/status changes.
- If doing low-risk final code fixes, limit to CMake suppression for remaining inherited C warnings in `libs/CMakeLists.txt`; avoid source edits in third-party libs.

## Read-Only Context Recommendation
- Keep `build-logs/flux-warning-pass4-20260526-160824.log`
- Keep `build-logs/flux-warning-pass4-20260526-160824.warnings.txt`
- Compare older `flux-warning-current-*` only as historical, because pass4 supersedes it.

## Validation Targets
- tests:
  - no unit test target needed for documentation closure.
- commands:
  - `timeout 1800 bash -lc 'cmake --build build --target clean && cmake --build build --target Natron -- -j"$(nproc)" && cmake --build build --target NatronRenderer -- -j"$(nproc)"'`
  - `grep -nE 'warning:|\\[-W[^]]+\\]' build-logs/<new-final>.log > build-logs/<new-final>.warnings.txt`
  - `wc -l build-logs/<new-final>.warnings.txt`
  - `timeout 300 cmake --build build/openfx-flux -- -j"$(nproc)"`
  - `git diff --check`
- manual checks:
  - Confirm final warning count/classification is pasted into T085.
  - Mark T085 `DONE` only if 32-ish remaining warnings are accepted as documented baseline.

## Risks / Unknowns
- Codemap was stale due commit change and untracked `build-logs/`; I updated it as safe for this locator task.
- I did not write `/home/npittas/Flux/build-logs/locator-T085-warning-audit-closure.md` because this role is explicitly no-edit/read-only.
- Pass4 warning baseline still includes Python 3.14 deprecations in Flux-owned files; fixing them may be non-trivial because Python initialization APIs changed and require compatibility judgment.

## Stop Recommendation
Implementation can proceed now as a documentation/status closure task. Do not launch broad warning cleanup unless Nick wants warning count below the current 32-line baseline.