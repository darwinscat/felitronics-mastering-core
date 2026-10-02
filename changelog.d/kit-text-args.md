### session — a fact without its arguments is refused, never printed as its template

- **`fc_kit_text` / `Kit::text` refuse an incomplete fact**: one short of an argument its message needs, with one too
  many, or with one of another kind (or a unit, precision or term the renderer does not know) is
  `FC_SESSION_ERR_CONTRACT` (`CodecStatus::Invalid`), nothing written, `*written` untouched — as a malformed fact is.
  Before, it came back `OK` with the raw template (`{field}: …`). `Text::size`, `write` and `text` render such a fact
  as nothing (0 bytes, an empty string), and the new `Text::complete` tells it from a rendering; a fact whose plural or
  select argument does not match no longer renders its id. No new C entry point; `FC_SESSION_ABI_VERSION` stays 5.
- **A field's refusal whose field no table names says the command's refusal, whole**: `Text::rejected` gives
  `RejectedContract` (131, no argument) where it gave 112/113/114 without the `{field}` they need. The source was a
  master refused on its own numbers — a ready chain's non-finite parameters (`NotFinite`, field 255: the WAV contract's
  refusal scenario), a delivery rate or topology the chain does not admit (`OutOfDomain`) — and an import refusal on a
  key that is no field. The answer's `code` and `field` are unchanged; only its `fact` moves (WAV contract recording).
- The wasm session check renders every fact the WAV contract's scenarios carry (answers, events, snapshots) on the kit,
  in ru and en, and fails on one that does not render whole. The text corpus builds every fact complete (a term of its
  own group) and pins its new hash; the old renderer gives the same hash on it, so no complete fact's words moved.
