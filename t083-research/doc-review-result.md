# Review Report

## Verdict
fix-then-ship

## Scope Compliance
- failed
- evidence: `git status --short` shows expected T083 tracking files plus unrelated modified files: `tasks/FLUX_MOTION_TEXT_RECOVERY_SOURCE_OF_TRUTH.md` and `tasks/T084-plugin-payload-discovery.md`; `git diff --stat` shows those files changed. The expected changed files were only `tasks/T083-ai-matte-depth.md`, `tasks/TASKS.md`, and `plans/PHASES.md`. No code or installer script changes were found in `git diff --name-only`.

## Validation Assessment
- command/result reviewed: `git status --short`, `git diff --stat`, `git diff --name-only`, direct reads of `tasks/T083-ai-matte-depth.md`, `tasks/TASKS.md`, and `plans/PHASES.md` with line numbers.
- sufficient? yes for documentation/tracking review; no build/test required because this packet is documentation-only and no code/installer scripts were changed.
- missing validation: none for the requested documentation checks.

## Findings

### Blocker
- Finding: The working tree includes out-of-scope documentation edits unrelated to T083.
- Why it matters: The task packet/expected files limit this review to T083 documentation/tracking; unrelated T084/recovery-source edits exceed scope and make the change set unsafe to accept as a T083-only update.
- Evidence: `git diff --stat` lists changes to `tasks/FLUX_MOTION_TEXT_RECOVERY_SOURCE_OF_TRUTH.md` and `tasks/T084-plugin-payload-discovery.md` in addition to `plans/PHASES.md` and `tasks/TASKS.md`; the diff changes T084 status/closure text in `tasks/T084-plugin-payload-discovery.md:1-9` and recovery log text around `tasks/FLUX_MOTION_TEXT_RECOVERY_SOURCE_OF_TRUTH.md:1155`.
- Suggested change: Split or revert the unrelated T084/recovery-source edits from this T083 documentation change set unless Nick explicitly wants them included in the same review scope.

### Major
- Finding: `tasks/T083-ai-matte-depth.md` says “T083-0/T083-1 planning complete” while milestone `T083-0` remains `IN_PROGRESS` and `T083-1` remains `PENDING`.
- Why it matters: The restart protocol depends on statuses/current stage; contradictory status text can cause the next worker to skip or start the wrong milestone.
- Evidence: Current stage text at `tasks/T083-ai-matte-depth.md:5` says `T083-0/T083-1 planning complete`; milestone statuses show `T083-0` is `IN_PROGRESS` at `tasks/T083-ai-matte-depth.md:40-47` and `T083-1` is `PENDING` at `tasks/T083-ai-matte-depth.md:49-56`. Restart rule says pick the first `PENDING` only after any `IN_PROGRESS` milestone is completed or blocked at `tasks/T083-ai-matte-depth.md:164-168`.
- Suggested change: Make the current stage match the milestone table, e.g. “T083-0 in progress; implementation not started,” or mark T083-0 complete if that is the intended state and allowed status vocabulary supports it.

## Passed Checks
- Nick-approved decisions are recorded exactly enough in `tasks/T083-ai-matte-depth.md:7-13`: SAM3.1/SAM3+ user-downloadable/not bundled, MatAnyone2 FOSS/non-commercial warning, DepthCrafter same policy, RVM/XMem2 external subprocess helpers, and unsaved projects force Save As.
- Implementation surfaces, validation/evidence, and stop conditions are present per milestone in `tasks/T083-ai-matte-depth.md:40-146`, with a validation matrix at `tasks/T083-ai-matte-depth.md:148-160` and restart protocol at `tasks/T083-ai-matte-depth.md:162-169`.
- `tasks/TASKS.md:153` marks T083 `IN_PROGRESS`, started `2026-05-26`, and points to `tasks/T083-ai-matte-depth.md`.
- `plans/PHASES.md:266` points to `tasks/T083-ai-matte-depth.md` and records the approved direction.
- No code or installer scripts appear in `git diff --name-only`; only markdown files are modified/tracked, with T083 research files untracked.

## Simpler Alternative Check
A smaller and safer T083-only change set exists: keep only `tasks/T083-ai-matte-depth.md`, the T083 row in `tasks/TASKS.md`, and the T083 line/follow-up in `plans/PHASES.md`, and align the current-stage wording with milestone statuses.

## Final Recommendation
Do not ship until the out-of-scope T084/recovery-source edits are separated and the T083 current-stage/status contradiction is fixed.
