#pragma once

#include <string>

// A float setting's limits. The ini reader clamps to them, and both settings screens use them for their sliders.
struct Range {
    float min, max;
};

constexpr Range kRotateStepRange{0.0f, 90.0f};
constexpr Range kKeyRotateSpeedRange{10.0f, 360.0f};
constexpr Range kMaxTiltRange{10.0f, 75.0f};
constexpr Range kSmoothingRange{0.0f, 0.5f};
constexpr Range kMouseSensitivityRange{0.1f, 5.0f};
constexpr Range kTopDownTiltRange{0.0f, 30.0f};
constexpr Range kTopViewZoomRange{0.0f, 1.0f};
constexpr Range kZoomInFactorRange{0.02f, 1.0f};
constexpr Range kZoomOutFactorRange{1.0f, 5.0f};
constexpr Range kEdgeRoomRange{0.0f, 200.0f};

// The defaults are defined only here. Settings_Load falls back to them, and "reset" assigns Settings{}.
struct Settings {
    // Keys, as virtual-key codes.
    // VK_MENU (Alt): hold + move the mouse to rotate/tilt around the screen center
    int rotateModifier = 0x12;
    // VK_CONTROL (Ctrl): hold + move the mouse to look around in place
    int lookModifier = 0x11;
    int rotateLeftKey = 0x5A;  // Z
    int rotateRightKey = 0x43; // C
    // VK_OEM_3, backtick/tilde on US layouts: top-down <-> saved angle. Shift + it opens the settings window.
    int toggleViewKey = 0xC0;
    // V: glide back to north-up (rotation 0), keeping the tilt
    int resetHeadingKey = 0x56;

    // Mouse drags.
    float mouseSensitivity = 0.45f;
    bool reverseOrbitDrag = false;        // Alt + mouse: reverse both directions
    bool reverseLookDrag = true;          // Ctrl + mouse: reverse both directions
    bool hideCursorWhileDragging = false; // false: show a "move" cursor while dragging
    // After a rotate/look drag the cursor goes to the screen center (else back where the drag started).
    bool cursorToCenterAfterDrag = false;

    // Rotation and tilt.
    float rotateStep = 0.0f;      // >0: the rotate keys turn in steps of this many degrees (0 = turn while held)
    float keyRotateSpeed = 85.0f; // degrees per second
    float maxTilt = 60.0f;        // degrees away from straight down
    float smoothing = 0.05f;      // seconds the camera takes to ease to a new angle (0 = instant)

    // The top view.
    bool startInTopView = true;       // missions start in the top view (after troop placement)
    bool topViewScreenCenter = false; // the top view keeps the screen center (else: the trooper nearest the cursor)
    // The toggle key's switch to the top view puts the cursor at the screen center.
    bool cursorToCenterOnTopView = true;
    float topDownTilt = 20.0f; // the toggle's top view: tilt in degrees (north-up), 0 = straight down
    float topViewZoom = 0.3f;  // the top view's zoom: 0 = stock closest, 1 = stock farthest

    // Zoom and map limits.
    float zoomInFactor = 0.02f;   // closest zoom, as a fraction of the stock minimum height
    float zoomOutFactor = 2.0f;   // farthest zoom, as a multiple of the stock maximum height
    bool allowOutsideMap = false; // false: what the camera looks at stays over the map (+ edgeRoom)
    float edgeRoom = 15.0f;       // how far past the map edges the camera may look, in world units

    // Map icons (waypoint actions, doors, ...) face the camera while freecam is engaged, instead of lying flat.
    bool uprightIcons = true;
};

extern Settings g_settings;

void Settings_Load(const std::wstring& path);
void Settings_Save();
