### build — felitronics-core v0.53.0 is the minimum

The chain now calls `MonoBass::setBypass()` and `LaneDynamics::setReleaseOnDisengage()`, which first shipped in
felitronics-core v0.53.0, so the pinned `FELITRONICS_MASTERING_FCORE_TAG` moves from v0.52.0 to v0.53.0 and the
configure-time messages and `tools/wasm/build.sh` name v0.53.0 as the minimum.
