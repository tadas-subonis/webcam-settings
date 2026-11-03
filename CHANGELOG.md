# Changelog

## 0.2.1 — 2026-11-03

- Add Windows video rotation for external USB webcams through the camera
  pipeline, with an administrator prompt when needed, profile support, and a
  `rotate` CLI command. Reopen camera streams after changing orientation.

- Reject process IDs outside the native operating system range before attempting
  termination. Previously, large IDs could wrap and target a different process
  or, on Linux/macOS, a process group.
- Reject malformed, fractional, out-of-range, and duplicate profile entries
  before applying settings. Preserve full signed 64-bit integer values.
- Use V4L2 integer-menu indexes when reading, selecting, and saving choices,
  while retaining the advertised integer values as display labels.
- Restore the remembered camera before opening a default device, so startup
  no longer overwrites the previous selection.

Profiles containing invalid entries now fail with an error instead of partially
loading or silently replacing invalid numbers with zero.
