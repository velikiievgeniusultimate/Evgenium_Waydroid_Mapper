# Architecture draft

The mapper is split at security and platform boundaries rather than being embedded into Waydroid Helper.

1. **Qt 6 application** — profile editor, Waydroid window tracking and visible overlay.
2. **Linux input service** — keyboard and mouse events with explicit user-granted access.
3. **Waydroid transport** — a narrow local protocol between the host and container.
4. **Android agent** — deterministic multitouch injection with stable pointer IDs.

## First technical milestone

- Detect the active Waydroid application and content geometry.
- Map one physical key to one Android touch point.
- Support press, hold and release without lost events.
- Verify at least four simultaneous touches.
- Measure input latency before adding WASD, swipe and mouse-look controls.

## Non-goals for the bootstrap

- Modifying files owned by Waydroid Helper.
- Requiring root for update checks.
- Installing unreviewed AUR packages.
- Pretending the Android injection mechanism is selected before it is tested.

