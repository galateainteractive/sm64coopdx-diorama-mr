# SM64 Diorama MR v0.9.0 beta

This beta introduces the Diorama MR presentation for Meta Quest 3. The game
world appears as a room-scale miniature in passthrough while Mario remains
fully playable.

## Recommended download

Download and run:

```text
SM64-Diorama-MR-v0.9.0-Installer.exe
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
SM64-Diorama-MR-v0.9.0.apk
```

The final Galatea application identity is
`com.galateainteractive.sm64dioramamr`. Its private Quest data directory is:

```text
/sdcard/Android/data/com.galateainteractive.sm64dioramamr/files
```

The installer safely migrates saves, configuration and ROM data from the
earlier pre-release `com.fulldivegames.sm64coopdxvr` directory when the new
directory is still empty. The original directory is retained as a backup.

## Important update note

The v0.9.0 APK uses the new permanent Galatea release-signing certificate.
Development builds signed with the Android debug certificate cannot be updated
in place. If Android reports a signature conflict, back up any important save
data, uninstall the debug build, and run the v0.9.0 installer again.

Future releases signed with the same protected key can update v0.9.0 in place.

## Highlights

- Diorama MR enabled by default for new configurations.
- Quest passthrough behind the complete miniature game world.
- Room-scale observation independent from Mario's camera position.
- One-hand drag and two-hand character-centered scale and rotation.
- Headset-relative Mario controls stabilized across transitions and respawns.
- Spatial Mario audio and contextual passthrough color effects.
- Improved billboard rendering and conservative hidden-room object culling.
- Permanent Galatea Android package identity with resumable legacy-data migration.

## Integrity

Verify the downloads against `SHA256SUMS-v0.9.0.txt`.

The Windows installer is not currently Authenticode-signed, so Windows may
show an unknown-publisher SmartScreen warning. The embedded Android APK is
release-signed by Galatea Interactive.
