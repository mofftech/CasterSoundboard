# Issue #8: idle CPU loop and memory growth

Investigated 2026-10-12 against local HEAD `b9e142c` plus the existing
uncommitted OSC metadata and web remote work.

Upstream report: https://github.com/covarianttensor/CasterSoundboard/issues/8
The reporter later isolated the problem to one saved tab, even without playback,
and worked around it by recreating that tab. The original profile was not
attached, so its precise trigger cannot be confirmed.

## Findings

The vulnerable cue handling and OSC send allocation were present in the report's
commit `1a84315` and remained in the current working tree before this fix.
Neither the recent audio routing work nor the metadata/web changes fixed them.

A real Qt 5.15.13 backend regression test loaded a silent two-second WAV, set a
saved-style cue with start = stop = 500 ms, and sought there while stopped.
Before the fix it generated **39,334 position notifications in a 200 ms event
processing window**. The queued handler called stop and sought back to the same
invalid cue repeatedly. This reproduces an idle event storm, not the original
reporter's exact 32 GB memory exhaustion.

Separately, `OscMessageComposer::getBytes()` allocates a new QByteArray on each
call. `MainWindow::sendOSCMessageToClient()` did not free it. This leaks on every
outgoing OSC packet when OSC is enabled, and a cue event storm amplifies it.

## Changes

- `CasterPlayerWidget::validateCueRange`: reset invalid cue ranges to the full
  track, clamp stop points beyond the duration, and preserve cues while duration
  is unknown.
- `CasterPlayerWidget::playerPositionChanged`: discard obsolete queued positions;
  do not enforce playback boundaries while stopped/paused, except natural
  EndOfMedia handling needed for looping.
- `CasterCuePicker::playerPositionChanged`: ignore inactive, obsolete, or empty
  preview ranges rather than repeatedly seeking/stopping.
- `MainWindow::sendOSCMessageToClient`: own both the message and serialized packet
  with `std::unique_ptr`, including exception paths.

## Verification

- New cue suite: 6 passed, 0 failed (four data cases plus setup/cleanup).
  Covers equal, reversed, negative, and past-end cue starts, idle quiescence,
  preservation of valid cues, stopping, partial loops, and full-track loops.
- Existing web remote playback integration test: passed.
- Full application build in `build-web-remote`: passed.
- `git diff --check`: passed.

Reproduce the cue suite from the repository root:

```sh
mkdir -p /tmp/caster-cue-build
cd /tmp/caster-cue-build
qmake /home/michael/github/CasterSoundboard/tests/cues/cue-tests.pro
make -j4
QT_QPA_PLATFORM=offscreen ./cue-tests
```

The suite needs a working Qt multimedia backend and access to the audio server;
a sandbox blocking PulseAudio can prevent media loading. The WAV is silent.
These checks establish the reproduced fixes, not an exhaustive leak audit of
all Qt/GStreamer backends or all application object lifetimes.
