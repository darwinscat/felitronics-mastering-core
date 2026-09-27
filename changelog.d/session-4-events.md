### session — work-unit pump, event deltas and owned snapshots

The session runs deterministic stub measurement and master jobs through a bounded work-unit pump. Phase events use
config weights; facts appear on the step that establishes them. Cancellation preserves the session, and job/source
identity checks ignore late completions. Measurement jobs have ids alongside master jobs.

Snapshots own both project layers, source metadata, recipes, progress and reading rows. The named-field JSON codec
preserves finite doubles and signed zero, explicitly represents infinities and NaN gaps, and generates TypeScript
declarations from its field description. Every operation declares its memory demand before work. Scenario tests pin
event sequences, commands between steps, cancellation, stale completions, round trips and exact allocation demands.
