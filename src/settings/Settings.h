#pragma once

#include <string>

// for the ini clamps and both settings screens' sliders
struct Range {
    float min, max;
};

constexpr Range kRotateStepRange{0.0f, 90.0f};
constexpr Range kKeyRotateSpeedRange{10.0f, 360.0f};
constexpr Range kMaxTiltRange{10.0f, 75.0f};
constexpr Range kSmoothingRange{0.0f, 0.5f};
constexpr Range kMouseSensitivityRange{0.1f, 5.0f};
constexpr Range kLockedTiltRange{0.0f, 30.0f};
constexpr Range kLockedHeadingRange{-180.0f, 180.0f};
constexpr Range kLockedZoomRange{0.0f, 1.0f};
constexpr Range kZoomInFactorRange{0.02f, 1.0f};
constexpr Range kZoomOutFactorRange{1.0f, 5.0f};
constexpr Range kEdgeRoomRange{0.0f, 200.0f};

// The defaults live only here.
struct Settings {
    // virtual-key codes
    int rotateModifier = 0x12; // Alt: hold + mouse to orbit
    int lookModifier = 0x4E;   // N: hold + mouse to look in place
    int rotateLeftKey = 0x5A;  // Z
    int rotateRightKey = 0x43; // C
    int toggleViewKey = 0x14;  // Caps Lock
    int resetHeadingKey = 0x56; // V: north-up, keeping the tilt

    float mouseSensitivity = 0.45f;
    bool reverseOrbitDrag = false;
    bool reverseLookDrag = true;
    bool lockDragRotation = false; // drags only tilt
    bool lockDragTilt = false;     // drags only rotate
    bool hideCursorWhileDragging = false; // false: a "move" cursor
    bool cursorToCenterAfterDrag = false; // false: back where the drag started

    float rotateStep = 0.0f;      // degrees per key press; 0: turn while held
    float keyRotateSpeed = 85.0f; // degrees per second
    float maxTilt = 60.0f;        // degrees from straight down
    float smoothing = 0.05f;      // seconds; 0: instant

    bool startLocked = true;
    bool lockedScreenCenter = false; // false: the trooper nearest the cursor
    bool cursorToCenterOnLock = true;
    // the defaults match the stock mission start: straight down, north-up, fully zoomed out
    float lockedTilt = 0.0f;    // degrees
    float lockedHeading = 0.0f; // degrees, 0 = north-up
    float lockedZoom = 1.0f;    // 0 = stock closest, 1 = stock farthest

    float zoomInFactor = 0.02f;   // times the stock minimum height
    float zoomOutFactor = 2.0f;   // times the stock maximum height
    bool allowOutsideMap = false;
    float edgeRoom = 15.0f; // world units

    bool uprightIcons = true;
    bool hudButton = true; // the camera button in the bottom bars
};

extern Settings g_settings;

void Settings_Load(const std::wstring& path);
void Settings_Save();
// the locked view's tilt, heading and zoom, and the drag locks
void Settings_ResetCameraView();
