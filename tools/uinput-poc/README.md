# Linux uinput proof of concept

Standalone experiment for the Caster Action Engine. No changes to Caster's
application, audio routing, web server, or profile format. Python's standard
library calls the installed libevdev runtime through ctypes; development headers
and additional Python packages are unnecessary. A production Qt implementation
can use the same libevdev API directly.

## Run

Requires Linux, Python 3.10+, libevdev.so.2, and permission to open /dev/uinput.
Run as your ordinary desktop user. Do not make /dev/uinput world-writable.

From the repository root:

```sh
# Creates, inspects, and removes a keyboard. Sends NO keys.
python3 -B tools/uinput-poc/poc.py --probe

# Phone-accessible test, separate from Caster's port 5052.
python3 -B tools/uinput-poc/poc.py --key KEY_V --bind 0.0.0.0 --port 5053
```

Open the printed URL on a phone, replacing `<computer-LAN-IP>` with the
computer's LAN address. Include the `#...` token. The server uses HTTP and is
intended for a trusted LAN only; do not expose it to the Internet. Requests
require a random per-run bearer token. Opening the page sends no keyboard input.

1. Bind V to push-to-talk in the game and leave the game focused.
2. On the phone, press **Hold push-to-talk**. Check the game's voice indicator.
3. Speak, or use the existing Caster web front end in another phone tab to play a
   sound during a longer hold. Confirm actual reception with another player.
4. Verify release at the end of the timer and with **Release now**.
5. Close the phone page during a hold: the timer must still release the key.
6. Stop the POC with Ctrl+C during a hold: it must release and remove the device.
7. Check physical V and virtual V interaction, including overlapping holds.

The tap button sends one 80 ms key press. It is NOT a mute/unmute sequence and
can change a toggle's state. This POC does not play audio or measure audio buffers.

## Lifecycle

The web process owns a pipe to a separate keyboard helper. Only the helper opens
uinput and owns held-key state. It keeps a virtual keyboard alive, emits EV_KEY
with SYN_REPORT, and releases on expiry, explicit cancellation, pipe EOF, SIGINT,
or SIGTERM. A hold is bounded to 30 seconds and duplicate hold requests cannot
extend it. Partial/malformed input cannot block the timer. Browser disconnection
does not cancel the timer. Killing the web process closes its pipe, triggering
helper cleanup. Killing/freezing the helper itself is outside that cleanup
guarantee; device removal and the receiving application's response need validation.

Device recognition uses a conservative 1.5-second settling delay for this POC.
This is not a compositor readiness acknowledgement. The virtual device advertises
normal keyboard capabilities for classification, but only sends the single key
configured at startup. No physical keyboard devices are read.

## Automated tests

```sh
python3 -B -m unittest discover -s tools/uinput-poc -v
```

Simulation tests exercise the real helper loop and HTTP/controller subprocesses,
replacing only device writes. They cover expiry, explicit release, disconnect,
SIGTERM, invalid/partial commands, duplicate holds, authentication, and controller
SIGKILL. They do not establish game or desktop input delivery.

## Initial validation, 2026-10-12

- Host session reports KDE/X11.
- `/dev/uinput` is accessible to the desktop user through an existing ACL.
- The sandbox hides the device; the real probe was run on the host.
- libevdev runtime is installed; development metadata is absent.
- Real device creation and removal passed without sending keys.
- udev reports `ID_INPUT=1`, `ID_INPUT_KEY=1`, `ID_INPUT_KEYBOARD=1`,
  `XKBLAYOUT=de` for the created device.
- X11 lists `Caster uinput POC keyboard` attached to its master keyboard.
- All nine automated tests passed on the host, including the authenticated HTTP
  flow and release after the controller is killed. The sandbox disallows the
  HTTP test's listening socket; the helper-only tests pass inside it.
- The real web page was opened and displayed `KEY_V · Released`; no test button
  was pressed by the agent.
- Game delivery, physical-key interaction, and Wayland desktop compatibility
  remain manual validation items. This is not yet a claim of broad game support.

No system permissions, packages, or services were changed.
