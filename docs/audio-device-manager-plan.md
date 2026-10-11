# Stage 1 implementation plan

Initial planning record; see `audio-device-manager.md` for the implemented
behavior, live validation, and remaining work.

Baseline: clean feature/cli-profile-loading at c7e1d14. Work on
codex/integrated-audio-manager; preserve profile serialization and CLI loading.

Add common endpoint/configuration/plan models, a provider factory, and a
PulseAudio-compatible provider. Run pactl with argument vectors, bounded waits
in a worker thread, strict JSON parsing and diagnostics. Discover card ports and
compatible profiles, including inactive inputs; resolve live names after profile
activation. Keep all Linux fields inside the provider.

Add an application-scoped controller and nonmodal Audio Devices dialog. Stage
choices, preview profile/default changes, persist only successful choices in
versioned QSettings. Manual refresh retains staged selections. Never apply at
startup or touch the legacy script configuration.

Routing uses two owned null sinks and loopbacks. Track exact module identity,
create before removing old connections, unwind failures and restore defaults
conditionally. No microphone-to-physical-output loopback. Stop/shutdown affect
only resources created by this process; detect crash leftovers conservatively.

Playback is a separate service. First experiment: request Qt5's output selector.
On installed Qt 5.15.13 GStreamer backend it returns null. Investigate a contained
Qt/GStreamer sink path; do not claim asynchronous stream movement prevents
first-frame leakage. If unavailable, surface the limitation explicitly.

Verify discovery against captured local PipeWire metadata and fixtures covering
inactive input, duplex conflicts and sink renaming. Use fake pactl for mutation,
rollback and ownership tests; do not mutate production audio during development.
Build out of tree, smoke-test CLI/offscreen startup, document hardware checks
that require pressing Apply and listening in Overwatch.
