### mastering — bypass toggles fade: the clipper and the limiter warm up and cross-fade, mono-bass rides its own fades

A bypass of the clipper or the limiter was a skip with a `reset()` on BOTH edges and a hard swap, so a toggle clicked
and a return punched a hole: on a -12 dBFS 227 Hz sine at K = 128 (max|Δ²y| where the change reaches the output) the
clipper's round trip read -9.2 dBFS with a ~63-sample dropout and its return at drive 9 -7.3, the limiter's round trip
-12.5 with a 111-sample hole of exact zeros, and entering bypass while it held 6 dB -18.2. Each now has a FADER (five
modes, a position counted in samples of the quantum): entering bypass fades the stage to its aligned dry over
`kBypassFadeMs` = 10 ms and then stops calling it (a steady bypass is the skip it always was, bit for bit); leaving it
resets the stage, WARMS it up on the live input while the output stays on the aligned dry — for the stage's whole
finite memory, 2L+1 samples for the clipper and 2·O+A+1 for the limiter (its oversampler round trip around its
lookahead, the delay line and the peak window running in parallel) — and fades it back in; a reversal mid-fade turns
around from the weight it reached, and a bypass during the warm-up goes straight back to dry. Measured the same way:
-67.1 (round trip, no dropout), -63.6 (return at drive 9), -73.1 (limiter round trip, no hole), -73.1 (into bypass
while limiting), -70.7 (out of it), -63.2 / -75.9 (reversals), -65.1 (an Asym clipper's return), -68.7 (a dual-release
limiter holding 12 dB). 10 ms was measured against 5 (worst -60.9, the Asym return) and 20 (worst -64.3). After the
warm-up and the fade a restored chain is bit for bit the chain never bypassed wherever the stages' memory is finite
(a symmetric curve, a limiter not limiting); the Asym DC blocker and the limiter's release state start fresh, as any
reset starts them. The limiter's traces are its own whenever it runs, warming or fading included; they read zero only
while it is not called. Mono-bass's bypass now rides `stereo::MonoBass::setBypass` — the island's own 20 ms fades and
its own retirement — where it was a skip with a reset: -21.5 / -48.7 dBFS before, -74.3 / -75.5 after. Latency does not
move, and a render with bypass flags set before its first sample is the bits it always was (checked by hash).
