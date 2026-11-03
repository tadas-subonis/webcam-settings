#pragma once

#include <windows.h>
#include <QtGlobal>

// Separate key I/O lets tests use a temporary user key, never camera configuration.
namespace WindowsCameraRotation::detail {
LSTATUS readKey(HKEY key, qint64 *degrees);
LSTATUS writeKey(HKEY key, qint64 degrees);
}
