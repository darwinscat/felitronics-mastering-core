### session — `[hpf] note.aboveHz` is removed

- It was 20 Hz, below where the lowest note is searched, so it did nothing (owner, 07.10). The note's lower bound is
  `[lowEnd] lowestNoteFromHz` alone. A config that still carries the key is refused as an unknown key. No master changes.
