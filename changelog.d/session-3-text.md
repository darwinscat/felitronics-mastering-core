### session · tools — the text: facts, a catalog of whole messages and one formatting table, compiled into `felitronics::session`

**The session states facts, not strings** (`<felitronics/session/Text.h>`). A fact is a `FactId` and typed arguments — a
number with its unit, precision, sign and bound; a count; a term the catalog names; a note as a MIDI number; a text of
the user's, never translated — and carries no ready string. `Text::text(fact, lang)` renders it: a pure function over
data compiled into the library, with `size()` and `write()` beside it that render into a caller's buffer without the
heap, and `textBytes()`, the demand of `text()`. The first facts exercise every kind of argument: a reading alone, the
landing's pass and its convergence (plural on the passes), the blind test's repeat consistency (plural on the second
number), the loudest bass note, the wide-bass warning of phase 1 in the owner's words, and a file above the platform's
highest rate (select on the platform). Russian first, then English.

**Two TOML documents, compiled in** (felitronics_toml_embed, as the config): `modules/session/text/catalog.toml` — whole
messages with named placeholders, `plural` variants by CLDR category and `select` variants by term, and the languages
it declares, `ru` and `en` — and `modules/session/text/format.toml`, the one table of how each of the twelve site
languages writes a number: decimal sign, grouping separator and CLDR's minimum grouping (es, it, pl group from five
digits), the Unicode minus, the bounds `≥` and `≤`, `—` for a value that is not a number, each unit's pattern (Turkish
`%45`, French narrow no-break spaces, Russian and Ukrainian unit signs in Cyrillic as the site writes them), and the
names of the notes in the site's three systems (letters; German, where B natural is H; solfège).

**Every build checks the catalog.** `felitronics_session_text_check`, a host tool compiled from the library's own
`src/TextSchema.cpp`, runs over both documents before the library is built (through node on the wasm tier, and in
`tools/wasm/build.sh`): every declared language has every message and term; placeholders name the fact's arguments
(`src/TextFacts.h`), every argument is placed and every language places the same set; a plural message has exactly its
language's categories, a select message exactly its group's terms, and every variant places every argument (Russian
"one" is also 21); the table covers all twelve languages and every unit; and no key is one nothing reads. A problem is a
red build at `<file>:<line>:<column>`. There is no fallback to English anywhere: a message the catalog does not have in
a language renders as its id. Six controls plant mistakes in a copy and require the gate red at the spot; the suite
plants thirty-nine more in-process.

**Numbers by rules of its own**, in integer arithmetic — no libm, no printf, no locale: the exact value of the double
rounded to the decimal grid, halves away from zero (0.125 → 0.13, 2.5 → 3, and 1.005 → 1.00 because its double lies
below the half), checked against an independent oracle over 22 000 values; the sign follows the value and is read from
its bits, so no rendering depends on the thread's floating-point environment (held under flush-to-zero,
denormals-are-zero and every rounding mode); CLDR 48's plural categories for all twelve languages, selected on the
number as printed ("1.0" is not "one" in English) and pinned against ICU 78's answers; `Text::parse` reads a typed
number with `std::from_chars` and one correctly rounded division, never `strtod`, refuses a grouping separator rather
than guess (a German "12.345" is not twelve), and asks for the default floating-point environment first.
`felitronics_session_text_tests` pins the twelve rows, every message in both languages, the memory demand through the
allocation counter, and one FNV-1a hash of a corpus of renderings that every native row and the wasm tier must reproduce
byte for byte.

The session-laws lint names the two documents as data, admits `Text.h` and, in `src/Text.cpp` alone, the two headers the
build generates from them; the new units are det-math entry points; `tests/HeaderHygiene.cpp` compiles `Text.h`.
