English | [日本語](android-ja.md)

# Android

The Android app is the same port as the desktop builds, with a touch pad on
the screen. Like them it carries no game data: it reads the game from your
own disc images every time it starts.

## Requirements

- Android 13 or later and a GPU driver with Vulkan 1.3.
- Your discs as Redump `.cue`/`.bin` images: US (SLUS-00664) or Japanese
  (SLPS-01160), each `.cue` beside its `.bin`.

Each release has two APKs. Phones and tablets take the one whose name has
`arm64-v8a`; the `x86_64` one is for emulators and x86 Chromebooks.

## Installing

1. Download the APK and its `.sha256` file from the same
   [release](https://github.com/EtherAura/xenogears-pc/releases).
2. Check the download: its SHA-256 must be the one in the `.sha256` file.
   With the Android SDK's build tools you can also check who signed it:
   `apksigner verify --print-certs <file>.apk` must report the certificate
   SHA-256 digest
   `5dd2ee3620a373151c959f76a9bda01c0dd7c8066084e75f5ecdf73952912ffb`.
   Every release is signed with this certificate.
3. Open the APK on the device and follow the installer. If Android asks,
   allow the app you opened it from to install apps; you can turn that off
   again afterwards. Where Android blocks the installation, see below.
4. Start the app. The first time, the system's folder picker asks for the
   folder that holds your disc images. They are read where they are (also
   from one folder down, as a folder per disc), nothing is copied, and the
   app keeps access to that folder. A PlayStation BIOS image (a 512 KB
   `.bin`) in the same folder is used too: its font draws the chapter names
   of your saves on the memory card screens. Without one, the app asks for
   one at the start; System > Discs and BIOS chooses another file, the
   folder's, or none.

The app can also check for new releases and install them itself, if you
allow it when it first asks ([Updates](building.md#updates)). Android asks
you once to let the app install apps. Obtainium can track the releases too:
add `https://github.com/EtherAura/xenogears-pc` and take the `arm64-v8a` APK.

To update, install the new APK over the old one. An APK signed with the same
certificate keeps your saves, settings and save states; uninstalling deletes
them. Saves > Backup in the menu writes the save folder to one `.zip` file
you choose, and restores it.

## When Android requires developer verification

Google is introducing developer verification for apps on certified Android
phones and tablets, in a few countries first and then worldwide. The
developer of this app is not verified. Where the rule applies, Android shows
that the installation is blocked. You can still install it in either of two
ways.

**The advanced flow.** A one-time setup on the device: turn on developer
options in the system settings, confirm, restart, and come back after a
one-day waiting period to confirm with your fingerprint, face or PIN. After
that the installer warns that the app is from an unverified developer and
offers to install it anyway. While the advanced flow is off, updates of the
app fail too.

**ADB.** From your own computer, with USB debugging turned on in the
device's developer options and the computer allowed on the device:

```sh
adb install -r <file>.apk
```

Google states that ADB installs work as before and have no waiting period.
`-r` installs over an existing installation and keeps its data.

Google's own pages:
[developer verification FAQ](https://developer.android.com/developer-verification/guides/faq)
and [ADB](https://developer.android.com/tools/adb#move).

## Playing

The touch pad shows from your first touch and goes away while you use a
controller or a keyboard. Controls > Touch pad sets its size and opacity (the
pad shows over the menu while you change them) and its layout: Buttons, or
Buttons and sticks, which adds a left stick that moves as the D-pad does and,
with the Enhanced engine's Free camera or First-person look turned on
(Controls > Camera and vibration), a right stick for the camera, a button that
puts the camera back and one that enters and leaves the first-person view.

The pad stays over the settings menu too, and works there as a controller
does: the D-pad moves, the game's confirm and cancel buttons choose and go
back, L1 and R1 change the section, and the menu button or Start closes it.
A touch anywhere else taps or drags the menu.

System > Engine chooses the Original engine, the game as the PlayStation ran
it (the default), or the Enhanced one, the port's own. On Android the app
starts again to switch.

## Packs

Packs live in the app's own folder (`Android/data/io.github.etheraura.xgport/files`).
Copy one in from the menu (Packs > Copy in a pack, from a folder you pick) or
over USB from a computer. See [Packs](packs.md) for what they are.
