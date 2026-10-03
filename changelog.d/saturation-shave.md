### session · mastering — what the saturation took off the peaks, over time (03.10)

- **`QueryKind::SaturationShave`** (appended, 13): per master, on `LimiterGr`'s very buckets
  (`[firstFrame,lastFrame,minDb,maxDb,meanDb,samples,nonFinite]`), the dB the saturation — the chain's soft clipper —
  took off the peaks, ≥ 0. Per internal quantum (256 frames): the stage's input peak, aligned on its own latency, times
  its clean gain (dry share, the shape's slope at zero under the drive compensation, the output trim), against its
  output peak, floored at 0 — only what the curvature took, the pair the report's `saturationCutMaxDb` reads, so the
  largest bucket is that number. Base-rate peaks: the oversampled copies live inside the Saturator. The soft clipper
  sits after the glue and before the landing gain, the limiter and its peak clipper. A master whose soft clipper does
  not shape answers `Unavailable` with reason `NoSignal`, as `GlueGr` does.
- `MasteringChainTaps` appends `clipperShaveDb` (per baseband frame, under `frameCapacity`); `LoudnessSolution` appends
  `saturationTrace`, built on every render like the other three, and the solver's memory formulas count its trace and
  its tap. The session keeps the trace with the master's rows only where the soft clipper shapes (a bucket row in the
  master's declared memory). No output sample moves.
- **`FC_SESSION_ABI_VERSION` 8**: a shell that reads the new query kind knows it by this version.
