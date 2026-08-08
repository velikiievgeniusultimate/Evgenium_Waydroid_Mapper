# Development progress

## 2026-08-08 — Environment and first injected touch

Test system:

- Arch Linux
- KDE Plasma on Wayland
- Linux `7.1.6-zen1-1-zen`
- AMD Radeon RX 6700 XT using `amdgpu`
- Waydroid `1.6.3`
- Android physical size: `1920x1080`
- `CONFIG_ANDROID_BINDER_IPC_RUST=y`

Verified:

- Waydroid container and full UI start successfully.
- An Android game launches successfully.
- The game ignores ordinary physical keyboard input, as expected for a touch-only game.
- Host command `sudo waydroid shell input tap 500 500` produces a touch recognized by the game.
- Android touch visualization was enabled with `settings put system show_touches 1`.

Next input milestone:

1. Verify separate `input motionevent DOWN` and `UP` events.
2. Capture a host keyboard press and release.
3. Translate those events into Android touch down/up.
4. Test stable holding and then simultaneous touches.

Observed graphics issue:

- Moving/flickering bands are visible in the Waydroid window.
- The bands are absent from screenshots and screen recordings.
- The bands occur only in fullscreen, pointing to KWin/direct scanout rather than Android framebuffer corruption.
- Work on this is deferred because the mapper overlay changes the presentation path and may avoid direct scanout entirely.

## 2026-08-08 — Native application started

- Qt 6 application skeleton added.
- The control window checks `waydroid status` asynchronously.
- A fullscreen translucent overlay captures distinct key press and release events.
- Escape exits the overlay.
- Android touch injection is deliberately the next isolated change.
