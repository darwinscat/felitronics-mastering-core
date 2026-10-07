### session · analysis_offline — 88.2, 96 and 192 kHz resolve the low end as 48 kHz does

- At fftOrder 17 a 96 kHz run resolved the semitone bands only from 50.7 Hz, so even an E1 bass (41.2 Hz) was "unsure"
  at 96 kHz. The run's FFT order now rises with the rate: `[lowEnd.run] fftOrderUpToHz = 48000` (new) — fftOrder holds
  up to 48 kHz, and above it the order is the smallest whose bin is no wider than at 48 kHz (18 at 88.2 and 96 kHz, 19 up
  to 192 kHz). `LowEnd::fftOrderFor` computes it.
- The bands resolve from the same 25.36 Hz, and the window and the hop last as long as at 48 kHz. At 96 kHz the run takes
  0.67 s per minute of programme instead of 0.83, and 10.7 MiB instead of 5.4; the declared work peak of a 10-minute
  stereo source rises by 15.8 MiB, its declared peak does not move.
- Every rate up to 48 kHz is bit for bit as before, 44.1 and 48 kHz included.
