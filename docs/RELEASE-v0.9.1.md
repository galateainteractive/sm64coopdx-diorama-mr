# SM64 Diorama MR v0.9.1 beta

This branding update adds the finished SM64 Diorama MR identity to the Quest
application library and to the game's loading/menu screen. Gameplay and the
mixed-reality behavior remain the same as v0.9.0.

## Recommended download

Download and run:

```text
SM64-Diorama-MR-v0.9.1-Installer.exe
```

The Windows installer contains the signed mod APK and the required ADB tools.
It does not contain a Super Mario 64 ROM or extracted Nintendo assets. Each
player must select their own legally obtained, unmodified US `.z64` ROM. The
installer validates the supported ROM before copying it to the headset.

Developer Mode and USB debugging must be enabled on the Quest. Accept the USB
debugging prompt inside the headset, select the Quest in the installer, and
install. On first launch, accept any mixed-reality or passthrough permission.

The standalone APK is also supplied for advanced manual installation:

```text
SM64-Diorama-MR-v0.9.1.apk
```

The application identity remains
`com.galateainteractive.sm64dioramamr`. Its private Quest data directory is:

```text
/sdcard/Android/data/com.galateainteractive.sm64dioramamr/files
```

## Updating from v0.9.0

Install v0.9.1 directly over v0.9.0. The package identity and permanent
Galatea signing certificate are unchanged, so Android can update the app in
place while retaining the private ROM, saves, settings, and other app data.
Do not uninstall first if you want those private files to remain available.

If Android reports a signature conflict with an older development build, back
up any important data, uninstall that development build, and use the v0.9.1
installer.

## Highlights

- New SM64 Diorama MR icon in the Quest application library.
- New Diorama MR loading/menu logo.
- Same Diorama MR gameplay and mixed-reality presentation as v0.9.0.
- Same Android package, permissions, private data path, and release signature.
- In-place update support from the public v0.9.0 release.

## Integrity

Verify the downloads against `SHA256SUMS-v0.9.1.txt`.

The Windows installer is not currently Authenticode-signed, so Windows may
show an unknown-publisher SmartScreen warning. The embedded Android APK is
release-signed by Galatea Interactive.
