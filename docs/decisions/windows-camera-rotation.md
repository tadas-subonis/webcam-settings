# Windows camera rotation

External USB webcams can lack a hardware Roll control while still needing a
rotation correction in conferencing applications. Rotating the Qt preview would
only affect this application's display. A virtual camera would add a continuously
running capture service and a different camera selection in conferencing apps.

Use the documented Windows `FSSensorOrientation` device-interface override instead.
Resolve the selected USB video interface with Configuration Manager, write only
its orientation value, and read it back before reporting success. Convert the
user's clockwise output angle into the inverse sensor mounting offset. Expose the
override as `windows:rotation` through the existing menu/profile interfaces.

The desktop application first attempts a direct write. On access denied, it runs
the adjacent CLI through Windows' administrator prompt with an exact device ID.
The helper only writes rotation; it does not open the video stream. A failed or
cancelled write restores the displayed value from the saved configuration. The
CLI itself never requests elevation, making unattended failures explicit.

This override is machine-wide and separate from Windows Settings' per-user
rotation defaults. Existing streams may need reopening, or the camera may need
reconnecting. Windows removes compressed formats when applying the correction;
applications that bypass the camera pipeline may ignore it. No driver installation,
virtual camera, or profile migration is required. Selecting No rotation restores
a zero pipeline offset without changing other camera controls or per-user defaults.

Reference: [Microsoft camera orientation documentation](https://learn.microsoft.com/en-us/windows-hardware/drivers/stream/camera-device-orientation).
