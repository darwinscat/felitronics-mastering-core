### session · C ABI — frozen v1 shell contract

`fc_session` v1 exposes capability/config creation and its demand, named JSON commands, planar PCM load, work-unit
steps, event/snapshot size and copy calls, and project import/export. The session enforces heap ceilings, maximum
rates and offered devices for C++ callers too. Generated types carry the config version and tagged event union;
numeric rows travel in caller-owned f64 buffers. Convert, Lra and Final append stable phase values with catalog text.

Compiled probes freeze signatures, constants, enums and layouts on native and wasm32. The manifest gate permits
additions and has change/deletion controls. Native/wasm tests cover refusal order, allocation bounds, typed transfer,
project round trips and permanent poison; the wasm artifact smoke exercises the public surface and real trap recovery
status. Windows Debug includes both session ABI suites.

The 28 September pre-freeze target decision makes `setTarget` always reset device edits. Its C++ request and generated
JSON/TypeScript command carry only the command id and target. Answers and events are unchanged; the shell warns from
the snapshot's existing hand-edit count. The v1 manifest baseline is regenerated for this not-yet-frozen surface.
