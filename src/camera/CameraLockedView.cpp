// The locked / unlocked view toggle. The locked view faces lockedHeading, tilted by lockedTilt, at lockedZoom, centered
// on the trooper nearest the cursor. Rotating by key or mouse ends it; zooming, the stock tilt keys, the reset key and
// the wheel adjust it and save the setting.
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

bool g_toggleRequested = false;
bool g_lockedViewRequested = false;
bool g_toggleAtCursor = false; // false for the settings button, where the cursor is on the button

// a hit beyond MaxRaycastDistance is sky or far horizon
bool CursorOnMap(void* client, void* camera, Vector3* out)
{
    HWND window = GameWindow();
    POINT cursor;
    if (!window || !GetCursorPos(&cursor) || !ScreenToClient(window, &cursor)) {
        return false;
    }

    game::GameClient_ConvertScreenToMapCoords(client, out, static_cast<float>(cursor.x), static_cast<float>(cursor.y));
    if (!std::isfinite(out->x) || !std::isfinite(out->y) || !std::isfinite(out->z)) {
        return false;
    }

    Vector3 pos = Pos(camera);
    float height = HeightAboveGround(pos);
    if (Length(Sub(*out, pos)) > MaxRaycastDistance(height)) {
        return false;
    }

    ClampPointToMap(out);
    return true;
}

constexpr int kMaxTroopers = 64;

bool NearestTrooper(void* client, Vector3 reference, Vector3* out)
{
    Vector3 troopers[kMaxTroopers];
    int n = game::OwnTroopers(client, troopers, kMaxTroopers);
    if (n == 0) {
        game::api->Log("locked view: no troopers found, centering on the screen center");
        return false;
    }

    int best = 0;
    float bestDist = 0;
    for (int i = 0; i < n; ++i) {
        float dx = troopers[i].x - reference.x, dz = troopers[i].z - reference.z;
        float d = dx * dx + dz * dz;
        if (i == 0 || d < bestDist) {
            best = i;
            bestDist = d;
        }
    }

    *out = troopers[best];
    ClampPointToMap(out);
    return true;
}

// lockedZoom is an absolute height within the stock zoom range. lockedTilt is at most 30 degrees, so the cosine is
// never small.
float LockedViewDistance(float groundY)
{
    const Bounds* stock = GameBounds();
    if (!stock || stock->max.y <= stock->min.y) {
        return g_distance;
    }

    float height = stock->min.y + (stock->max.y - stock->min.y) * g_settings.lockedZoom;
    float down = std::cos(g_settings.lockedTilt * kDegToRad);
    return std::max((height - groundY) / down, kMinOrbitDistance);
}

bool InLockedView()
{
    return g_lockedLatched || (!TargetActive() && !g_leftLockedView);
}

void StartViewGlide(void* camera)
{
    // a saved zoom may be outside the current range (settings changed, other part of the map)
    g_targetDistance = ClampDistanceToZoomRange(g_targetDistance, g_targetTilt);

    g_animatingDistance = true;
    g_animationTau = GlideTau();
    game::Camera_m_impulse(camera) = {0, 0, 0}; // no leftover drift
}

// The trooper nearest the cursor, or nearest the screen center without a cursor hit. Returns false to keep the screen
// center.
bool LockedViewTarget(void* client, void* camera, bool atCursor, Vector3* out)
{
    if (g_settings.lockedScreenCenter) {
        return false;
    }

    Vector3 reference = g_pivot;
    bool cursorHit = atCursor && CursorOnMap(client, camera, &reference);
    if (!cursorHit) {
        reference = g_pivot; // CursorOnMap may have written a rejected hit
    }
    return NearestTrooper(client, reference, out);
}

// Coming from another view saves it for the toggle. Already in the locked view, it resets the zoom and centering.
void GoToLockedView(void* client, void* camera, bool atCursor)
{
    g_lookInPlace = false;
    if (!g_hasOrbit) {
        CapturePivot(client, camera);
    }

    // a running glide's destination is what's saved, like the angles
    if (!InLockedView()) {
        g_savedYaw = g_targetYaw;
        g_savedTilt = g_targetTilt;
        g_savedPivot = g_animatingPivot ? g_targetPivot : g_pivot;
        g_savedDistance = g_animatingDistance ? g_targetDistance : g_distance;
        g_hasUnlockedView = true;
    }
    g_animatingPivot = false; // lockedScreenCenter keeps the current spot
    g_lockedLatched = true;
    g_targetYaw = g_settings.lockedHeading;
    g_targetTilt = g_settings.lockedTilt;

    // gliding the pivot ends with the point at the screen center
    float groundY = g_pivot.y;
    Vector3 hit;
    if (LockedViewTarget(client, camera, atCursor, &hit)) {
        g_targetPivot = hit;
        g_animatingPivot = true;
        g_lastGroundY = hit.y;
        groundY = hit.y;
    }
    g_targetDistance = LockedViewDistance(groundY);

    // only for the key
    if (g_settings.cursorToCenterOnLock && atCursor && game::api->IsGameFocused()) {
        CursorToScreenCenter();
    }
    StartViewGlide(camera);
}

// With no unlocked view saved yet, it goes to the locked view again.
void ToggleView(void* client, void* camera)
{
    if (!InLockedView() || !g_hasUnlockedView) {
        GoToLockedView(client, camera, g_toggleAtCursor);
        return;
    }

    g_lookInPlace = false;
    if (!g_hasOrbit) {
        CapturePivot(client, camera);
    }

    g_targetYaw = g_savedYaw;
    g_targetTilt = std::min(g_savedTilt, kMaxLookTilt);
    g_targetDistance = g_savedDistance;
    g_targetPivot = g_savedPivot;
    g_animatingPivot = true;
    g_lastGroundY = g_savedPivot.y;
    LeaveLockedView(); // even if it's flat
    StartViewGlide(camera);
}

// the inverse of LockedViewDistance's height
void SaveLockedZoom(void* camera)
{
    const Bounds* stock = GameBounds();
    if (!stock || stock->max.y <= stock->min.y) {
        return;
    }

    float zoom = (Pos(camera).y - stock->min.y) / (stock->max.y - stock->min.y);
    zoom = std::clamp(zoom, kLockedZoomRange.min, kLockedZoomRange.max);
    if (zoom != g_settings.lockedZoom) {
        g_settings.lockedZoom = zoom;
        Ui_SettingsChanged();
    }
}

} // namespace

namespace cam {

void RequestToggle(bool atCursor)
{
    g_toggleRequested = true;
    g_toggleAtCursor = atCursor;
    g_engaged = true;
}

void ResetViewRequests()
{
    g_toggleRequested = false;
    g_lockedViewRequested = false;
}

void LeaveLockedView()
{
    g_lockedLatched = false;
    g_leftLockedView = true;
    g_lockedZoomChanged = false;
}

// The reset key and the wheel turn the locked view itself and save lockedHeading.
void KeepLockedHeading()
{
    float heading = std::clamp(WrapDegrees(g_targetYaw), kLockedHeadingRange.min, kLockedHeadingRange.max);
    if (heading != g_settings.lockedHeading) {
        g_settings.lockedHeading = heading;
        Ui_SettingsChanged();
    }
}

// beautyDelta is as for TiltLikeStockKeys
void AdjustLockedTilt(float beautyDelta)
{
    float tilt = g_settings.lockedTilt - beautyDelta * kStockTiltSign;
    tilt = std::clamp(tilt, kLockedTiltRange.min, kLockedTiltRange.max);
    g_animationTau = 0;
    if (tilt != g_settings.lockedTilt) {
        g_settings.lockedTilt = tilt;
        Ui_SettingsChanged();
    }
}

// The zoom saves once it settles. Player zoom goes through m_impulse.y, which a glide zeroes.
void TrackLockedZoom(void* camera)
{
    if (!g_lockedLatched) {
        return;
    }

    bool zooming = std::fabs(game::Camera_m_impulse(camera).y) > kPlayerImpulseEpsilon;
    bool gliding = g_animatingDistance || g_animatingPivot;
    if (zooming) {
        g_lockedZoomChanged = true;
    } else if (g_lockedZoomChanged && !gliding) {
        g_lockedZoomChanged = false;
        SaveLockedZoom(camera);
    }
}

void ApplyViewRequests(void* client, void* camera, bool startLocked)
{
    if (g_toggleRequested) {
        g_toggleRequested = false;
        ToggleView(client, camera);
    }
    if (g_lockedViewRequested) {
        g_lockedViewRequested = false;
        GoToLockedView(client, camera, false);
    }
    if (startLocked) {
        GoToLockedView(client, camera, false);
        g_hasUnlockedView = false; // the stock start view isn't one to toggle back to
    }

    // the locked view follows its settings live
    if (g_lockedLatched) {
        g_targetYaw = g_settings.lockedHeading;
        g_targetTilt = g_settings.lockedTilt;
    }
}

} // namespace cam

void Camera_ToggleView()
{
    RequestToggle(false);
}

void Camera_GoToLockedView()
{
    g_lockedViewRequested = true;
    g_engaged = true;
}

bool Camera_LockedViewShown()
{
    return g_engaged && g_lockedLatched;
}
