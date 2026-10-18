### contract — the site recordings leave git and ride on the release

- `tools/contract/recordings/` keeps only its manifests in git: `manifest.json` for the session recordings and the new
  `wav-contract.manifest.json` for the WAV contract (the recording less its archive's base64). The recordings are build
  output: `run.mjs` and `wav-contract.mjs` write them once the run matches the manifests, and fail as stale when it does
  not, as before. The recordings had been rewritten whole by every release — 60 MB of text over 119 commits.
- A release tag builds them from its own tree and attaches them to the tag's GitHub release
  (`.github/workflows/recordings.yml`, `tools/contract/pack-recordings.mjs`): `recording-<scenario>.json.gz`,
  `recording-wav-contract.json.gz` and the two manifests. `workflow_dispatch` attaches them to a tag already out, those
  from before this change included. The site downloads them by version and checks each against its own copy of the
  manifests.
- The WAV recording's engine-free replay (`wav-recording-check.mjs`) runs inside `wav-contract.mjs` on the recording it
  has just written; the ctest `felitronics_wav_recording_replay`, which read the tracked file, is gone.
