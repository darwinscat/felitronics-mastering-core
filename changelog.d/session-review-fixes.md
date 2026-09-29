### session · tools — measurement review fixes, and a manifest that only grows

A reload of the same PCM under another bit depth is another forensics result: its key mixes the measurement key with
the depth, and the reload publishes one `Measurement` event for it; the other results keep their key. A needles job
that outlives a measurement cancelled at the first phase's end (the `Stopped` column) is cancellable there, and a
cancel's fact names the job it stopped. A live preparation refused for memory is that instrument's outcome —
`Unavailable` for `Memory`, one error, the job goes on — so a capacity shrunk for good no longer repeats the refusal
on every unit and the job ends. Needles are `Pending` only while a job runs: with unusable readings (silence) the
result is `Unavailable` with the loudness result's reason. The command table names `master`'s `NoAudio` for a sidecar
source among its name checks.

`FC_SESSION_ABI_VERSION` is a floor, like fc_master's: the manifest's version line is checked as "at least", and the
number moves with each addition after the first release. Every `fc_session_*` declaration is frozen whatever it
returns; the wire's `SessionStatus` union is generated from `fc_session_status` instead of a copy in the codec schema;
the generated `SessionCapabilities` carries `largestFreeBlockBytes`, and a wire record that mirrors a C struct must
carry all of its fields. On pull requests CI refuses a manifest that removes or edits a base line.
