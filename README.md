# EWM

**EWM** (Evgenium Waydroid Mapper) is a native keyboard and mouse mapper for Waydroid on Linux.

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

The application is installed only for the current user under `~/.local`; it does not use AUR or local compilation. Runtime force-start/force-stop may ask for system authorization through KDE PolicyKit because it controls `waydroid-container.service`. Updates stage a complete release beside the active installation and swap directories atomically, so the graphical updater can replace EWM while the old executable is still running without `Text file busy`.

On KDE Plasma the installer also registers the application in the launcher menu and places an executable `EWM` shortcut in the user's XDG desktop directory (including localized paths such as `Рабочий стол`). The shortcut starts the installed binary directly, so it does not depend on the terminal `PATH`. Running the updater refreshes the menu entry, icon, and desktop shortcut; uninstalling removes all three while preserving mapper profiles and settings.

```bash
evgenium-waydroid-mapper
evgenium-waydroid-mapper-update
```

On first launch EWM checks both the `waydroid` executable and
`/var/lib/waydroid/waydroid.cfg`. If either stage is missing, the controller offers
to complete it through KDE's PolicyKit prompt. It first installs the official Arch
package (`pacman -S --needed --noconfirm waydroid`) and then runs
`waydroid init -f -s GAPPS`, downloading Android images with Google Play. EWM checks
the config, both Android images, base properties, and the final LXC configuration;
an interrupted partial installation is offered for safe automatic repair. Launching and
Android configuration remain locked until both stages succeed. If PolicyKit or
pacman is unavailable, EWM reports that automatic setup is unsupported on that host.

```bash
sudo pacman -S --needed waydroid
```

The user confirms the complete setup once; EWM then advances from package install to
image initialization automatically and verifies the resulting configuration before
enabling `ЗАПУСТИТЬ`.

## Status

Version 0.23 adds Center Tracker V2 as the first deliberately isolated
computer-vision experiment with an automatic verdict. Press F2, prepare one
HP reference, then press `Запустить автоматический тест — 10 минут` and simply
play. A synthetic preflight first verifies three invariants: green HP wins over
a geometrically identical grey distractor, a locally tracked HP may turn grey,
and a lost target cannot be reacquired without the hero colour identity.

The live test runs in `SHADOW MODE`: it never changes Character center and
cannot inject mapper input. V2 requires three identity-confirmed frames before
acquisition, follows temporarily grey HP by geometry and motion prediction,
blocks implausible teleports, and only accepts a large relocation after three
consistent green-identity observations. Short occlusions are predicted for a
bounded number of frames; then the tracker returns to identity-only search.
At the end EWM assigns `PASS`, `QUESTIONABLE` or `FAIL`, writes a machine-readable
`autotest-summary.json`, saves short frame histories around loss, reacquisition
and blocked jumps, and automatically creates
`~/ewm-center-vision-autotest-*.tar.gz`. The test can be ended early, in which
case it is deliberately not eligible for PASS.

Version 0.22 added the original isolated computer-vision laboratory.
Press F2 inside Integrated Android to open `Поиск центра`. The lab captures the
Android `QWaylandSurface` directly, without desktop panels, window decorations
or letterbox bars. The user freezes one representative frame, drags a rectangle
around the hero HP bar (including its stable frame and any level/name identity
pixels), and clicks the actual character centre on the ground. The resulting
reference belongs to the active mapper profile and exact Android resolution.

Reference capture prefers the Android texture already rendered by the integrated
window and validates its alpha coverage, brightness and visual detail before it
is frozen. A blank compositor buffer never replaces the live game view. The
movable laboratory panel can export a focused vision diagnostic package from
its header with one click.

Tracking uses a masked colour-and-edge template matcher on a worker thread. It
prefers the HP frame and corners over the changing health fill, searches near
the previous position first, falls back to a full-frame reacquisition, and
reports `CONFIRM`, `LOCKED`, bounded `PREDICT`, and `LOST` states. The displayed centre is a
smoothed prediction while the raw observation remains in diagnostics. During
reference markup Android input is blocked; during live tracking normal mapper
gameplay is restored so the hero and camera can be moved under real conditions.
Ally and enemy HP bars are rejected by a local-player identity gate: a candidate
must contain pixels on the Mobile Legends hero HP gradient from `#499134` to
`#83f418`. The gate tolerates small renderer colour shifts and low remaining HP,
but a lone green scenery pixel is insufficient.

The lab is intentionally observation-only in this release: it cannot silently
replace Character center or alter skill aiming. `✓ Хорошо` records a positive
sample. `✕ Центр неверный` freezes the exact analyzed frame and asks for one
corrective click, recording the pixel error and updated HP-to-character offset.
Every captured frame and match writes timestamped capture latency, local/full
search mode, best and spatially distinct competitor scores, confidence, state
transitions, velocity,
candidate counts, analysis time and coordinates. Periodic and transition frames
are saved with the detected HP rectangle, raw point and smoothed centre drawn on
top. `Собрать пакет` creates a tar.gz containing the session log, annotated
frames and a snapshot of the reference/configuration for reproducible tuning.
These data stay local until the user explicitly shares the archive.

Version 0.21 introduces MEGA calibration and a global mapper baggage. A new
MOBA-skill calibration records the outer boundary first with 16 directions,
then measures five progressively smaller contours with 14, 12, 10, 8 and 6
directions. All 66 samples are collected with one uninterrupted Android finger.
Runtime aiming interpolates both distance and direction along learned rays from
Character center; beyond the measured outer contour only distance is clamped,
so the cursor direction keeps extending along the same ray. Existing 24-point
profiles remain fully supported by the legacy interpolation path and are only
replaced when the user explicitly runs MEGA calibration.

Every mapper marker now offers `В багаж` in its right-click menu. The user names
the reusable control, while the original remains in the current profile. Baggage
is stored outside profile variants and is therefore available from
`Кнопка из багажа` after right-clicking empty editor space in any character
profile or resolution. Tap buttons, Character center, MOBA movement, MOBA
skills and the skill-cancel point are supported. Normalized geometry adapts to
the target resolution; a reused calibrated skill remains usable but is marked
for review. The empty-space menu also provides explicit baggage deletion.

Version 0.20 adds a reversible Android device identity selector under the EWM settings gear. `Настоящий Waydroid` leaves the existing identity alone; `POCO F5 — Mobile Legends` presents Android as the MLBB-compatible POCO F5 (`23049PCD8G`, `marble`) while preserving the real Android version, ABI, fingerprint, Mesa driver and GPU. The helper runs only after Waydroid is fully stopped, saves the pre-EWM values, verifies both `waydroid.cfg` and `waydroid_base.prop`, and restores the exact originals when the native option is selected. System authorization is requested only when a profile actually needs to change; unchanged launches stay prompt-free. Changing the selection while Android is already prepared makes the next one-click start perform the required clean restart.

Version 0.19 keeps the compact EWM launcher introduced in 0.18 and hardens gameplay input. `ЗАПУСТИТЬ` serializes the complete lifecycle automatically: it stops any previous Waydroid session, applies the remembered resolution and touch properties, starts a clean final session, waits for the nested Android surface, and opens Integrated Android. `Изменить разрешение` safely stops Waydroid before exposing custom and favorite sizes. `МЕГА СТОП` remains available even while another operation is busy and immediately invalidates every older asynchronous lifecycle callback before killing the local launchers, LXC container, and complete `waydroid-container.service` cgroup. This operation-generation barrier prevents an old timed-out command from unexpectedly restarting Waydroid after emergency shutdown.

The top-right settings menu contains the MEGA-log collector and a graphical updater. The updater runs the current bootstrap installer without opening a terminal and reports success or failure inside EWM. When Evgenium VPN Manager 0.2.5+ is active, EWM automatically detects its local DIRECT SOCKS channel on `127.0.0.1:18443`; only update downloads use that channel, so GitHub bypasses the VPN without disabling it or changing the routing of unrelated programs. The obsolete standalone input overlay has been removed from the controller and build.

The Qt 6 application prepares and controls Waydroid through a nested compositor. The Android view scales while preserving its aspect ratio, accepts arbitrary resolutions, converts mouse input to Android touch, and supports F11 fullscreen. Closing Integrated Android with its title-bar `×` now hides that window normally without stopping the prepared Waydroid session, so it can be reopened from the controller.

Waydroid lifecycle control is deliberately fail-hard without destroying healthy infrastructure. Stop first settles the mapper independently: active synthetic fingers are released, accepted profile data is persisted, and an unfinished F5 draft is reverted exactly as before. Android then receives a short graceful-stop opportunity. A successful synchronous D-Bus stop preserves the healthy `waydroid-container.service`, including its one-time binder/LXC preparation. Only a failed or timed-out stop enters recovery: the mapper kills leftover `session start`/`show-full-ui` launchers, uses privileged `lxc-stop -k` to break the Android container directly, waits for the existing D-Bus manager to answer `Peer.Ping`, and asks it to finish session cleanup. Destroying the whole systemd cgroup is now the last resort; if it also resists, the mapper sends `SIGKILL` and verifies `ActiveState` before unlocking resolution controls. It never loops on `waydroid status`, because that command depends on the same manager that can hang. Start verifies the service and retries a failed Android session once through this recovery ladder. System authorization can be requested during emergency recovery; cancelling it produces a bounded error instead of an endless wait.

Version 0.16 adds persistent application logging and a focused Waydroid MEGA-diagnostics collector. A final start failure launches it automatically; it can also be started manually from the controller. After one PolicyKit confirmation it writes `~/evgenium-waydroid-mega-log-YYYYMMDD-HHMMSS.txt` containing the EWM runtime history, `waydroid-container.service` journal, bounded D-Bus probes, LXC state and log tails, binder/mount/network state, systemd cgroup data, kernel stacks for relevant blocked processes, and a short `strace` of the container manager when available. The collector deliberately excludes the full environment, unrelated user files, browser data, credentials, and unrelated process/network listings.

The controller remembers the last selected resolution between application launches. The star beside the width and height adds or removes that exact size from favorite resolutions; the adjacent list switches between saved sizes while configuration is unlocked.

Press F6 inside Integrated Android to open the in-game profile manager. It is a movable, resizable panel that remains fully constrained to the game window. Profiles are presented as a compact, borderless grid of circular avatars for fast character switching: the active profile has a green ring and profiles without a variant for the current Android resolution are marked yellow. Imported images are centre-cropped into real transparent circles; existing square avatars are migrated automatically. Right-click an avatar to choose an image, duplicate the complete profile (including every resolution variant), rename it, or delete it after confirmation. `Default` remains protected from deletion. The title-bar `×` or F6 closes the manager, and `+` creates `Пустой профиль N` for the current resolution.

Mapper data uses a versioned `profile → resolution variant → complete control set` hierarchy. `Default` is special and permanent: the existing mapper migrates into it, it is pinned to exactly one resolution, and it can never migrate. Ordinary profiles can hold independent variants for multiple resolutions. Selecting one at a new resolution offers three explicit choices: proportionally auto-adapt the closest saved variant, create a completely blank variant for manual setup, or cancel. Auto-adaptation copies normalized positions, radii, binds, and calibration as a rough starting point; the user can then fine-tune it through F5. Compatibility decisions and warnings exist only inside the F6 manager, not before starting the game.

The experimental F2 center-vision laboratory uses a narrow vertical control panel. During HP-template, character-anchor, and correction selection it automatically collapses into a small instruction strip, leaving the frozen Android frame bright and almost completely unobstructed for precise marking.

Press F5 inside Integrated Android to open the mapper editor. Right-click the Android screen and choose `Tap button`, then drag the marker with the left mouse button. A new tap defaults to `Hold until key release` and immediately waits for its keyboard bind; clicking outside leaves it unbound. Double-click a Tap or MOBA skill marker to bind it again. Every mapper marker now uses the same right-click menu: `Настройки`, `Сделать копию`, `В багаж`, and `Удалить` (copy is disabled for unique controls). Settings provide exact Android X/Y coordinates; Tap also offers `Quick tap` and `Hold until key release`. `Done` (or F5) accepts and saves the draft. Bindings use normalized coordinates internally, so they survive window/fullscreen scaling and resolution changes.

The editor also provides `Character center (cross)` and `MOBA movement`. The character center is unique: adding it again moves the existing cross. The MOBA movement circle has a visible centre, a side triangle for changing its radius, and right-click settings with exact Android X/Y coordinates plus click timing controls. Every RMB press starts steering immediately; the click/hold threshold only decides what happens later when RMB is released. Holding past the threshold follows the cursor until release. A shorter click keeps walking in that direction; duration grows with cursor distance from Character center and is scaled by the profile's distance modifier. Any new RMB press invalidates the previous route timer but deliberately reuses its still-held Android finger, teleporting that finger to the new joystick direction without a release, centre reset, or movement pause. A second click therefore replaces the old route instantly, while a click followed by a hold transitions directly into live steering. At the default 100% modifier, a click one shorter-screen side from the center holds the joystick for 1600 ms. Centre-dependent controls show a warning until the cross exists. Mapper actions use independent native Wayland touch IDs, so held movement, timed routes, quick taps, and held skill buttons can operate simultaneously without releasing each other. The lowest free Android pointer ID is allocated globally, ensuring the first active gesture is always primary touch ID 0.

`MOBA skill` adds any number of independently bound ability joysticks. Each skill has exact centre coordinates, joystick diameter, keyboard bind, cast mode, start-speed profile, perspective calibration, and an optional artificial centre. In edit mode the artificial DOWN point is a separate draggable orange circle, with an arrow pointing along the initial route to the real joystick centre. With an artificial centre, the synthetic finger presses that physical button point first, moves to the real circular joystick centre, and only then follows the calibrated cursor direction. The five latency levels remain Stable (120 ms), Fast (60 ms), Very fast (30 ms), Instant (10 ms), and Superhuman (next event loop). After the initial route, the skill follows the cursor while held and releases only on the real key-up event.

All synthetic fingers now share one tracked lifecycle. While any mapper touch is down, the compositor blocks Waydroid's parallel physical mouse stream and converts an intentional left click into an independent touch instead. MOBA skill startup frames also carry a unique gesture generation, so delayed frames from a cancelled cast cannot affect the next cast of the same skill.

`MOBA skill cancel` is a unique profile-wide control, like Character center: adding it again moves the existing marker instead of creating a duplicate. Its right-click settings contain only exact Android X/Y coordinates and a keyboard bind. Every skill is cancellable by default, but can opt out independently. Its reaction profile moves the already-held finger smoothly into the configured cancel target before releasing it: Gentle (180 ms), Smooth (110 ms), Balanced (65 ms, default), Fast (30 ms), or Instant. Releasing the skill key during this transition does not turn the cancellation back into a cast, and unrelated touches such as MOBA movement remain active. The MOBA skill settings are grouped into scrollable Geometry, Input and cast, Skill cancellation, Artificial centre, and Perspective calibration sections.

MEGA skill calibration is an interactive 66-point measurement. It starts at the maximum joystick radius and records 16 directions, then traverses five inner contours with 14, 12, 10, 8 and 6 staggered directions. The mapper keeps one Android finger down throughout the complete wizard, moving back through the real joystick centre without releasing it between samples. Each selected endpoint is stored together with its Character-centre ray. Runtime aiming reconstructs a direction-specific sequence of contour crossings and interpolates joystick radius and angle between them. Outside maximum range the radius saturates while the learned ray continues towards the cursor. Geometry changes no longer destroy measured data: they preserve the calibration and mark it with a warning. The skill menu can accept it as correct, restore the exact pre-change geometry/calibration snapshot, or run calibration again. Existing 24-point calibrations continue using the original piecewise-affine triangular mesh, and each resolution variant preserves its own calibration independently.

F12 toggles gameplay cursor lock. It grabs the pointer and clamps it to the actual scaled Android picture, not the full host window, so letterbox bars are excluded. F11 continues to toggle fullscreen. Super/Windows/Meta and any Super chord are consumed only while Integrated Android has focus, before they reach the nested Waydroid compositor; KDE/KWin still receives and handles the physical key normally.

## Development build on Arch Linux

```bash
sudo pacman -S --needed base-devel cmake ninja qt6-base qt6-declarative qt6-wayland
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build
./build/evgenium-waydroid-mapper
```
