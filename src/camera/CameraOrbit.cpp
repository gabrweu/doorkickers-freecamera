// The orbit and the easing. While the angles change, the camera stays at pivot - forward * distance. The pivot is
// raycast once when a rotation starts and analytic after that, so a ray hitting walls or sky can't make it jump. Pans
// and zooms move it, and it's dropped after kOrbitIdleMs.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <algorithm>
#include <cmath>

#include "Freecam.h"
#include "camera/CameraInternal.h"
#include "game/Game.h"
#include "settings/Settings.h"

using namespace cam;

namespace {

constexpr DWORD kOrbitIdleMs = 500;
constexpr float kSnapDegrees = 0.05f; // easing snaps to the target below these
constexpr float kSnapDistance = 0.01f;

// A raycast hit farther than kRaycastRange camera heights (at least kRaycastMinHeight) is sky or far horizon.
constexpr float kRaycastRange = 12.0f;
constexpr float kRaycastMinHeight = 10.0f;
constexpr float kPivotMinAlignment = 0.95f; // cosine to the view axis, ~18 degrees
constexpr float kPivotGroundMaxForwardY = -0.2f; // above this, the view ray meets the ground too far away

DWORD g_lastOrientTick = 0;
Vector3 g_lastPos = {}; // at the end of our last frame

bool ScreenCenterOnMap(void* client, Vector3* out)
{
    const int* viewport = &game::GameClient_m_viewport(client);
    if (viewport[2] <= 0 || viewport[3] <= 0) {
        return false;
    }

    float cx = viewport[0] + viewport[2] * 0.5f;
    float cy = viewport[1] + viewport[3] * 0.5f;
    game::GameClient_ConvertScreenToMapCoords(client, out, cx, cy);
    return std::isfinite(out->x) && std::isfinite(out->y) && std::isfinite(out->z);
}

// A move since our last frame (pan, zoom, bounds push) becomes a change of distance (along the view axis) and a pivot
// shift (the rest).
void FollowExternalMove(void* camera)
{
    Vector3 delta = Sub(Pos(camera), g_lastPos);
    if (Dot(delta, delta) < 1e-8f) {
        return;
    }

    Vector3 f = Forward(camera);
    float along = Dot(delta, f);
    g_distance = std::max(g_distance - along, kMinOrbitDistance);
    g_pivot = Add(g_pivot, Sub(delta, Mul(f, along)));
}

} // namespace

namespace cam {

float HeightAboveGround(Vector3 pos)
{
    return std::max(pos.y - g_lastGroundY, 1.0f);
}

float MaxRaycastDistance(float height)
{
    return std::max(height, kRaycastMinHeight) * kRaycastRange;
}

// The screen-center ground point. The raycast counts only along the view axis at a sane distance; otherwise the view
// ray meets the last ground height.
void CapturePivot(void* client, void* camera)
{
    Vector3 pos = Pos(camera);
    Vector3 f = Forward(camera);
    float height = HeightAboveGround(pos);
    float maxDistance = MaxRaycastDistance(height);

    Vector3 hit;
    if (ScreenCenterOnMap(client, &hit)) {
        Vector3 d = Sub(hit, pos);
        float dist = Length(d);
        bool sane = dist > kMinOrbitDistance && dist < maxDistance;
        if (sane && Dot(d, f) > kPivotMinAlignment * dist) {
            g_pivot = hit;
            g_distance = dist;
            g_lastGroundY = hit.y;
            g_hasOrbit = true;
            return;
        }
    }

    // near the horizon that point is too far, so two heights out
    float dist = height * 2.0f;
    if (f.y < kPivotGroundMaxForwardY) {
        dist = (g_lastGroundY - pos.y) / f.y;
    }
    g_distance = std::clamp(dist, kMinOrbitDistance, maxDistance);
    g_pivot = Add(pos, Mul(f, g_distance));
    g_hasOrbit = true;
}

// Player pans and zooms go through m_impulse, which a glide zeroes when it starts. Other moves (the game's height
// clamp, ClampToMap, CapZoom) don't cancel a glide.
void TrackOrbit(void* camera)
{
    if (!g_hasOrbit) {
        return;
    }

    Vector3 impulse = game::Camera_m_impulse(camera);
    bool playerMoving = Dot(impulse, impulse) > kPlayerImpulseEpsilon * kPlayerImpulseEpsilon;
    if ((g_animatingDistance || g_animatingPivot) && playerMoving) {
        g_animatingDistance = false;
        g_animatingPivot = false;
        g_targetDistance = g_distance;
    }
    if (!g_animatingDistance && !g_animatingPivot) {
        FollowExternalMove(camera);
    }
}

// Returns true while the angles or a glide are changing.
bool UpdateOrbitPivot(void* client, void* camera)
{
    bool orienting = g_yaw != g_appliedYaw || g_tilt != g_appliedTilt || g_animatingDistance || g_animatingPivot;
    DWORD now = GetTickCount();
    if (orienting) {
        if (g_lookInPlace) {
            g_hasOrbit = false;
        } else if (!g_hasOrbit) {
            CapturePivot(client, camera); // from last frame's view, which holds the point to keep centered
        }
        g_lastOrientTick = now;
    } else if (g_hasOrbit && now - g_lastOrientTick > kOrbitIdleMs) {
        g_hasOrbit = false;
    }
    return orienting;
}

void Ease(int dt)
{
    float tau = g_animationTau > 0 ? g_animationTau : g_settings.smoothing;
    float alpha = tau <= 0 ? 1.0f : 1.0f - std::exp(-(dt / 1000.0f) / tau);

    float yawDelta = WrapDegrees(g_targetYaw - g_yaw); // shortest way around
    g_yaw = std::fabs(yawDelta) < kSnapDegrees ? g_targetYaw : WrapDegrees(g_yaw + yawDelta * alpha);
    float tiltDelta = g_targetTilt - g_tilt;
    g_tilt = std::fabs(tiltDelta) < kSnapDegrees ? g_targetTilt : g_tilt + tiltDelta * alpha;

    if (g_animatingDistance) {
        float distDelta = g_targetDistance - g_distance;
        if (std::fabs(distDelta) < kSnapDistance) {
            g_distance = g_targetDistance;
            g_animatingDistance = false;
        } else {
            g_distance += distDelta * alpha;
        }
    }

    if (g_animatingPivot) {
        Vector3 pivotDelta = Sub(g_targetPivot, g_pivot);
        if (Length(pivotDelta) < kSnapDistance) {
            g_pivot = g_targetPivot;
            g_animatingPivot = false;
        } else {
            g_pivot = Add(g_pivot, Mul(pivotDelta, alpha));
        }
    }

    bool animating = g_animatingDistance || g_animatingPivot;
    bool anglesReached = g_yaw == g_targetYaw && g_tilt == g_targetTilt;
    if (g_animationTau > 0 && !animating && anglesReached) {
        g_animationTau = 0;
    }
}

void PlaceOnOrbit(void* camera, bool orienting)
{
    if (orienting && g_hasOrbit) {
        game::Camera_UpdateViewMatrix(camera); // for the new view axis
        SetPos(camera, Sub(g_pivot, Mul(Forward(camera), g_distance)));
    }
    g_lastPos = Pos(camera);
}

} // namespace cam
