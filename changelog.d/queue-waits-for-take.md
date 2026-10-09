### session — `step` says `Done` while the masters' queue waits only for a take

- A master queued behind a delivered one is not work while it waits only for that master's PCM to be taken or released:
  no step advances it, so `step` returns `Done` instead of `More` with 0 units, and a shell that pumps until `Done` no
  longer spins. The take or the release readies the next master, and the shell steps again. No sample of any master
  moves.
