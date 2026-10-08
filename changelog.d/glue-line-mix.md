### session — the glue's line counts its mix

- The master report's glue line ("Glue took X dB off the loud places, Y dB at most"; `MasterCost::glueP95Db` and
  `glueMaxDb`) now says what the song got: the drop of the glue stage's output — the dry signal at 1 − mix under the
  compressed one at mix — against its input, on the same 4 ms windows (their P95) and the same largest sample. It was the
  compressed path's own gain reduction, so a glue at mix 40 % printed what the same glue printed at 100 %. At mix 1 the
  numbers are unchanged, to the bit; at 0.4 they come to about 0.4 of mix 1's (a little under, the loss being in dB); at 0
  the glue took 0 dB. The saturation's line already measured its stage after its mix, so the two now read alike. The
  glue's trace (`GlueGr`) counts the mix too: each bucket goes through the same law at the mix that sounded, so its
  largest is the report's `glueMaxDb`, and at mix 1 the trace is the compressor's detector, to the bit. No sample of any
  master moves.
