### toml — deterministic project-file TOML subset

Add the dependency-free, header-only `felitronics::toml` parser and canonical writer. The owned
subset supports quoted/dotted keys, comments, strict scalar arrays, nested tables and arrays of
tables, with insertion-ordered storage, localized error codes and bounded document resources.
Decimals retain their exact mantissa, scale and negative zero; binary64 conversion uses one
correctly rounded division of exact integers, without libc number conversion or locale state.

Standalone suites cover the grammar, every error code and limit, hostile input, exact rational
rounding and generated round trips. Add a local Python `tomllib` cross-check, an optional
libFuzzer/ASan/UBSan target, header-hygiene coverage and the full contract in `docs/TOML-SUBSET.md`.
