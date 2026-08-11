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

The Qt 6 application prepares and controls Waydroid through a nested compositor. The Android view scales while preserving its aspect ratio, accepts arbitrary resolutions, converts mouse input to Android touch, and supports F11 fullscreen. Closing Integrated Android with its title-bar `×` now hides that window normally without stopping the prepared Waydroid session, so it can be reopened from the controller.

The controller remembers the last selected resolution between application launches. The star beside the width and height adds or removes that exact size from favorite resolutions; the adjacent list switches between saved sizes while configuration is unlocked.

Press F6 inside Integrated Android to open the in-game profile manager. It is a movable, resizable panel that remains fully constrained to the game window. Profiles are presented as a compact, borderless grid of circular avatars for fast character switching: the active profile has a green ring and profiles without a variant for the current Android resolution are marked yellow. Imported images are centre-cropped into real transparent circles; existing square avatars are migrated automatically. Right-click an avatar to choose an image, duplicate the complete profile (including every resolution variant), or rename it. The title-bar `×` or F6 closes the manager, and `+` creates `Пустой профиль N` for the current resolution.

Mapper data uses a versioned `profile → resolution variant → complete control set` hierarchy. `Default` is special and permanent: the existing mapper migrates into it, it is pinned to exactly one resolution, and it can never migrate. Ordinary profiles can hold independent variants for multiple resolutions. Selecting one at a new resolution offers three explicit choices: proportionally auto-adapt the closest saved variant, create a completely blank variant for manual setup, or cancel. Auto-adaptation copies normalized positions, radii, binds, and calibration as a rough starting point; the user can then fine-tune it through F5. Compatibility decisions and warnings exist only inside the F6 manager, not before starting the game.

Press F5 inside Integrated Android to open the mapper editor. Right-click the Android screen and choose `Tap button`, then drag the marker with the left mouse button. A new tap defaults to `Hold until key release` and immediately waits for its keyboard bind; clicking outside leaves it unbound. Double-click a Tap or MOBA skill marker to bind it again. Every mapper marker now uses the same right-click menu: `Настройки`, `Сделать копию`, and `Удалить` (copy is disabled for unique controls). Settings provide exact Android X/Y coordinates; Tap also offers `Quick tap` and `Hold until key release`. `Done` (or F5) accepts and saves the draft. Bindings use normalized coordinates internally, so they survive window/fullscreen scaling and resolution changes.

The editor also provides `Character center (cross)` and `MOBA movement`. The character center is unique: adding it again moves the existing cross. The MOBA movement circle has a visible centre, a side triangle for changing its radius, and a gear with exact Android X/Y coordinates plus click timing controls. Every RMB press starts steering immediately; the click/hold threshold only decides what happens later when RMB is released. Holding past the threshold follows the cursor until release. A shorter click keeps walking in that direction; duration grows with cursor distance from Character center and is scaled by the profile's distance modifier. Any new RMB press invalidates the previous route timer but deliberately reuses its still-held Android finger, teleporting that finger to the new joystick direction without a release, centre reset, or movement pause. A second click therefore replaces the old route instantly, while a click followed by a hold transitions directly into live steering. At the default 100% modifier, a click one shorter-screen side from the center holds the joystick for 1600 ms. Centre-dependent controls show a warning until the cross exists. Mapper actions use independent native Wayland touch IDs, so held movement, timed routes, quick taps, and held skill buttons can operate simultaneously without releasing each other. The lowest free Android pointer ID is allocated globally, ensuring the first active gesture is always primary touch ID 0.

`MOBA skill` adds any number of independently bound ability joysticks. Each skill has exact centre coordinates, joystick diameter, keyboard bind, cast mode, start-speed profile, perspective calibration, and an optional artificial centre. With an artificial centre, the synthetic finger presses the configured physical button point first, moves to the real circular joystick centre, and only then follows the calibrated cursor direction. The five latency levels remain Stable (120 ms), Fast (60 ms), Very fast (30 ms), Instant (10 ms), and Superhuman (next event loop). After the initial route, the skill follows the cursor while held and releases only on the real key-up event.

`MOBA skill cancel` is a unique profile-wide control, like Character center: adding it again moves the existing marker instead of creating a duplicate. Its gear contains only exact Android X/Y coordinates and a keyboard bind. Pressing that bind while one or more MOBA skills are held moves each skill's existing touch to the configured in-game cancel region and releases it immediately; unrelated touches such as MOBA movement remain active. MOBA skill settings show a warning until the cancel control exists and has a key.

Skill calibration is an interactive 24-point measurement (eight directions at 33%, 67%, and 100% joystick distance). The mapper keeps one Android finger down throughout the complete wizard. Geometry changes no longer destroy measured data: they preserve the calibration and mark it with a warning. The skill menu can accept it as correct, restore the exact pre-change geometry/calibration snapshot, or run calibration again. Runtime aiming uses piecewise-affine interpolation over the measured triangular mesh, and each resolution variant preserves its own calibration independently.

F12 toggles gameplay cursor lock. It grabs the pointer and clamps it to the actual scaled Android picture, not the full host window, so letterbox bars are excluded. F11 continues to toggle fullscreen.

## Development build on Arch Linux

```bash
sudo pacman -S --needed base-devel cmake ninja qt6-base qt6-declarative qt6-wayland
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build
./build/evgenium-waydroid-mapper
```
