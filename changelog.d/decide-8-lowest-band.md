### session — the lowest occupied band is published with its sureness

- The low end's reading of its lowest occupied band (`lowestOccupiedMidi`, `lowestOccupiedHz`, `lowestOccupiedDuty`,
  `lowestOccupiedMarginDb`) is published wherever a band was on at all, no longer withheld when it stands under the
  2 dB margin; two numbers are added beside it — `lowestOccupiedSure` (it stands the margin and is resolved) and
  `lowestOccupiedResolved`. A shell shows an unsure band as such ("35 Hz, unsure"); the observation says it too
  (`sourceLowestBandUnsure`, doubtful). The planner's rule is unchanged: an unsure lowest band gives the high-pass floor.
