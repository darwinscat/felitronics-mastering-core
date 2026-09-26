### C ABI v14 — `fc_master_set_params`: a parameter set written while the stream runs

`fc_master_configure` re-prepares, so it is exact and refused (FC_ERR_STATE) once audio has been seen. The live preview
needs the other half, and this is it: `fc_master_set_params (h, params)` maps the set with the same `toCore` and hands it
to `MasteringChain::setParams()` — no preparation, no reset, no allocation — before, during and between streams. The set
lands on the chain's next internal quantum and GLIDES from there by each stage's own rule (the gain nodes and the mix,
the Saturator, MonoBass, the compressor's makeup, a dynamic point switched off, the limiter's ceiling, the bypass
fades); several calls before that boundary are the last one alone; and the first quantum of a stream snaps, so a set
written before the first frame renders exactly what `fc_master_configure` with that set renders (pinned).
`fc_master_resolved_get` lags ONE QUANTUM behind it — it reads what the chain applied — which the header states. Refused
where `fc_master_process` is refused (a delivering handle; a handle that has solved, until a configure) and for
configure's struct and value reasons, with the stream untouched. One entry point and no struct, so the size table
has no new row (as v7 and v9). The JavaScript half moves with it: `FC_MASTER_ABI_VERSION = 14`, and `FC_STATUS` gains
the three v13 codes (16–18) it had been missing for a version — layout-check.mjs now holds `FC_STATUS` against the
header's `fc_status` (count, order, value, name), so the next code cannot go missing the same way. MasterAbiTests: the
ABI stream with a live write held bit for bit against the C++ chain driven the same way, the resolved lag, the snap
before the first frame, every refusal and that it moves nothing, no allocation, the poison list. The wasm modules and
the parity scripts (master-parity on both generated programmes, the probe NULL) pass against a build of this tree.
