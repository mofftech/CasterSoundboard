

CasterSoundboard
================
[![License](https://img.shields.io/github/license/covarianttensor/CasterSoundboard.svg)](https://github.com/covarianttensor/CasterSoundboard/blob/master/LICENSE)

A soundboard for hot-keying and playing back sounds. (For podcasting)

CasterSoundboard is written in C++ Qt6.
---

## Guides
+ [OSC colour and grid metadata API](docs/osc-metadata.md)
+ [Control CasterSoundboard with your phone or tablet via a network](https://github.com/covarianttensor/CasterSoundboard/wiki/Guide:-How-to-Control-CasterSoundboard-Using-Your-Phone-or-Tablet "CasterSoundboard's Wiki")
---

## Libraries Used
+ [osc-cpp-qt](https://github.com/MugenSAS/osc-cpp-qt)
---

## How to Install

### Flatpak (Universal Linux)
Download the pre-built `CasterSoundboard.flatpak` bundle from the [Releases page](https://github.com/covarianttensor/CasterSoundboard/releases/latest), then install and run:
```bash
flatpak install --user CasterSoundboard.flatpak
flatpak run io.github.covarianttensor.CasterSoundboard
```

Or build and install locally from source:
```bash
flatpak install flathub org.kde.Platform//6.8 org.kde.Sdk//6.8
flatpak-builder --user --install --force-clean build-flatpak io.github.covarianttensor.CasterSoundboard.yml
```

### AppImage (Standalone Portable)
Download the pre-built `CasterSoundboard-x86_64.AppImage` from the [Releases page](https://github.com/covarianttensor/CasterSoundboard/releases/latest), then make it executable and run:
```bash
chmod +x CasterSoundboard-x86_64.AppImage
./CasterSoundboard-x86_64.AppImage
```

Or build a standalone AppImage locally:
```bash
./scripts/build-appimage.sh
```

### Nix
Build and run using `default.nix`:
```bash
nix-build
./result/bin/CasterSoundboard
```

### Screenshot ###
![Alt text](/docs/img/screenshot.png?raw=true "Running on Ubuntu")

### Embedded web remote (proof of concept)

Caster automatically serves a button page at **http://localhost:5052**. From a
phone or another computer, use **http://CASTER-PC-IP:5052**; the address also
appears in Caster's status bar. Open profiles in Caster normally. The page shows
all loaded tabs and their assigned sounds, using the same displayed track names
as the desktop/OSC interface. Button colours and grid positions match the desktop,
including gaps for empty slots. Narrow screens scroll horizontally to preserve
the grid. Text automatically uses black or white for contrast. Empty slots have no buttons. Click a sound to play it;
click again to stop. A green outline and Playing label indicate actual playback, including playback
started from the desktop or OSC. Finished sounds return to their stopped state.
Audio plays through Caster's existing output devices, not in the browser.

No settings, additional libraries, browser packages, or OSC configuration are
needed. This uses the existing Qt Network module, an embedded HTML resource,
and Server-Sent Events. Playback changes are sent immediately; tab/label changes
are checked every 100 ms while a browser is connected. Browsers reconnect
automatically and disable buttons while disconnected. Tab IDs remain stable
until closed, so an old button cannot accidentally trigger a different tab.

This is a trusted-LAN remote: port 5052 listens on IPv4 interfaces with no login
or TLS. Anyone able to reach it can control playback. If the port is occupied,
Caster still runs and reports the web server error in its status bar. Profiles
are opened in the desktop app; this proof of concept does not browse disk files
or stream audio/OSC packets to the browser.

Build separately to preserve an existing executable (from the repository root):

```sh
mkdir -p build-web-remote
cd build-web-remote
qmake6 ../CasterSoundboard/CasterSoundboard.pro
make -j4
./CasterSoundboard
```

Integration tests use Qt Test (development only), a silent WAV, and port 15052:

```sh
mkdir -p build-web-tests
cd build-web-tests
qmake6 ../tests/web/web-tests.pro
make -j4
QT_QPA_PLATFORM=offscreen ./web-tests
```

### Actions

Use **Actions… → Pin to tab** to configure PTT, mute-toggle, or ordinary playback
for a tab. Multiple profiles can be attached; one is selected. Save the Actions,
then save the tab's `.caster` profile. See [Actions setup and behaviour](docs/actions.md).
