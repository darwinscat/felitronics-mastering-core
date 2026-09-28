### session · owned measurement results and memory demand

Analyzer results now have owned numbers, arrays, grids, completeness and missing-value reasons. Snapshot copies
survive workspace release and source replacement. Cancellation retains PCM, completed results and saved progress;
identical loads reuse them. Measurement keys include actual parameters and versions independently of the target.

Load preflight includes native analyzer preparation, result copies, serialization, previous source storage and
allocator allowance. Waveform and stereo columns expose native storage declarations; vector construction and
oversampler padding are included in the other affected declarations. The excursion-index budget remains explicitly
unknown. Live analyzer execution is not connected to the existing deterministic pump.

The v1 ABI gains `fc_session_measurement_bytes` and its size-prefixed output record. Snapshot fields, measurement
events and catalog facts are additive; codec and TypeScript declarations still share one generator. Allocation
coverage includes MSVC Debug through the existing budget job.
