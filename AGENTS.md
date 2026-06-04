# Flux — Project Agent Guidelines

This file governs all Forge agent behavior within the Flux workspace.

## Rule 0: Read First

Every agent working in this workspace MUST read this `AGENTS.md` file on **every single user request** before taking any action. Do not rely on memory of prior reads; re-read it every turn/request.

Every agent working in this workspace MUST also read:
3. `ARCHITECTURE.md` — Current project architecture (this file IS the source of truth for tech decisions)
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

## Project Identity

**Flux** is a 2D motion graphics compositor for Linux, built as a fork of Natron (GPL2).

- Fork: Natron RB-2.6 (C++17, CMake, Qt5/6)
- Direction: Replace Natron's node-graph-only UI with a layer-based timeline UI (After Effects paradigm)
- The node graph remains accessible for power users
