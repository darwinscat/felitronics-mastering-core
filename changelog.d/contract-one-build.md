### contract — the site recordings come from one clean wasm build

- **Nine site recordings corrected** (`domains`, `load-measure`, `machine-layer`, `measurement-poison`, `memory`,
  `project-roundtrip`, `recovery`, `target-edits`, `two-sessions`): every scenario that places or poisons runs on the
  `contract-trap` module, and the committed recordings had been made with a trap module left from an older build. Its
  wasm32 memory budget was 80 bytes short — `measurementStorage.workspaceBytes` and `largestBlockBytes`, and
  `peakBytes`/`workPeakBytes` where the workspace sets the peak. Nothing else moved; these numbers are masked in the
  native/wasm comparison, so only the byte-for-byte recording check saw it, and only on a clean build.
- **`tools/wasm/build.sh` stamps what it built**: `contract-modules.sha256` beside the modules (production and
  `contract-trap`, for the release and the checked tier) names each module's sha256 and a digest of the sources, taken
  before the compile. `tools/contract/run.mjs` refuses a module pair the stamp does not name and a stamp made from other
  sources, with planted controls under `--controls`. The rebuild hint in the manifest now names
  `tools/wasm/build/fcsession.node.js`, the directory build.sh writes.
