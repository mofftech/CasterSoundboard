# Integrated Audio Devices manager — implementation and verification

Implemented on `codex/integrated-audio-manager`, based on the untouched CLI
profile patch at `c7e1d14`. This is an **experimental Stage 1 implementation**;
first-frame playback leakage is deferred at the user’s request. The user has
confirmed working soundboard delivery to Overwatch; broader reliability testing
and UI refinement remain follow-up work.

## Running

From the repository root:

```sh
mkdir -p build-audio-manager
cd build-audio-manager
qmake6 ../CasterSoundboard/CasterSoundboard.pro
make -j4
./CasterSoundboard --profile "$HOME/Soundboard/overwatch.caster.caster"
```

Open **Audio Devices…** on the toolbar. Choose the physical microphone, global
system playback output, and soundboard monitoring output independently. Apply
first presents a change preview, including any card profile interruption. Confirm
Apply to change audio. Startup and Refresh never apply saved settings.

The VOIP source's actual name is shown after Apply. It normally is
`VirtualMic.monitor`; an existing unrelated `VirtualMic` gets a collision-free
alternative. Enable **Use mixed virtual microphone as system default input**,
then Apply. Set the game's input and output to **Default/System Default**.
Reconnect voice chat if its existing recording session retains its old input.
Caster does not select applications or move their recording streams. The panel
shows the current system defaults on open/Refresh. Monitoring receives only
`SoundboardSink.monitor`, never the physical microphone.

Cancel discards staged selections when the dialog is reopened. Refresh preserves
staged IDs and explicitly retains missing selections. The panel no longer polls
or refreshes automatically. Dropdowns expand with the panel and their popups
allow full labels. Settings are stored using
`QSettings("CasterSoundboard", "AudioDevices")`, schema 1, independently of
`.caster` files and the legacy launcher's configuration. Newer settings schemas
are not overwritten. Saved settings are restored as selections, not auto-applied.

Stop and clean exit remove this process's owned modules. Successfully selected
physical card profiles and system playback output remain in place. The previous
default input is restored only if the default still points at Caster's managed
virtual input. External default changes are retained. Cleanup failures are shown
in the dialog; shutdown failures are logged to stderr.

## Overwatch troubleshooting

The user confirmed successful soundboard delivery after fixing the ALSA stream
routing and checking these game settings:

- In this setup, disabling game audio while unfocused also prevented voice input.
  Test with Overwatch focused, or enable its background audio setting.
- Check whether push-to-talk is enabled, and hold the assigned key when testing.
- Set the game's input/output to Default/System Default, and apply the mixed
  virtual microphone as the system default input in Caster.
- Restarting desktop audio can disconnect the game's voice input even if playback
  reconnects. Rejoin voice chat; if it does not recover, restart the game after the
  audio setup is ready. Reserve the desktop-audio restart for last-resort recovery.
- Hearing the clip locally does not prove it reaches the mix. Qt/GStreamer may
  use PipeWire's ALSA path, whose stream PID is on the client record. Caster now
  handles that path as well as ordinary Pulse playback streams.

## Device recovery

The reference `hdmi-output-fix.sh` is incorporated as a **generic card-backed
input/output recovery flow**, not a GA104-specific device name or PCI address.
The script itself remains unchanged.

- Discovery flags an advertised active profile whose expected endpoint is
  missing, and identifies cards disabled by the `off` profile. It keeps endpoints
  with unavailable advertised profiles in the recovery list. `SUSPENDED` alone
  is normal idle state and is never treated as a fault. Reported unavailability
  can also mean an unplugged device; detection is a symptom, not a diagnosis.
- In **Audio Devices → Device to recover**, select an input or output and click
  **Recover selected device…**. Stop Caster routing first. Review the preview,
  then confirm the targeted card interruption. Disabled cards are enabled;
  otherwise recovery switches off where supported, tries up to three alternate
  profiles for the requested direction, then selects the target profile. The
  final profile preserves the opposite active direction where possible.
- Recovery runs on the worker thread, waits for the server to publish the new
  endpoint, resolves its actual name (including renamed sinks), and verifies the
  selected port. Failure attempts to restore the original profile only if no
  external profile change was observed. Default-device choices and other
  applications' streams are not forcibly rewritten; desktop policy may move
  them during a reset. The endpoint reappearing does not prove an audible signal.
- **Restart desktop audio…** is the separately confirmed fallback requested by
  the user. It verifies the three installed user units, then runs
  `systemctl --user restart wireplumber.service pipewire.service pipewire-pulse.service`.
  The confirmation explicitly warns that **all desktop playback/recording is
  interrupted and other applications' virtual devices may disappear**. Caster
  routing stops; saved preferences are retained and must be applied again.
  Restart is never triggered automatically by discovery, failed recovery,
  Refresh, or startup. It is offered for detected PipeWire sessions, including
  when pipewire-pulse is unreachable but the PipeWire core service is active.
  Native PulseAudio does not get a PipeWire restart action.
- Restart failures report partial/uncertain service state. A successful restart
  retires old owned-module IDs to prevent cleanup against recycled server IDs.
  Server reconnection is verified, but hardware recovery still requires a
  playback/recording check. Card-only recovery never restarts services.

Recovery tests use synthetic state plus a reduced GA104 metadata capture. They
cover inputs and outputs, off profiles, unavailable profiles, normal suspension,
missing endpoints, renamed outputs, profile errors, concurrent changes, stale
previews, explicit restart planning, failed service commands, native PulseAudio
exclusion, unreachable pipewire-pulse, and module-ID reuse after restart. The
service command runner is injected in tests: **no production card reset or user
service restart was performed during development**.

## Architecture

- `audio/AudioBackend.h`: common endpoint, capability, configuration, plan, and
  result types; provider interface and factory. A second provider implements this
  interface and is selected in the factory; UI layout and profile serialization
  do not depend on pactl. Provider-specific fields live in opaque metadata.
- `audio/PulseCompatBackend.*`: read-only probe/discovery, profile intersection
  planning, bounded QProcess calls with separate arguments, routing transaction,
  conditional rollback, status, and owned-module cleanup. Endpoint IDs identify
  card, role and port; live node names are resolved after profile changes.
- `audio/AudioDeviceController.*`: serialized worker-thread operations, preview /
  apply coordination, manual discovery, persistence, and shutdown. Probes do not block the
  GUI. Defaults/profile/port snapshots are internal to the transaction.
- `audio/CasterPlaybackRouter.*`: debounced `pactl subscribe` stream events and a
  one-second fallback for stream routing (without refreshing the panel). Playback
  movement selects only Caster’s process PID, including cue-preview players.
  Native PipeWire/ALSA streams without a stream PID are matched through their
  Pulse client index and that client’s PID; application names are never used as
  ownership proof. This covers Qt/GStreamer ALSA fallback after a server reset.
  Games and other receiving applications use the system defaults.
  Player construction, decoding, volume, and OSC remain
  unchanged.
- `audio/AudioDevicesDialog.*`: nonmodal native Qt widgets, staged controls,
  missing/inactive labels, global-change labels, preview, status and selectable
  diagnostics.
- `MainWindow.cpp`: toolbar integration only. `main.cpp`, `CasterBoard`,
  `CasterPlayer`, cue logic, and profile serialization are unchanged.

Null sinks and loopbacks carry a per-process random `caster.owner` token. Exact
module IDs, module names and complete arguments are retained in memory and
rechecked before unloading. Colliding names are never adopted. Repeated Apply
reuses the existing graph; monitoring changes alter only the affected loopback.
Owned unused virtual microphone sinks remain until Stop for subsequent reuse.
Modules created during a failed transaction are unwound; previously active
connections are retained until the commit point. Cleanup failures after commit
are explicitly reported as partial state, requiring Stop/retry.

`pactl` JSON is used for discovery. pactl 16 omits module indices in JSON, so the
adapter cross-checks its tab-delimited short module listing against JSON for
`module-null-sink` and `module-loopback` only. Native PipeWire modules with
multiline arguments are excluded. Ambiguous or mismatched module data fails
closed. Module source/sink names are restricted to safe identifier characters.

## Verified here (11 October 2026)

Environment: Ubuntu 24.04.5 LTS, X11, Qt 5.15.13 / qmake, GStreamer Qt Multimedia
plugin, pactl/libpulse 16.1, PipeWire 1.0.5 reporting PulseAudio protocol server
15.0.0.

- Full application builds with C++11 and Qt5; no new dependency beyond existing
  Qt modules and runtime `pactl`.
- The real `overwatch.caster.caster` profile loads in an offscreen window; the
  dialog renders and the event loop exits cleanly using the application's window
  lifetime. `--help` still advertises `--profile`, `-p`, and positional loading.
- Offscreen Audio Devices window renders and degrades gracefully when sandbox
  restrictions make the audio server unavailable.
- Read-only discovery metadata captured from the actual Focusrite Scarlett 2i2
  and JBL cards; reduced fixtures preserve their profiles and ports.
- Automated backend suite: inactive microphone, duplex transition, renamed sink,
  incompatible profiles without mutations, repeated Apply, monitor toggle,
  rollback at each of five module loads, failed default change, failed reapply
  preserving an active graph, persistent degraded status after cleanup failure, collisions, externally changed defaults, reused
  module ID refusal, Stop without settings, malformed/unavailable server,
  pactl 16 module-ID fallback, and PID-restricted stream movement.
- Private PipeWire integration: real null-sink and loopback module syntax,
  ownership properties, exact module arguments, full Apply, identical reapply,
  monitoring off and Stop all pass. Physical ports are represented by a test-only
  facade over isolated null endpoints. The private server uses its own WirePlumber policy manager and D-Bus
  session with ALSA, camera and Bluetooth discovery disabled. A real recording
  client using the default input attaches to the mixed microphone. Stop restores
  the previous system default input. A real ALSA playback client (`aplay -D pipewire`)
  verifies client-PID routing into SoundboardSink when the stream itself lacks a PID.
  This verifies server commands/ownership, not audible hardware playback.

Reproduce the tests:

```sh
mkdir -p build-audio-tests
cd build-audio-tests
qmake6 ../tests/audio/audio-tests.pro
make -j4
./audio-tests
cd ..
python3 tests/audio/isolated-server.py "$PWD/build-audio-tests/audio-tests"
```

The ordinary suite intentionally skips the private-server integration test. The
Python runner creates a unique temporary runtime, sets an explicit private Pulse
socket, starts only its own PipeWire processes, and terminates them in `finally`.
It requires `pipewire`, `pipewire-pulse`, `pactl`, `parecord`, `aplay` and the PipeWire ALSA plugin, WirePlumber 0.4
(with its installed Lua configuration), and `dbus-run-session`.

## Remaining acceptance work and deviations

1. **Playback first-frame leakage is deferred.** The local
   Qt5 GStreamer service returned null for `QAudioOutputSelectorControl` in a
   compiled experiment. The implemented event/PID movement cannot guarantee that
   newly created streams avoid initially playing on the physical default output.
   No claim of leak-free live switching is made.
   Proposed contained fix: a Qt/GStreamer output adapter that assigns an explicit
   Pulse sink before a stream enters PLAYING (for example, an application-local
   registered sink factory), preserving Qt decoding, player controls and profiles.
   Validate first playback, overlapping players, replay, cue previews and sink
   removal with captured audio before treating this as production-ready.
   The inspected [Qt 5.15 implementation](https://github.com/qt/qtmultimedia/blob/5.15/src/gsttools/qgstreamerplayersession.cpp)
   constructs its output sink when the player session is created. Changing
   `PULSE_SINK` after GUI startup is deliberately not used as a purported fix.
2. **Crash leftovers are detected but not automatically adopted/deleted.** Without
   a durable session-liveness/ownership journal, another tagged graph could belong
   to another running Caster instance. Stop safely handles this session without a
   config file, but cannot reclaim a previous process's modules. A future explicit
   reconciliation UI must prove stale ownership before offering cleanup.
3. Discovery supports card-backed ports with advertised profile memberships.
   Portless cards expose logical input/output endpoints when role counts are
   advertised; ambiguous multi-endpoint/pro-audio mappings are not guessed. Additional captured Bluetooth/pro-audio/native PulseAudio
   fixtures and adapters are needed for wider coverage.
4. Device discovery uses manual Refresh. The capabilities
   model reports native hotplug support as false. Loopbacks request no automatic
   source/sink movement. Unavailable hardware is reported; no replacement device
   is silently chosen. Physical unplug/reconnect behavior remains to be tested.
5. Live Focusrite/Overwatch soundboard delivery is user-confirmed. No native
   PulseAudio daemon test, unsupported-platform build or acoustic echo test has
   been performed. Continue verifying volume,
   looping, seeking, overlapping sounds, OSC and drag/drop during routing. Module
   availability is conclusively established on Apply, with errors/rollback on
   unsupported modules; startup does not create test modules on desktop audio.
6. Operations are bounded individually, not by one total transaction deadline.
   Clean exit can take longer if the server repeatedly times out. The backend
   offers internal rollback rather than an externally exposed snapshot API.

During live troubleshooting, six playback streams from the verified Caster
process were moved into its existing SoundboardSink to confirm the ALSA routing
fix. The legacy script/configuration was left unchanged. No remote deck, macro,
PTT, per-application mixer or decoding-engine rewrite was added.
