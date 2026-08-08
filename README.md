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

Install the latest ready-made GitHub Release:

```bash
curl -fsSL https://raw.githubusercontent.com/velikiievgeniusultimate/Evgenium_Waydroid_Mapper/main/scripts/install.sh | bash
```

The application is installed only for the current user under `~/.local`; it does not use AUR, compilation or root privileges.

```bash
evgenium-waydroid-mapper
evgenium-waydroid-mapper-update
```

Waydroid itself is intentionally not installed automatically yet. Its kernel, session and graphics requirements must be diagnosed on the target Arch Linux system before we automate anything privileged.

## Status

The first Qt 6 application skeleton can detect Waydroid and open a fullscreen keyboard-capturing overlay. Android touch injection is the next milestone.

## Development build on Arch Linux

```bash
sudo pacman -S --needed base-devel cmake ninja qt6-base
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build
./build/evgenium-waydroid-mapper
```
