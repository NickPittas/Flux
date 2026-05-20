# Flux — Master Task List

Last updated: 2026-05-20 (T009.5 done, P1 in progress)

## Active Phase: P1 (Fork & Build)

### P0 Tasks

| ID | Task | Status | Assigned | Started | Completed | File |
|---|---|---|---|---|---|---|
| T001 | Create AGENTS.md with project guidelines | DONE | forge | 2026-05-20 | 2026-05-20 | — |
| T002 | Create plans/PHASES.md with phase breakdown | DONE | forge | 2026-05-20 | 2026-05-20 | — |
| T003 | Create tasks/TASKS.md with task tracking | DONE | forge | 2026-05-20 | 2026-05-20 | — |
| T004 | Create plans/phase-1.md with detailed P1 plan | DONE | forge | 2026-05-20 | 2026-05-20 | — |
| T005 | Update ARCHITECTURE.md for Qt+Natron approach | DONE | forge | 2026-05-20 | 2026-05-20 | — |
| T006 | Fork Natron RB-2.6 into Flux repo | DONE | forge | 2026-05-20 | 2026-05-20 | — |

---

## P1 Tasks (Fork & Build)

| ID | Task | Status | Assigned | Started | Completed | File |
|---|---|---|---|---|---|---|
| T007 | Install Natron build dependencies on Linux | DONE | forge | 2026-05-20 | 2026-05-20 | — |
| T008 | Build Natron from source (gui-sbk6 branch + Qt6) | DONE | forge | 2026-05-20 | 2026-05-20 | — |
| T009 | Run Natron GUI, verify it works | DONE | forge | 2026-05-20 | 2026-05-20 | — |
| T009.5 | Build & install OpenFX plugins (IO + Misc), validate file import + OCIO | DONE | forge | 2026-05-20 | 2026-05-20 | — |
| T010 | Build Engine/ as shared library (strip Gui/) | PENDING | — | — | — | — |
| T011 | Test headless render via Renderer/ process | PENDING | — | — | — | — |
| T012 | Document key Engine classes for Flux | PENDING | — | — | — | — |
| T013 | Validate import: test with real mov/mp4/mxf files | PENDING | — | — | — | — |
| T014 | Validate import: test with real exr/tiff/png/jpg/psd files | PENDING | — | — | — | — |
| T015 | Validate OCIO: test color transforms with real config | PENDING | — | — | — | — |
| T016 | Validate cache: test RAM/disk cache with real footage | PENDING | — | — | — | — |
| T017 | Validate playback: test real-time playback performance | PENDING | — | — | — | — |
| T018 | Set up Flux development workflow (build, test, run) | PENDING | — | — | — | — |

---

## P2 Tasks (UI Shell)

_To be planned when P1 nears completion._

---

## P3 Tasks (Timeline)

_To be planned when P2 nears completion._

---

## P4 Tasks (Effects + Properties)

_To be planned when P3 nears completion._

---

## P5 Tasks (Import/Export)

_To be planned when P4 nears completion._

---

## P6 Tasks (Shapes + Text)

_To be planned when P5 nears completion._

---

## P7 Tasks (Polish + Cache)

_To be planned when P6 nears completion._

---

## Task Detail Files

Complex tasks get their own detail file in `tasks/T###-task-name.md`. The file contains:
- Objective
- Approach
- Acceptance criteria
- Test plan
- Results
- Notes

Simple tasks don't need a detail file — the row above is sufficient.
