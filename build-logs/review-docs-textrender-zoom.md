# Review Report

## Verdict
fix-then-ship

## Scope Compliance
- failed
- evidence: requested doc scope was `ARCHITECTURE.md`, `INSTALL_FLUX_LINUX.md`, `plans/PHASES.md`, `tasks/TASKS.md`, `tasks/FLUX_MOTION_TEXT_RECOVERY_SOURCE_OF_TRUTH.md`, but `git diff --name-only` also shows many non-doc/code files (for example `openfx-flux/TextRender/TextRender.cpp`, `openfx-flux/TextRender/TextRasterizer.cpp`, `Gui/FluxTimeline.cpp`, `tools/linux/flux-linux-setup.sh`). Do not include those if this commit is meant to be docs-only.

## Validation Assessment
- command/result reviewed: `git diff -- ...docs...`; `grep -RIn "scaled project\|RoD\|destination bounds\|viewer zoom\|proxy\|hash\|FluxTextRender.ofx\|--deploy-extras\|--build" ...docs...`
- sufficient? yes for documentation content review; no build/test needed for docs-only.
- missing validation: none for doc wording. Commit scope still needs separation if this is a docs-only commit.

## Findings

### Major
- Finding: Working tree contains out-of-scope non-doc changes alongside the requested documentation files.
- Why it matters: A docs-before-commit review cannot approve a docs-only commit if unrelated code/tooling edits are included by accident.
- Evidence: `git diff --name-only` includes `openfx-flux/TextRender/TextRender.cpp`, `openfx-flux/TextRender/TextRasterizer.{h,cpp}`, many `Gui/`/`Engine/` files, and `tools/linux/flux-linux-setup.sh`, beyond the five requested docs.
- Suggested change: Commit only the five requested documentation files, or explicitly make this a combined bugfix+docs commit and review the code separately.

## Simpler Alternative Check
No smaller doc change is needed for the requested facts: `ARCHITECTURE.md:179`, `plans/PHASES.md:270`, and the recovery ledger cover the invariant; `INSTALL_FLUX_LINUX.md:247-262` covers the deployment/hash pitfall.

## Final Recommendation
Ship the doc wording after ensuring the commit/staging set is limited to the intended files.

## Exact doc edits needed
None. The requested documentation is present:
- `ARCHITECTURE.md:179` states scaled project/RoD layout, host destination-bounds writes, and viewer zoom scaling the finished image rather than relayout.
- `INSTALL_FLUX_LINUX.md:247-262` states use installer `--build` or copy `build/openfx-flux/FluxTextRender.ofx` into `plugins/FluxTextRender.ofx.bundle/...` before `--deploy-extras`, then verify matching hashes.
- `plans/PHASES.md:261,270`, `tasks/TASKS.md:150`, and `tasks/FLUX_MOTION_TEXT_RECOVERY_SOURCE_OF_TRUTH.md:1147-1153` record the accepted viewer zoom/proxy behavior and deployment lesson.
