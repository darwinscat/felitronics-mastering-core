### session · mastering — the delivery format is the target's

A master's WAV takes the frozen target's format: its rate (the source's when the target's `sampleRate` is 0) and its
bit depth, PCM16 or PCM24. A ready `deliveryRateHz` or `deliveryBits` (the C facade's `fc_master_config.deliveryRate`
and `fc_master_params.dither.bits`) of 0 takes it, the same value restates it, and any other value is refused before
any allocation with the appended `Rejection::DeliveryFormat` and a fact naming the target's depth and rate. cd and
cdDynamic now deliver 44.1 kHz and take the source-rate crest pass. The float32 and 20-bit delivery paths are gone. The WAV of a 16-bit target is now 16-bit even though the completed job's working recipe has
been handed to the kept master. A late crest join waits for a resumed source measurement instead of settling it as
cancelled, and a crest joined inside the job is not published again. Every facade output that could land in the
retained master PCM is fenced. The landing search's first ceiling uses the caller's `LoudnessRequest::ceilingMarginDb`
(required for a product landing; Session passes engine.toml's), and a best pass at the end of the budget is delivered
without a second render. felitronics-core v0.56.0 is consumed as released, without a build-time patch, and is held to its version floor and the
presence of the K13 tap and the WAV writer, no longer to whole-file hashes of either header.
