# Install on Android

The APK requires Android 13 or later, a Vulkan 1.3 driver, and an arm64 or
x86-64 device. Bring your own US or Japanese Xenogears PlayStation disc
images. Keep each .cue file next to its matching .bin file. The APK
includes no game data.

1. Download the Android APK and its SHA-256 file from the same
   [GitHub release](https://github.com/EtherAura/xenogears-pc/releases).
2. Compare the APK's SHA-256 with the value in that file. With the Android
   SDK Build Tools, run `apksigner verify --print-certs <file.apk>` to
   check its signature. The release certificate's SHA-256 digest is
   `5dd2ee3620a373151c959f76a9bda01c0dd7c8066084e75f5ecdf73952912ffb`.
3. Open the APK on Android and follow the installer. If asked, allow
   installation from the app you opened the file with. You can turn that
   permission off again after installing.
4. Open Xenogears. Use the Android file picker to select the folder holding
   your disc images.

## When Android requires developer verification

Project Exodus uses its own release signing key. On certified devices where
Android enforces developer verification for this install, you can use
[Google's advanced flow](https://developer.android.com/developer-verification/guides/faq#advanced-flow).
The one-time setup has a 24-hour waiting period.

You can also install over Android Debug Bridge. Enable USB debugging, connect
your own computer and approve it on your device, then run:

```sh
adb install -r <file.apk>
```

ADB installs use the usual workflow and do not require the advanced flow's
waiting period. Use an APK with the same signing certificate to update an
existing release installation and keep its app data. See
[Android developer verification](https://developer.android.com/developer-verification/guides/faq)
and [ADB installation](https://developer.android.com/tools/adb#move).
