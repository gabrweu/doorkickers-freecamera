#pragma once

// Shared by camera/Camera*.cpp only.

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include "game/Game.h"

namespace cam {

constexpr float kMinOrbitDistance = 1.0f;
constexpr float kPlayerImpulseEpsilon = 1e-3f; // m_impulse above this: the player is panning or zooming
constexpr float kMaxLookTilt = 89.0f; // just short of the horizon, so "down" stays defined
constexpr float kStockTiltSign = 1.0f; // freecam tilt per degree of -m_beautyAngles.x
constexpr float kUnlimited = 9999.0f; // Camera::SetDefaults' "no limits"

// --- Camera.cpp ---

// Degrees. Input sets the targets; the applied angles ease toward them. Yaw is in (-180, 180], tilt is away from
// straight down.
extern float g_targetYaw;
extern float g_targetTilt;
extern float g_yaw; // this frame
extern float g_tilt;
extern float g_appliedYaw; // last frame
extern float g_appliedTilt;

extern bool g_hasOrbit;
extern bool g_lookInPlace; // turning in place (look modifier), not orbiting
extern Vector3 g_pivot;
extern float g_distance;

extern float g_targetDistance;
extern bool g_animatingDistance;
extern Vector3 g_targetPivot;
extern bool g_animatingPivot;
extern float g_animationTau; // seconds; >0 while a glide runs

extern float g_lastGroundY; // height of the last good pivot

// the unlocked view, saved when it's left
extern bool g_hasUnlockedView;
extern float g_savedYaw;
extern float g_savedTilt;
extern Vector3 g_savedPivot;
extern float g_savedDistance;

// The locked view shows. A tilted locked view has non-zero targets, so it needs a flag.
extern bool g_lockedLatched;
// A flat view no longer counts as the locked view; only the mission's start view does.
extern bool g_leftLockedView;

// Off: dormant, the camera and m_bounds are stock. Reset on every mission start.
extern bool g_engaged;

HWND GameWindow();
float WrapDegrees(float a);
bool Active(); // the applied angles aren't stock
bool TargetActive();
Vector3& Pos(void* camera);
void SetPos(void* camera, Vector3 p); // m_pos and m_actualPos
Vector3 Forward(void* camera);
float GlideTau(); // seconds

// --- CameraInput.cpp ---
void ReadInput(int dt);
void CursorToScreenCenter();
bool Dragging();
bool WheelTiltHeld();
void ApplyWheelTilt(int dt);
void ResetInput(); // ends a drag, drops the wheel's requests

// --- CameraOrbit.cpp ---
float HeightAboveGround(Vector3 pos);
float MaxRaycastDistance(float height);
void CapturePivot(void* client, void* camera);
void TrackOrbit(void* camera);
bool UpdateOrbitPivot(void* client, void* camera);
void Ease(int dt);
void PlaceOnOrbit(void* camera, bool orienting);

// --- CameraLockedView.cpp ---
void RequestToggle(bool atCursor);
void ResetViewRequests();
void LeaveLockedView();
void KeepLockedHeading();
void AdjustLockedTilt(float beautyDelta);
void TrackLockedZoom(void* camera);
void ApplyViewRequests(void* client, void* camera, bool startLocked);

// --- CameraLimits.cpp ---
const Bounds* GameBounds(); // null before the first ApplyBounds
void ApplyBounds(void* camera);
void ResetLimits();
void ClampPointToMap(Vector3* p);
float ClampDistanceToZoomRange(float distance, float tiltDeg);
bool CapZoom(void* camera);
bool ClampToMap(void* camera, int dt);

} // namespace cam
