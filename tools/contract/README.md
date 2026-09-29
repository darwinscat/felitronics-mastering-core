# Measurement session contract

The slice 0 script grammar and native/Wasm runner live here. The same `.session`
file drives `fcore_session` and the shipped `fcsession.node.js` C ABI. The runner
compares every event, answer, snapshot, summary, query, and owned f64 row byte in
order. The only masked fields are host allocation budgets (`measurementStorage`
except `sourceBytes` and `allocatorBytes`, `needlesBytes`,
`needlesLargestBlockBytes`, and `needBytes`): pointer widths make these different
between native and Wasm. The allocation suites check each tier's declared bytes
against its counted allocations. A mismatch prints the first record or event.

`fixtures/inputs.json` generates public synthetic PCM and a saved project. No
private corpus enters this directory. Run `node tools/contract/fixtures.mjs
--rebuild` after changing its generator or inputs. The manifest records the
input and file SHA-256 hashes; the runner refuses a stale or damaged fixture.

`recordings/*.json` are site examples from the Wasm codec. Each contains exact
JSON strings and owned row bytes in hex for selected milestones, summaries,
answers, and queries. `recordings/manifest.json` records the script and input
hashes, component, config and codec versions, file hashes, and the rebuild
command. Rebuild after an intentional contract change:

```sh
node tools/contract/run.mjs build/tools/fcore_session build/measure-08-wasm-artifacts/fcsession.node.js --rebuild-recordings
```

Normal verification omits the rebuild flag. `--native-only` runs on a host
without Emscripten; `--controls` also proves reordered events, corrupted fields and rows,
and stale fixtures fail. `tools/wasm/build.sh` builds the production and checked
Wasm modules and a separate poisonable contract control. The four appended v1
ABI functions load measured facts and attach the matching audio.

The WAV delivery example is `wav-input.json` and `recordings/wav-contract.json`.
It checks native and wasm WAV bytes in each target's own format (44.1 kHz PCM16 for cd
and cdDynamic, the source's 48 kHz PCM24 for allStreaming and spotify, with `deliveryRate`
and `dither.bits` 0 or the same values) and records exact
codec answers, snapshots, events and binary rows for a safe master, repeat
download, release, refusal, a depth or rate other than the target's (the open rejection
DeliveryFormat with its fact), cancellation, safe miss, unavailable source and
unsafe landing. The source and each binary ready request have SHA-256 hashes.
The safe Solve example records its preflight demand, largest block, source and delivery PCM bytes,
observed wasm heap growth, and separate playback, WAV and copy-chunk browser buffers.
Three identical masters then release and forget their rows; the recording includes each answer,
snapshot and observed heap size, with the last two high-water readings equal.
`wav-recording.mjs` decodes the lossless archive for page tests;
`wav-recording-check.mjs` replays its scenarios without an engine. Regenerate
with the command in `docs/SESSION.md`; normal verification omits `--rebuild`.
