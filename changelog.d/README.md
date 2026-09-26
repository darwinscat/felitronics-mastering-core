<!-- SPDX-License-Identifier: AGPL-3.0-or-later -->

# `changelog.d/` — one file per task, so two branches never collide

Every branch that changes behaviour writes its release note **here, as a new file**, and leaves
`CHANGELOG.md` alone. Git cannot conflict on two branches adding two different files; it conflicts every
single time they append to the same section of the same file, which is what `## Unreleased` used to be.
felitronics-core adopted this after five rebase rounds in one day; this repository lost the directory in the
split and was back to appending to `## Unreleased`, so every pair of open branches conflicted again.

**Name the file for the task**: `tempo.md`, `analyzer-split-invariance.md` — a couple of words, no internal
task id. A task that ships in several items gets one file per item, numbered: `<task>-0-<item>.md`,
`<task>-1-<item>.md`, … The name decides the order the notes appear in the release: files sort by
name, with runs of digits compared as numbers (`-2` before `-10`). A later commit of the same task edits its own
file; it does not edit another task's, which may be released without it.

**Write exactly what used to go under `## Unreleased`** and nothing more: a `### module · module — the
headline` line, then the body. No version number, no date, no `## Unreleased` heading — the release adds
those.

**At release time** `node tools/changelog-collect.mjs --release vX.Y.Z` folds every fragment into
`CHANGELOG.md` under the new version heading, deletes them, and moves `project(felitronics_mastering_core
VERSION ...)` in `CMakeLists.txt` to the same number, in one commit on the release branch, where there is nobody
to conflict with. `--preview` prints what the next release would say without touching anything — that is how
you read the accumulated notes now that they live apart.
