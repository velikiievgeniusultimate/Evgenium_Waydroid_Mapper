# Evgenium Waydroid Mapper

Native keyboard and mouse mapper for Waydroid on Linux. The project is currently in its bootstrap phase: repository structure, installation, diagnostics and release updates are being built before input injection.

## Planned components

- `app/` — Qt 6 desktop interface and profile editor.
- `input-service/` — Linux keyboard and mouse capture.
- `android-agent/` — multitouch injection inside Waydroid.
- `packaging/` — distribution packages.
- `scripts/` — installation and maintenance tools.
- `docs/` — architecture and development notes.

## Bootstrap installation

After the first GitHub Release is published:

```bash
curl -fsSL https://raw.githubusercontent.com/velikiievgeniusultimate/Evgenium_Waydroid_Mapper/main/scripts/install.sh | bash
```

The bootstrap is installed only for the current user under `~/.local`; it does not use AUR and does not require root privileges.

```bash
evgenium-waydroid-mapper doctor
evgenium-waydroid-mapper check-update
evgenium-waydroid-mapper update
```

Waydroid itself is intentionally not installed automatically yet. Its kernel, session and graphics requirements must be diagnosed on the target Arch Linux system before we automate anything privileged.

## Status

No input mapping is implemented yet. Version `0.0.x` is the installer and architecture bootstrap.

