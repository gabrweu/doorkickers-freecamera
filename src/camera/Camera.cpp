// Shared camera state, the per-frame driver, the takeover from the stock camera, and the mission and replay lifecycle.
// m_rotAngles becomes (-90 + tilt, yaw, 0). Map picking uses the same view matrix, so clicks still match.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <algorithm>
#include <cmath>

#include "Freecam.h"
#include "camera/CameraInternal.h"
#include "game/Game.h"
#include "settings/Settings.h"

namespace cam {

float g_targetYaw = 0;
float g_targetTilt = 0;
float g_yaw = 0;
float g_tilt = 0;
float g_appliedYaw = 0;
float g_appliedTilt = 0;

bool g_hasOrbit = false;
bool g_lookInPlace = false;
Vector3 g_pivot = {};
float g_distance = 0;

float g_targetDistance = 0;
bool g_animatingDistance = false;
Vector3 g_targetPivot = {};
bool g_animatingPivot = false;
float g_animationTau = 0;

float g_lastGroundY = 0;

bool g_hasUnlockedView = false;
float g_savedYaw = 0;
float g_savedTilt = 0;
Vector3 g_savedPivot = {};
float g_savedDistance = 0;

bool g_lockedLatched = false;
bool g_leftLockedView = false;
bool g_lockedZoomChanged = false;

bool g_engaged = false;

} // namespace cam

using namespace cam;

namespace {

constexpr float kStockPitch = -90.0f;
constexpr float kMinGlideTau = 0.1f; // seconds; the toggle's glide stays visible with smoothing 0

// the stock "tilt when zooming" (Camera::Update)
constexpr float kStockZoomTiltHeight = 40.0f; // gone at this height
constexpr float kStockZoomTiltMax = 7.0f; // degrees at m_minHeight
constexpr float kZoomLimitSlack = 0.5f; // height units

int g_lastDt = 16; // ms

bool g_startLockedPending = false;
bool g_wasEngaged = false;

// the view at a replay rewind, put back after the reload
struct KeptView {
    Vector3 pos;
    float yaw;
    float tilt;
    float targetYaw;
    float targetTilt;
    bool hasUnlockedView;
    float savedYaw;
    float savedTilt;
    Vector3 savedPivot;
    float savedDistance;
    bool lockedLatched;
    bool leftLockedView;
    float lastGroundY;
};

KeptView g_keptView = {};
bool g_rewindKept = false; // kept, the reload hasn't happened yet
bool g_restoreKeptView = false; // reloaded, restore once the replay runs

// This frame's Camera::Update applied the stock tilt. UpdateCamera sets the flag for the next frame, so it's read first.
bool g_beautyThisFrame = false;

// the stock tilt shown (hotkeys + zoom tilt), in freecam tilt degrees
float StockTilt(void* camera)
{
    if (!game::Camera_m_beautyMode(camera)) {
        return 0;
    }

    float hotkeyTilt = game::Camera_m_beautyAngles(camera).x;
    float minHeight = game::Camera_m_minHeight(camera);
    float range = kStockZoomTiltHeight - minHeight;
    float ratio = range > 0 ? std::clamp((Pos(camera).y - minHeight) / range, 0.0f, 1.0f) : 1.0f;
    float zoomTilt = ratio * kStockZoomTiltMax - kStockZoomTiltMax;
    return -(hotkeyTilt + zoomTilt) * kStockTiltSign;
}

// Starts from what the stock camera shows, so the view doesn't jump.
void TakeOverStockView(void* camera)
{
    Vector3 rot = game::Camera_m_rotAngles(camera);
    float tilt = std::clamp(rot.x - kStockPitch + StockTilt(camera), 0.0f, kMaxLookTilt);
    game::Camera_m_beautyAngles(camera) = {0, 0, 0};
    float yaw = WrapDegrees(rot.y);

    // dormant targets are 0, so they hold only this frame's input; keep it
    float yawInput = g_targetYaw;
    float tiltInput = g_targetTilt;
    g_tilt = g_appliedTilt = tilt;
    g_yaw = g_appliedYaw = yaw;
    g_targetTilt = std::clamp(tilt + tiltInput, 0.0f, kMaxLookTilt);
    g_targetYaw = WrapDegrees(yaw + yawInput);
}

bool PushingStockZoomLimit(void* camera)
{
    const Bounds* stock = GameBounds();
    if (!stock) {
        return false;
    }

    float height = Pos(camera).y;
    float push = game::Camera_m_impulse(camera).y;
    bool pushingIn = push < 0 && height <= stock->min.y + kZoomLimitSlack;
    bool pushingOut = push > 0 && height >= stock->max.y - kZoomLimitSlack;
    return pushingIn || pushingOut;
}

// Engaged as it was, so TakeOverStockView doesn't replace the angles.
void RestoreKeptView(void* camera)
{
    const KeptView& k = g_keptView;
    g_yaw = g_appliedYaw = k.yaw;
    g_tilt = g_appliedTilt = k.tilt;
    g_targetYaw = k.targetYaw;
    g_targetTilt = k.targetTilt;
    g_hasUnlockedView = k.hasUnlockedView;
    g_savedYaw = k.savedYaw;
    g_savedTilt = k.savedTilt;
    g_savedPivot = k.savedPivot;
    g_savedDistance = k.savedDistance;
    g_lockedLatched = k.lockedLatched;
    g_leftLockedView = k.leftLockedView;
    g_lastGroundY = k.lastGroundY;

    SetPos(camera, k.pos);
    game::Camera_m_impulse(camera) = {0, 0, 0};
    game::Camera_m_beautyAngles(camera) = {0, 0, 0};

    g_restoreKeptView = false;
    g_startLockedPending = false;
    g_engaged = true;
    g_wasEngaged = true;
}

// Returns true on the frame the mission's locked view starts.
bool UpdateEngaged(void* camera)
{
    if (g_restoreKeptView && game::MissionRunning()) {
        RestoreKeptView(camera);
    }

    // so the wider zoom range is reachable without rotating
    if (!g_engaged && PushingStockZoomLimit(camera)) {
        g_engaged = true;
    }

    // MissionRunning: not during the loading frames
    bool startLocked = g_startLockedPending && game::MissionRunning();
    if (startLocked) {
        g_startLockedPending = false;
        g_engaged = true;
    }

    if (g_engaged && !g_wasEngaged) {
        TakeOverStockView(camera);
    }
    g_wasEngaged = g_engaged;
    return startLocked;
}

void WriteAngles(void* camera)
{
    if (g_engaged) {
        game::Camera_m_rotAngles(camera) = {kStockPitch + g_tilt, g_yaw, 0.0f};
    }
    g_appliedYaw = g_yaw;
    g_appliedTilt = g_tilt;

    // the stock tilt rotates on top of UpdateViewMatrix's view
    if (g_engaged) {
        game::Camera_m_beautyMode(camera) = false;
    }
    g_beautyThisFrame = game::Camera_m_beautyMode(camera);
}

} // namespace

namespace cam {

HWND GameWindow()
{
    return static_cast<HWND>(game::api->GetGameWindow());
}

float WrapDegrees(float a)
{
    a = std::fmod(a, 360.0f);
    if (a > 180.0f) {
        a -= 360.0f;
    } else if (a <= -180.0f) {
        a += 360.0f;
    }
    return a;
}

bool Active()
{
    return g_yaw != 0 || g_tilt != 0;
}

bool TargetActive()
{
    return g_targetYaw != 0 || g_targetTilt != 0;
}

Vector3& Pos(void* camera)
{
    return game::Camera_m_pos(camera);
}

void SetPos(void* camera, Vector3 p)
{
    Pos(camera) = p;
    game::Camera_m_actualPos(camera) = p;
}

// The view axis is +-row 2 of m_matView. Tilt stays below 90 degrees, so it's the sign that points down.
Vector3 Forward(void* camera)
{
    const float* m = &game::Camera_m_matView(camera);
    Vector3 row2 = {m[8], m[9], m[10]};
    float len = Length(row2);
    if (len < 1e-6f) {
        return {0, -1, 0};
    }
    return Mul(row2, (row2.y < 0 ? 1.0f : -1.0f) / len);
}

float GlideTau()
{
    return std::max(g_settings.smoothing * 2.0f, kMinGlideTau);
}

} // namespace cam

bool Camera_Engaged()
{
    return g_engaged;
}

void Camera_OnMissionStart()
{
    g_targetYaw = g_targetTilt = 0;
    g_yaw = g_tilt = 0;
    g_appliedYaw = g_appliedTilt = 0;

    g_hasOrbit = false;
    g_lookInPlace = false;
    g_animatingDistance = false;
    g_animatingPivot = false;
    g_animationTau = 0;
    ResetViewRequests();

    g_hasUnlockedView = false;
    g_lockedLatched = false;
    g_leftLockedView = false;
    g_lockedZoomChanged = false;
    g_startLockedPending = g_settings.startLocked;

    ResetInput(); // the wheel's requests, and a drag
    ResetLimits();
    g_engaged = false;
    g_wasEngaged = false;
}

// Troop placement and the editor reset through Camera_OnMissionStart too, so a kept view becomes due only here.
void Camera_OnMapLoaded()
{
    bool rewound = g_rewindKept;
    g_rewindKept = false;
    Camera_OnMissionStart();

    g_restoreKeptView = rewound;
    if (rewound) {
        g_startLockedPending = false; // the kept view, not the locked view
    }
}

// dormant: the stock reset applies
void Camera_OnReplayRewind(void* client)
{
    if (!g_engaged || game::FreelookEnabled(client)) {
        return;
    }

    KeptView& k = g_keptView;
    k.pos = Pos(game::Camera(client));
    k.yaw = g_yaw;
    k.tilt = g_tilt;
    k.targetYaw = g_targetYaw;
    k.targetTilt = g_targetTilt;
    k.hasUnlockedView = g_hasUnlockedView;
    k.savedYaw = g_savedYaw;
    k.savedTilt = g_savedTilt;
    k.savedPivot = g_savedPivot;
    k.savedDistance = g_savedDistance;
    k.lockedLatched = g_lockedLatched;
    k.leftLockedView = g_leftLockedView;
    k.lastGroundY = g_lastGroundY;
    g_rewindKept = true;
}

float Camera_Yaw()
{
    return g_yaw;
}

float Camera_Tilt()
{
    return g_tilt;
}

bool Camera_Orbit(float* distance)
{
    *distance = g_distance;
    return g_hasOrbit;
}

float Camera_GroundY()
{
    return g_lastGroundY;
}

void Camera_BeforeUpdate(void* client, int dt)
{
    if (game::FreelookEnabled(client)) {
        g_hasOrbit = false;
        return; // the dev menu's FPS camera owns m_rotAngles
    }

    // Troop placement and the editor stay stock. The editor copies its camera over this one after this hook and loads
    // maps without MAP_LOADED, so engaged state is dropped here.
    void* camera = game::Camera(client);
    bool editing = game::Editing();
    if (game::Deploying() || editing) {
        if (g_engaged && !editing) {
            game::Camera_m_rotAngles(camera) = {kStockPitch, 0.0f, 0.0f};
        }
        if (g_engaged || Dragging()) {
            Camera_OnMissionStart(); // also ends a drag
        }
        return;
    }

    g_lastDt = dt;
    ReadInput(dt);

    if (WheelTiltHeld()) {
        g_engaged = true;
    }
    bool startLocked = UpdateEngaged(camera);
    ApplyWheelTilt(dt);
    ApplyBounds(camera);

    TrackOrbit(camera);
    TrackLockedZoom(camera);
    ApplyViewRequests(client, camera, startLocked);

    Ease(dt);
    bool orienting = UpdateOrbitPivot(client, camera);
    WriteAngles(camera);
    PlaceOnOrbit(camera, orienting);
}

void Camera_AfterUpdate(void* client)
{
    if (game::FreelookEnabled(client)) {
        return;
    }
    void* camera = game::Camera(client);

    bool moved = CapZoom(camera);
    moved |= ClampToMap(camera, g_lastDt);

    // A rebuild would drop the stock tilt, which Camera::Update applies; the move then shows next frame.
    if (moved && !g_beautyThisFrame) {
        game::Camera_UpdateViewMatrix(camera);
    }
}
