// Camera limits, the far plane and centered move targets.
// CollideWithBounds clamps to m_bounds and shrinks X/Z to zero at max.y, which pins a zoomed-out camera to the map
// center. Engaged, it gets unlimited X/Z and max.y = kApexScale * g_zoomCap, and ClampToMap keeps the look-at point on
// the map.
#include <algorithm>
#include <cmath>

#include "Freecam.h"
#include "camera/CameraInternal.h"
#include "game/Game.h"
#include "settings/Settings.h"

using namespace cam;

namespace {

constexpr float kApexScale = 2.0f;
constexpr float kNearHorizontalY = -0.25f; // forward.y above this: the look-at point is too far to clamp
constexpr float kSnapOvershoot = 2.0f; // past the edge by more than this, the camera slides back
constexpr float kSlideBackTau = 0.15f; // seconds
constexpr float kMinDownForZoomClamp = 0.1f; // below this, distance barely changes height

constexpr float kFarPlaneHeightScale = 1.25f; // times the height above ground
constexpr float kFarPlaneMargin = 50.0f;
constexpr float kFallbackMapDiagonal = 500.0f;
constexpr float kFarPlaneTiltedPerHeight = 4.0f; // extra reach per unit of camera height when tilted
constexpr float kMaxFarPlane = 20000.0f;

bool g_haveGameBounds = false;
Bounds g_gameBounds = {};
Bounds g_wantedBounds = {}; // for CollideWithBounds while engaged
bool g_haveWantedBounds = false;
Bounds g_swappedOutBounds = {};
bool g_boundsSwapped = false;
float g_zoomCap = kUnlimited;

} // namespace

namespace cam {

const Bounds* GameBounds()
{
    return g_haveGameBounds ? &g_gameBounds : nullptr;
}

// m_bounds is never written: other code reads it as the map rectangle. The wider limits are swapped in only for the
// CollideWithBounds call.
void ApplyBounds(void* camera)
{
    g_gameBounds = game::Camera_m_bounds(camera);
    g_haveGameBounds = true;
    if (!g_engaged) {
        g_wantedBounds = g_gameBounds;
        g_haveWantedBounds = false;
        g_zoomCap = kUnlimited;
        return;
    }

    Bounds wanted = g_gameBounds;

    float stockMin = g_gameBounds.min.y;
    float stockMax = g_gameBounds.max.y;
    wanted.min.y = stockMin * g_settings.zoomInFactor;
    if (stockMax > stockMin) {
        g_zoomCap = stockMax * g_settings.zoomOutFactor;
        wanted.max.y = g_zoomCap * kApexScale;
    } else {
        g_zoomCap = kUnlimited;
    }

    // ClampToMap handles X/Z
    wanted.min.x = wanted.min.z = -kUnlimited;
    wanted.max.x = wanted.max.z = kUnlimited;

    g_wantedBounds = wanted;
    g_haveWantedBounds = true;
}

// so no limits from the last map reach CollideWithBounds before ApplyBounds runs
void ResetLimits()
{
    g_haveGameBounds = false;
    g_haveWantedBounds = false;
    g_zoomCap = kUnlimited;
}

// as ClampToMap would, so it doesn't fight the end of a glide to this point
void ClampPointToMap(Vector3* p)
{
    if (g_settings.allowOutsideMap || !g_haveGameBounds) {
        return;
    }

    float room = g_settings.edgeRoom;
    p->x = std::clamp(p->x, g_gameBounds.min.x - room, g_gameBounds.max.x + room);
    p->z = std::clamp(p->z, g_gameBounds.min.z - room, g_gameBounds.max.z + room);
}

// An orbit distance that keeps the height in the zoom range, above the pivot a glide ends at.
float ClampDistanceToZoomRange(float distance, float tiltDeg)
{
    float down = std::cos(tiltDeg * kDegToRad);
    if (down < kMinDownForZoomClamp) {
        return distance;
    }

    float groundY = g_animatingPivot ? g_targetPivot.y : g_pivot.y;
    float lo = (g_wantedBounds.min.y - groundY) / down;
    float hi = (std::min(g_zoomCap, g_wantedBounds.max.y) - groundY) / down;
    if (hi <= lo) {
        return distance;
    }
    return std::clamp(distance, std::max(lo, kMinOrbitDistance), hi);
}

// Returns true if the camera moved.
bool CapZoom(void* camera)
{
    Vector3& pos = Pos(camera);
    if (pos.y <= g_zoomCap) {
        return false;
    }

    pos.y = g_zoomCap;
    game::Camera_m_actualPos(camera).y = g_zoomCap;
    Vector3& impulse = game::Camera_m_impulse(camera);
    if (impulse.y > 0) {
        impulse.y = 0;
    }
    return true;
}

// Keeps the screen-center ground point (from the view axis, no raycast) within the map + edgeRoom, or the camera
// position when looking near the horizon. A small overshoot snaps back, a big one slides. Returns true if the camera
// moved.
bool ClampToMap(void* camera, int dt)
{
    if (!g_engaged || g_settings.allowOutsideMap || !g_haveGameBounds) {
        return false;
    }

    Vector3 pos = Pos(camera);
    Vector3 f = Forward(camera);
    Vector3 point = pos;
    if (f.y < kNearHorizontalY) {
        point = Add(pos, Mul(f, (g_lastGroundY - pos.y) / f.y));
    }

    float room = g_settings.edgeRoom;
    float dx = std::clamp(point.x, g_gameBounds.min.x - room, g_gameBounds.max.x + room) - point.x;
    float dz = std::clamp(point.z, g_gameBounds.min.z - room, g_gameBounds.max.z + room) - point.z;
    if (dx == 0 && dz == 0) {
        return false;
    }

    if (std::fabs(dx) > kSnapOvershoot || std::fabs(dz) > kSnapOvershoot) {
        float alpha = 1.0f - std::exp(-(dt / 1000.0f) / kSlideBackTau);
        dx *= alpha;
        dz *= alpha;
    }
    SetPos(camera, {pos.x + dx, pos.y, pos.z + dz});

    // so panning into the edge doesn't bounce
    Vector3& impulse = game::Camera_m_impulse(camera);
    if (impulse.x * dx < 0) {
        impulse.x = 0;
    }
    if (impulse.z * dz < 0) {
        impulse.z = 0;
    }
    return true;
}

} // namespace cam

bool Camera_InExtraCloseZoom(float cameraHeight)
{
    return g_engaged && g_haveGameBounds && cameraHeight < g_gameBounds.min.y;
}

float Camera_NeededFarPlane(float cameraHeight)
{
    if (!g_engaged || !g_haveGameBounds) {
        return 0;
    }

    // the game's far plane comes from the stock m_bounds.max.y
    float needed = std::max(cameraHeight - g_lastGroundY, 0.0f) * kFarPlaneHeightScale + kFarPlaneMargin;
    if (Active()) {
        float w = g_gameBounds.max.x - g_gameBounds.min.x;
        float d = g_gameBounds.max.z - g_gameBounds.min.z;
        bool usableSize = w > 0 && d > 0 && w < kUnlimited;
        float diagonal = usableSize ? std::sqrt(w * w + d * d) : kFallbackMapDiagonal;
        needed = std::max(needed, diagonal + std::max(cameraHeight, 0.0f) * kFarPlaneTiltedPerHeight);
    }
    return std::min(needed, kMaxFarPlane);
}

bool Camera_SwapInCollisionBounds(void* camera)
{
    if (!g_engaged || !g_haveWantedBounds || g_boundsSwapped) {
        return false;
    }
    void* client = game::GameClient();
    if (!client || camera != game::Camera(client)) {
        return false;
    }

    Bounds& bounds = game::Camera_m_bounds(camera);
    g_swappedOutBounds = bounds;
    bounds = g_wantedBounds;
    g_boundsSwapped = true;
    return true;
}

void Camera_RestoreCollisionBounds(void* camera)
{
    if (!g_boundsSwapped) {
        return;
    }

    game::Camera_m_bounds(camera) = g_swappedOutBounds;
    g_boundsSwapped = false;
}

// The game asks for the camera right above the point, which centers it only looking straight down. Tilted, the camera
// stands back along the view axis at the same height. Near the horizon the target stays, as ClampToMap does.
const Vector3* Camera_CenterMoveTarget(void* camera, const Vector3* pos)
{
    static Vector3 centered; // the original reads it after the callback returns

    void* client = game::GameClient();
    if (!g_engaged || !client || camera != game::Camera(client) || game::FreelookEnabled(client)) {
        return pos;
    }

    Vector3 f = Forward(camera);
    if (f.y >= kNearHorizontalY) {
        return pos;
    }

    float distance = std::max(pos->y - g_lastGroundY, 0.0f) / -f.y;
    centered = {pos->x - f.x * distance, pos->y, pos->z - f.z * distance};
    return &centered;
}
