### tools — `fcore_master` names the three v13 statuses

The CLI printed `?` for `FC_ERR_BAND_NOT_DYNAMIC`, `FC_ERR_BAND_INERT` and `FC_ERR_LANE_OFF` (16–18, ABI v13): its
status-name switch stopped at 15, and only a `-Wswitch` warning said so. It now prints `BAND_NOT_DYNAMIC`,
`BAND_INERT` and `LANE_OFF`, and the build is free of that warning. The names are one list, which the switch reads
and `fcore_master layout` prints, and `layout-check.mjs` holds it against the header's `fc_status` in both
directions, so the next code missed fails ctest instead of warning on the rows whose compiler warns at all. The
domains suite's own status table had the same gap; it is a switch with no `default` now.
