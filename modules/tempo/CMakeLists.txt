# felitronics::tempo — the tempo of a decoded programme: the whole-track BPM, its confidence and octave
# alternatives, and the tempo over time. A port of the site's dsp/tempo.js (TempoDetector.h says what is exact
# and what is not). It stands on core's offline double FFT (core::offline::fftInplace) and its deterministic
# math floor (core::det), and on nothing else, so the target links felitronics::core alone.
add_library(felitronics_tempo INTERFACE)
add_library(felitronics::tempo ALIAS felitronics_tempo)
target_include_directories(felitronics_tempo INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(felitronics_tempo INTERFACE felitronics::core)
target_compile_features(felitronics_tempo INTERFACE cxx_std_20)

if(FELITRONICS_BUILD_TESTS)
    # The detector: the spec's behaviours on synthetic programmes (click trains at known tempi, a tempo that
    # changes, silence, noise, a lone click), a direct whole-buffer translation of the spec as a bit-for-bit
    # reference for the streaming class, the FFT against a direct DFT, and the call contract (law 11: split
    # invariance, the refusals, the exact width; law 11d: the demand is the allocation).
    add_executable(felitronics_tempo_tests tests/TempoDetectorTests.cpp)
    target_link_libraries(felitronics_tempo_tests PRIVATE felitronics::tempo felitronics::test_support)
    target_compile_features(felitronics_tempo_tests PRIVATE cxx_std_20)
    add_test(NAME felitronics_tempo_tests COMMAND felitronics_tempo_tests)

    # The JavaScript arithmetic the port reproduces (JsNumerics.h): Math.hypot, Math.round, Math.min/max against
    # values V8 computed, and the deterministic exp against its own identities.
    add_executable(felitronics_tempo_numerics_tests tests/JsNumericsTests.cpp)
    target_link_libraries(felitronics_tempo_numerics_tests PRIVATE felitronics::tempo felitronics::test_support)
    target_compile_features(felitronics_tempo_numerics_tests PRIVATE cxx_std_20)
    add_test(NAME felitronics_tempo_numerics_tests COMMAND felitronics_tempo_numerics_tests)
endif()
