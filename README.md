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

Press F5 inside Integrated Android to open the mapper editor. Right-click the Android screen and choose `Tap button`, then drag the marker with the left mouse button. Its gear opens a draggable settings window with exact Android X/Y coordinates, keyboard binding, and two activation modes: `Quick tap` releases after 35 ms, while `Hold until key release` keeps its touch down for as long as the keyboard key is held. The top-right `×` removes a control. `Done` (or F5) accepts and saves the draft. Bindings use normalized coordinates internally, so they survive window/fullscreen scaling and resolution changes.

The editor also provides `Character center (cross)` and `MOBA movement`. The character center is unique: adding it again moves the existing cross. The MOBA movement circle has a visible centre and a side triangle for changing its radius. Outside edit mode, hold the right mouse button anywhere over Android; the mapper measures the cursor angle from the character centre and holds the virtual joystick in the same direction. Centre-dependent controls show a warning until the cross exists. Mapper actions use independent native Wayland touch IDs, so held movement, quick taps, and held skill buttons can operate simultaneously without releasing each other.

`MOBA skill` adds any number of independently bound ability joysticks. Each skill has five settings: exact centre coordinates, joystick diameter, keyboard bind, cast mode, and perspective calibration. The first cast mode presses the skill when its key goes down, follows the mouse cursor while held, and releases the Android touch to cast when the key goes up.

Skill calibration is an interactive 24-point measurement (eight directions at 33%, 67%, and 100% joystick distance). The mapper holds and moves the virtual ability joystick while the user clicks the actual endpoint shown by the game. Runtime aiming uses piecewise-affine interpolation over the measured triangular mesh instead of assuming a fixed ellipse; points outside the measured area are clamped to the nearest outer edge. Accidental double clicks are gated while the joystick settles, a Back button repairs the previous sample, and Esc cancels the process while restoring the prior calibration. Moving the character centre, skill centre, skill diameter, or Android resolution deliberately invalidates stale calibration.

## Development build on Arch Linux

```bash
sudo pacman -S --needed base-devel cmake ninja qt6-base qt6-declarative qt6-wayland
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build
./build/evgenium-waydroid-mapper
```
