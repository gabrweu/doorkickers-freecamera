// Keys, modifier + mouse drags, the HUD wheel's requests, the stock tilt keys and edge-scroll.
// The stock tilt keys move m_beautyAngles.x (clamped to -20..0). Engaged, beauty mode stays off and that change
// becomes freecam tilt.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <algorithm>
#include <cmath>
#include <utility>

#include "Freecam.h"
#include "camera/CameraInternal.h"
#include "game/Game.h"
#include "settings/Settings.h"

using namespace cam;

namespace {

constexpr float kMouseDegreesPerPixel = 0.25f;
constexpr float kOnGridTolerance = 1e-3f; // in steps
constexpr float kStockTiltRate = 0.02f; // m_beautyAngles.x per ms while a tilt key is held
constexpr float kStockTiltBaseline = -10.0f; // mid-range, so both key directions survive the game's -20..0 clamp

bool g_dragging = false;
POINT g_dragAnchor = {};
bool g_dragLook = false;

// The game never calls SetCursor, so the window class cursor shows. A drag swaps it.
HWND g_cursorWindow = nullptr;
HCURSOR g_savedClassCursor = nullptr;

bool g_toggleWasDown = false;
bool g_rotateLeftWasDown = false;
bool g_rotateRightWasDown = false;
bool g_resetWasDown = false;

// the HUD wheel's requests
constexpr float kWheelRotateStep = 90.0f; // degrees
int g_wheelRotate = 0;
bool g_wheelReset = false;
int g_wheelTilt = 0;

bool KeyDown(int vk)
{
    return (GetAsyncKeyState(vk) & 0x8000) != 0;
}

// maxTilt, or the current tilt if a look in place went beyond it, so an orbit doesn't snap back
float OrbitTiltLimit(float tilt)
{
    return std::max(g_settings.maxTilt, std::min(tilt, kMaxLookTilt));
}

// Screen right and up in X/Z. Right is view row 0. Row 1's sign doesn't match screen up, so up is right's
// perpendicular (right +X, up +Z at yaw 0, as the stock edge-scroll has them).
void ScreenAxes(void* camera, Vector3& right, Vector3& up)
{
    const float* m = &game::Camera_m_matView(camera);
    Vector3 rowRight = {m[0], 0, m[2]};
    float rightLen = Length(rowRight);
    right = rightLen < 1e-6f ? Vector3{1, 0, 0} : Mul(rowRight, 1.0f / rightLen);
    up = {-right.z, 0, right.x};
}

void BeginDragCursor()
{
    g_cursorWindow = GameWindow();
    if (!g_cursorWindow) {
        return;
    }

    HCURSOR dragCursor = g_settings.hideCursorWhileDragging ? nullptr : LoadCursorW(nullptr, IDC_SIZEALL);
    g_savedClassCursor = reinterpret_cast<HCURSOR>(
        SetClassLongPtrW(g_cursorWindow, GCLP_HCURSOR, reinterpret_cast<LONG_PTR>(dragCursor)));
    SetCursor(dragCursor);
}

void EndDragCursor()
{
    if (!g_cursorWindow) {
        return;
    }

    SetClassLongPtrW(g_cursorWindow, GCLP_HCURSOR, reinterpret_cast<LONG_PTR>(g_savedClassCursor));
    SetCursor(g_savedClassCursor);
    g_cursorWindow = nullptr;
}

// the next multiple of step in that direction
float StepYaw(float yaw, int direction, float step)
{
    float k = 0;
    if (direction < 0) {
        k = std::ceil(yaw / step - kOnGridTolerance) - 1.0f;
    } else {
        k = std::floor(yaw / step + kOnGridTolerance) + 1.0f;
    }
    return WrapDegrees(k * step);
}

// The cursor's offset from the anchor becomes yaw and tilt, and the cursor goes back to the anchor, so the drag is
// unlimited and doesn't edge-scroll.
bool ApplyDragDelta(POINT cursor, bool look)
{
    bool reverse = look ? g_settings.reverseLookDrag : g_settings.reverseOrbitDrag;
    float sens = kMouseDegreesPerPixel * g_settings.mouseSensitivity * (reverse ? -1.0f : 1.0f);
    bool rotates = cursor.x != g_dragAnchor.x && !g_settings.lockDragRotation;
    bool tilts = cursor.y != g_dragAnchor.y && !g_settings.lockDragTilt;
    if (rotates) {
        g_targetYaw += (cursor.x - g_dragAnchor.x) * sens;
    }
    if (tilts) {
        g_targetTilt += (cursor.y - g_dragAnchor.y) * sens;
    }
    SetCursorPos(g_dragAnchor.x, g_dragAnchor.y);
    if (!rotates && !tilts) {
        return false;
    }

    g_lookInPlace = look;
    return true;
}

// Shift + the toggle key opens the settings window.
void ReadToggleKey(bool listening)
{
    bool toggleHeld = KeyDown(g_settings.toggleViewKey);
    if (listening && toggleHeld && !g_toggleWasDown) {
        if (KeyDown(VK_SHIFT)) {
            g_windowOpen = !g_windowOpen;
        } else {
            RequestToggle(true);
        }
    }
    g_toggleWasDown = toggleHeld;
}

// Rotate modifier + mouse orbits, look modifier + mouse turns in place.
bool ReadDrag(bool inputAllowed)
{
    bool steering = false;
    bool lookHeld = inputAllowed && KeyDown(g_settings.lookModifier);
    bool orbitHeld = inputAllowed && !lookHeld && KeyDown(g_settings.rotateModifier);

    if (lookHeld || orbitHeld) {
        POINT cursor;
        GetCursorPos(&cursor);
        if (!g_dragging) {
            g_dragging = true;
            g_dragAnchor = cursor;
            BeginDragCursor();
        } else {
            steering |= ApplyDragDelta(cursor, lookHeld);
        }
        g_dragLook = lookHeld;
    } else if (g_dragging) {
        // the movement since the last warp counts too; after an alt-tab the cursor is the other window's
        if (game::api->IsGameFocused()) {
            POINT cursor;
            GetCursorPos(&cursor);
            steering |= ApplyDragDelta(cursor, g_dragLook);
            if (g_settings.cursorToCenterAfterDrag) {
                CursorToScreenCenter();
            }
        }
        g_dragging = false;
        EndDragCursor();
    }
    return steering;
}

struct RotateInput {
    bool steered = false;
    bool glide = false; // a step, a slice or the reset
    bool rotateKeys = false; // they leave the locked view; the wheel and the reset key don't
};

// The wheel's slices count even while the keys aren't listening: the click is the input.
RotateInput ReadRotateKeys(bool inputAllowed, int dt)
{
    bool keysAllowed = inputAllowed && !g_dragging;
    bool leftHeld = keysAllowed && KeyDown(g_settings.rotateLeftKey);
    bool rightHeld = keysAllowed && KeyDown(g_settings.rotateRightKey);
    bool resetHeld = keysAllowed && KeyDown(g_settings.resetHeadingKey);
    RotateInput input;

    if (g_settings.rotateStep > 0) {
        if (leftHeld && !g_rotateLeftWasDown) {
            g_targetYaw = StepYaw(g_targetYaw, -1, g_settings.rotateStep);
            input.glide = true;
            input.rotateKeys = true;
        }
        if (rightHeld && !g_rotateRightWasDown) {
            g_targetYaw = StepYaw(g_targetYaw, 1, g_settings.rotateStep);
            input.glide = true;
            input.rotateKeys = true;
        }
    } else {
        float step = g_settings.keyRotateSpeed * dt / 1000.0f;
        if (leftHeld) {
            g_targetYaw -= step;
            input.steered = true;
            input.rotateKeys = true;
        }
        if (rightHeld) {
            g_targetYaw += step;
            input.steered = true;
            input.rotateKeys = true;
        }
    }
    int wheelRotate = std::exchange(g_wheelRotate, 0);
    if (wheelRotate != 0) {
        g_targetYaw = StepYaw(g_targetYaw, wheelRotate, kWheelRotateStep);
        input.glide = true;
    }

    bool wheelReset = std::exchange(g_wheelReset, false);
    bool resetPressed = (resetHeld && !g_resetWasDown) || wheelReset;
    if (resetPressed && g_targetYaw != 0) {
        g_targetYaw = 0;
        input.glide = true;
    }

    g_rotateLeftWasDown = leftHeld;
    g_rotateRightWasDown = rightHeld;
    g_resetWasDown = resetHeld;
    if (leftHeld || rightHeld || input.glide) {
        input.steered |= input.glide;
        g_lookInPlace = false;
    }
    return input;
}

// beautyDelta: the change to m_beautyAngles.x (cam_tilt_up adds, cam_tilt_down subtracts)
void TiltLikeStockKeys(float beautyDelta)
{
    float tiltLimit = OrbitTiltLimit(g_targetTilt);
    g_targetTilt = std::clamp(g_targetTilt - beautyDelta * kStockTiltSign, 0.0f, tiltLimit);
    g_lookInPlace = false;
    g_animationTau = 0;
    LeaveLockedView();
}

// The stock tilt keys and the wheel's tilt slices. In the locked view they change lockedTilt.
void TiltInput(float beautyDelta)
{
    if (g_lockedLatched) {
        AdjustLockedTilt(beautyDelta);
    } else {
        TiltLikeStockKeys(beautyDelta);
    }
}

} // namespace

namespace cam {

void ReadInput(int dt)
{
    bool listening =
        game::api->IsGameFocused() && !Ui_IsRebinding() && !Freecam_GameMenuOpen() && !HudButton_DialogOpen();
    bool inputAllowed = listening && !g_windowOpen;

    ReadToggleKey(listening);

    float tiltBefore = g_targetTilt;
    bool dragSteering = ReadDrag(inputAllowed);
    RotateInput rotate = ReadRotateKeys(inputAllowed, dt);

    // The reset key and the wheel turn the locked view and keep it; the rotate keys and the mouse leave it.
    bool turnLocked = g_lockedLatched && rotate.steered && !rotate.rotateKeys && !dragSteering;
    if (dragSteering || rotate.steered) {
        g_animationTau = 0;
        g_animatingPivot = false;
        g_engaged = true;
        if (!turnLocked) {
            LeaveLockedView();
        }
    }
    if (rotate.glide) {
        g_animationTau = GlideTau();
    }

    float tiltLimit = kMaxLookTilt;
    if (!g_lookInPlace) {
        tiltLimit = OrbitTiltLimit(tiltBefore);
    }
    g_targetYaw = WrapDegrees(g_targetYaw);
    g_targetTilt = std::clamp(g_targetTilt, 0.0f, tiltLimit);
    if (turnLocked) {
        KeepLockedHeading();
    }
}

void CursorToScreenCenter()
{
    HWND window = GameWindow();
    RECT rc;
    if (!window || !GetClientRect(window, &rc)) {
        return;
    }

    POINT center = {(rc.left + rc.right) / 2, (rc.top + rc.bottom) / 2};
    if (ClientToScreen(window, &center)) {
        SetCursorPos(center.x, center.y);
    }
}

bool Dragging()
{
    return g_dragging;
}

bool WheelTiltHeld()
{
    return g_wheelTilt != 0;
}

void ApplyWheelTilt(int dt)
{
    if (g_wheelTilt != 0) {
        TiltInput(g_wheelTilt * kStockTiltRate * dt);
    }
}

void ResetInput()
{
    g_wheelRotate = 0;
    g_wheelReset = false;
    g_wheelTilt = 0;

    if (g_dragging) {
        g_dragging = false;
        EndDragCursor();
    }
}

} // namespace cam

void Camera_WheelRotate(int direction)
{
    g_wheelRotate = direction < 0 ? -1 : 1;
}

void Camera_WheelResetHeading()
{
    g_wheelReset = true;
}

void Camera_WheelHoldTilt(int direction)
{
    g_wheelTilt = direction;
}

void Camera_PrepareStockTilt(void* client)
{
    if (!g_engaged || game::FreelookEnabled(client)) {
        return;
    }

    game::Camera_m_beautyAngles(game::Camera(client)) = {kStockTiltBaseline, 0, 0};
}

void Camera_AbsorbStockTilt(void* client)
{
    if (!g_engaged || game::FreelookEnabled(client)) {
        return;
    }

    Vector3& beauty = game::Camera_m_beautyAngles(game::Camera(client));
    float delta = beauty.x - kStockTiltBaseline;
    beauty = {0, 0, 0};
    if (delta != 0) {
        TiltInput(delta);
    }
}

void Camera_EdgeScrollToScreen(void* client, const Vector3& impulseBefore)
{
    if (g_yaw == 0 || game::FreelookEnabled(client)) {
        return;
    }

    void* camera = game::Camera(client);
    Vector3& impulse = game::Camera_m_impulse(camera);
    float dx = impulse.x - impulseBefore.x;
    float dz = impulse.z - impulseBefore.z;
    if (dx == 0 && dz == 0) {
        return;
    }

    // the game's edge-scroll: right edge +X, top edge +Z
    Vector3 right;
    Vector3 up;
    ScreenAxes(camera, right, up);
    impulse.x = impulseBefore.x + dx * right.x + dz * up.x;
    impulse.z = impulseBefore.z + dx * right.z + dz * up.z;
}
