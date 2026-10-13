# Qt 6 integration

The annotated tag `pre-qt6-integration-2026-10-13` preserves the custom Qt 5
version at `657b752`. The branch `codex/qt6-upstream-integration` merges upstream
`a0f1798` into that version, retaining the audio manager, Actions profiles, web
remote and toolbar customizations. The original custom branch remains unchanged.
The untracked, machine-specific `hdmi-output-fix.sh` is not part of the tag.

## Build

On Ubuntu 24.04:

```sh
sudo apt-get install qt6-base-dev qt6-multimedia-dev qmake6 build-essential
mkdir -p build-qt6/app
cd build-qt6/app
qmake6 ../../CasterSoundboard/CasterSoundboard.pro
make -j4
./CasterSoundboard
```

Qt 6.4 keeps Qt's default multimedia backend (GStreamer on this Ubuntu build):
forcing FFmpeg 6.4.2 failed the cue-loop regression tests. Qt 6.5+ retains upstream's
FFmpeg preference; `QT_MEDIA_BACKEND` overrides are still respected. FFmpeg became
the desktop default in [Qt 6.5](https://www.qt.io/blog/qt-6.5-lts-released).
Qt 6.4 uses palette-based theme detection; Qt 6.5 and later also use the system
color-scheme API. Actions and the web remote subscribe to Qt 6 playback signals.
Upstream preferences, default profiles, headless mode and output selection are
included. The output selector is disabled while the custom audio manager routes
playback; stopping that routing restores the selected Qt output.
Player destruction cancels pending Actions before releasing playback state.

## Validation

Built the application and all six Qt test executables with Qt 6.4.2 on Ubuntu
24.04. Tests cover audio backend discovery/routing and recovery, cue boundaries
and looping, OSC metadata and malformed packets, Actions lifecycle and profile
serialization, and HTTP/SSE remote control. Actions serialization is exercised
with both Qt 5.15 and Qt 6 stream formats, including legacy profiles without
Actions. Python helper (5 tests) and uinput proof-of-concept (9 tests) suites pass.
Qt suites pass with the default backend: audio 33 passes plus the separately
executed private-server case; cues 7; metadata 5; application 7; Actions 14;
and the web integration executable. Counts include Qt initialization/cleanup.
CLI validation and headless OSC startup also pass. Forced FFmpeg on Qt 6.4.2 is
not a supported configuration for this integration.

Build each test in its own directory using `qmake6` and `make`:

| Directory under `build-qt6` | Project under `tests` | Executable |
| --- | --- | --- |
| audio | audio/audio-tests.pro | audio-tests |
| cues | cues/cue-tests.pro | cue-tests |
| metadata | metadata/metadata-tests.pro | metadata-tests |
| application | metadata/application-tests.pro | metadata-application-tests |
| actions | actions/actions-tests.pro | actions-tests |
| web | web/web-tests.pro | web-tests |

Run tests from their build directory so source-relative fixtures resolve.
For example, from `build-qt6/cues`:

```sh
python3 ../../tests/audio/isolated-server.py ../audio/audio-tests ./cue-tests
```

The private PipeWire harness disables hardware monitors and isolates its sockets
and configuration from desktop audio. It requires PipeWire, pipewire-pulse,
WirePlumber 0.4, pactl and permission to create local sockets. Its optional GUI
test command has a 120-second timeout. Qt 6.4's GStreamer backend can emit
`g_object_unref` diagnostics during looping; the playback assertions pass.

Actual speakers/headsets, game capture/PTT, HDMI recovery, Flatpak and Nix builds
have not been validated in this integration. The upstream AppImage script removes
GStreamer, so use Qt 6.5+ for that packaging path; the Qt 6.4 loop failure applies
to its FFmpeg-only runtime. Check those on the intended setup
before replacing the installed Qt 5 executable. The build above leaves the
existing executable in the source directory intact.

To inspect or rebuild the preserved version without disturbing this branch,
create another worktree from `pre-qt6-integration-2026-10-13` and build it with Qt 5.
