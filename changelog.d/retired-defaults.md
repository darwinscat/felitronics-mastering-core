### session — a 2026-09 project is refused, nothing converts on import

- **A project saved on defaults `2026-09` is not opened (owner, 30.09).** It was saved before the core had its planner.
  The import converted it and placed the machine's layer again; it is now refused whole as `RetiredDefaults` (36),
  before the document is read, with fact `RejectedRetiredDefaults` (136) naming the version in ru and en. The revision
  and the open project do not move. The refused labels are an explicit list, never "older than the current", so a
  later change of calibration can neither re-place nor refuse a `2026-10` project unseen. An older label neither carried
  nor listed is `UnknownDefaults`; nothing converts any more, and `DefaultsConverted` (9) keeps its id unemitted. Held
  by `ProjectTests.cpp:defaultsVersions`.
