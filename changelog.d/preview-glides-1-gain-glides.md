### mastering — the gain nodes and the compressor mix glide instead of stepping

`inputGainDb`, `preLimiterGainDb` and `compressorMix` used to land on a quantum boundary as a step, which clicks on
a live preview: on a -12 dBFS 227 Hz sine at K = 128, max|Δ²y| where the change reaches the output was -20.1 dBFS
for inputGainDb 0 -> +3, -22.3 for preLimiterGainDb 0 -> +3 and -34.4 for compressorMix 1 -> 0.5, against -73.1 for
the steady tone. Each now moves by a fixed-length LINEAR ramp, `MasteringChain::kParamRampMs` = 30 ms (read back as
`paramRampSamples()`), per sample on the quantum's own clock, from the quantum the write lands on — -69.1, -69.4 and
-74.7. 30 ms was chosen by measurement: a +3 dB step is clean at every length from 5 ms, and 0 -> +12 dB reads -51.2
/ -58.0 / -57.2 / -60.8 / -60.2 dBFS at 5 / 10 / 20 / 30 / 50 ms against the louder tone's own -61.1, so 30 ms is the
shortest length at which a big jump sits on the tone's floor. The ramp arrives on the exact resolved value, so the
rest path is the constant multiply (and the mix branches) it always was; each glide sample of the mix is the stated
double blend at a float `m`, so the law-10 argument for the blend holds sample by sample. The FIRST write of a
stream — after `prepare()` or `reset()` — snaps: every offline render (`setParams -> reset -> process`, the renderer
and the solver) is bit-identical to the tree before this change, measured on six topologies including K = 8 and K =
100. The split-invariance suite gains two AUTOMATED chain rows — every glidable parameter and every bypass moved at
fixed stream positions on no grid — which must be the same bits under every cut, taps and statistics included.
