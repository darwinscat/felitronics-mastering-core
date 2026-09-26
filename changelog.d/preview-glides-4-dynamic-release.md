### mastering — a dynamic point switched off mid-duck releases; the compressor's makeup and the stages' own glides

On felitronics-core's preview-glides branch the stages glide on their own (the Saturator's drive, bias, mix and trim;
MonoBass's corners, its `enabled` and the air's; the Compressor's makeup and auto-makeup), and each snaps on the
first write after a restart, so this chain's offline renders stay bit-identical (checked by hash on six topologies).
Two things are the chain's own: every `dynamiceq::LaneDynamics` producer is opted into RELEASE ON DISENGAGE, so a
point whose `dyn.on` goes off — or whose range goes to 0 — mid-duck releases its delta through its own ballistics
instead of snapping it (a -9 dB duck on a -12 dBFS tone: -16.1 dBFS max|Δ²y| before, -55.3 after, the rest being the
band's 16-sample control grid), and once released the chain renders bit for bit what a chain with a static point
renders; and an EQ bypass edge hands every band its caller's own parameters back, since a releasing producer holds
its band's dynamic seam open. `bypassCompressor` no longer clicks either: it writes makeup 0, which now glides (-30.5
-> -73.8 dBFS).
