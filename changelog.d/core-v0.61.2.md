### build — felitronics-core v0.61.2 is the pin

- **The pin moves to felitronics-core v0.61.2** (`FELITRONICS_MASTERING_FCORE_TAG`); the floor stays v0.57.0 (CMake,
  `tools/wasm/build.sh`): v0.60.0 adds the `dehum` module, v0.61.0 the `delay` and `reverb` modules, v0.61.1 fixes the
  wasm build's `FlushToZero`, v0.61.2 fades the delay's and the reverb's feed. Nothing here uses any of them. No sound
  moves: the contract recordings move in their version fields only, every audio row and WAV byte as it was.
