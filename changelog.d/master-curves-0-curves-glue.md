### session — a master's loudness curves in the source's frames; the glue's gain reduction over time

- **`Momentary` and `ShortTerm` with a `masterId` are asked and named in the source's frames**: the source's own
  request with a `masterId` added answers the master's curve at the source's rows, so on A/B the master's curve lies
  under the original's. The range is refused past the source's end and `sampleRate` is the source's rate. At the
  source's rate nothing changes (the delivered audio is the source's length, the chain's latency cut off, delivered
  frame n is source frame n). A converted master (a target with its own `sampleRate`) used to answer in delivered
  frames; a row ending at delivered frame e is now named `round(e·source/delivery)` — the source's own row on rates
  that are whole multiples of 100 Hz. The readings are unchanged: the job's meter over the delivered audio, kept with
  the master and in its declared memory.
- **`QueryKind::GlueGr`** (appended, 12): the glue's — the compressor's — gain reduction over time from the delivered
  render, rows as `LimiterGr`'s (`[firstFrame,lastFrame,minDb,maxDb,meanDb,samples,nonFinite]`) on its very buckets.
  The trace is the one the landing search already took from the compressor's tap on every render; the session now
  keeps it with the master (a third bucket row in the master's declared memory, only where the glue compressed), so
  no output sample moves. A master whose glue did not compress answers `Unavailable` with reason `NoSignal`.
