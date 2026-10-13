

CasterSoundboard
================
[![License](https://img.shields.io/github/license/JupiterBroadcasting/CasterSoundboard.svg)](https://github.com/JupiterBroadcasting/CasterSoundboard/blob/master/LICENSE)

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
You can build and install the Flatpak on any Linux distribution with `flatpak` and `flatpak-builder`:
```bash
flatpak install flathub org.kde.Platform//6.8 org.kde.Sdk//6.8
flatpak-builder --user --install --force-clean build-flatpak io.github.covarianttensor.CasterSoundboard.yml
flatpak run io.github.covarianttensor.CasterSoundboard
```

### Nix
Build and run using `default.nix`:
```bash
nix-build
./result/bin/CasterSoundboard
```

### Arch AUR
Thanks to [@j605](https://github.com/j605 "Jagannathan Tiruvallur Eachambadi") for creating a `PKGBUILD` for people on Arch who want to try this software:
+ [CasterSoundboard-git](https://aur.archlinux.org/packages/castersoundboard-git)

### Fedora 25 and newer
RPM package can be found in the [Copr](https://copr.fedorainfracloud.org/coprs/fszymanski/castersoundboard/) repository:
```
# dnf copr enable fszymanski/castersoundboard
# dnf install castersoundboard
```

### Building From Source

First clone the repository using `git`, if you don't have `git` installed
you can find installation instructions [here](https://git-scm.com)

```
$ git clone https://github.com/covarianttensor/CasterSoundboard
```

Now we need to install the Qt 6 development libraries and tools, the
commands for various platforms are listed below:

*Arch / CachyOS*
```
# pacman -S qt6-base qt6-multimedia qt6-wayland ffmpeg base-devel
```

*Fedora*
```
# dnf install qt6-qtbase-devel qt6-qtmultimedia-devel make gcc gcc-c++
```

*Debian and Ubuntu*
```
# apt install qt6-base-dev qt6-multimedia-dev qmake6 build-essential
```

*Mac OSX*
```
# brew install qt
```

You should then have access to `qmake6` (or `qmake`), `make`, and the Qt 6 development
libraries. Next `cd` into the project folder inside the repo:

```
$ cd CasterSoundboard/CasterSoundboard
```

You can specify the target directory using the `PREFIX` `qmake` variable (default: `/usr/local`).
Now run `qmake6`, this will auto generate a makefile:

```
$ qmake6 PREFIX=/usr
```

Then run `make` to build the project:

```
$ make
```

And finally run `make install` to install the app:

```
# make install
```

Now you're good to go!

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
