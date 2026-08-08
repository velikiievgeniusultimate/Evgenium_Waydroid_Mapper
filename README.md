# Evgenium Waydroid Mapper

Native keyboard and mouse mapper for Waydroid on Linux.

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

The Qt 6 application prepares and controls Waydroid through a nested compositor. The Android view scales while preserving its aspect ratio, accepts arbitrary resolutions, converts mouse input to Android touch, and supports F11 fullscreen.

Press F5 inside Integrated Android to open the mapper editor. Right-click the Android screen and choose `Tap button`, then drag the marker with the left mouse button. Its gear opens exact Android X/Y coordinates and keyboard binding. `Done` (or F5) accepts and saves the draft. Bindings use normalized coordinates internally, so they survive window/fullscreen scaling and resolution changes.

The editor also provides `Character center (cross)` and `MOBA movement`. The character center is unique: adding it again moves the existing cross. The MOBA movement circle has a visible centre and a side triangle for changing its radius. Outside edit mode, hold the right mouse button anywhere over Android; the mapper measures the cursor angle from the character centre and holds the virtual joystick in the same direction. Centre-dependent controls show a warning until the cross exists.

## Development build on Arch Linux

```bash
sudo pacman -S --needed base-devel cmake ninja qt6-base qt6-declarative qt6-wayland
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build
./build/evgenium-waydroid-mapper
```
