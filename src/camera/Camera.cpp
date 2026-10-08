// Rotates, tilts and zooms the normal top-down mission camera, so orders still work as usual.
//
// The stock camera looks straight down. Camera::Update rebuilds the view matrix every frame from Camera::m_rotAngles,
// which is (pitch -90, yaw 0, roll 0). We write (-90 + tilt, yaw, 0) instead. Map picking (ConvertScreenToMapCoords)
// uses the same view matrix, so clicks still land under the cursor.
//
// Orbit. Changing the angles around the camera's own position would swing the view away. So while the angles change,
// the camera stays at pivot - forward * distance. The pivot is the ground point at the screen center, raycast when a
// rotation starts. After that everything is analytic (forward comes from the view matrix), so the camera can't jump
// when a strong tilt makes the center ray hit walls, far ground or nothing. Panning or zooming during an orbit moves
// the pivot with the camera. After a short idle the pivot is dropped, and the next rotation captures a new one.
//
// Look in place. The look modifier turns the camera without moving it, up to just short of the horizon, like the dev
// camera's mouse look.
//
// Input sets target angles, and the applied angles ease toward them (setting "smoothing").
//
// The toggle key animates between the top view, always at the zoom set by "topViewZoom", and the saved angled view at
// its saved spot and zoom (orbit pivot and distance). The switch to the top view glides to the trooper nearest the
// cursor, or keeps the screen center with "topViewScreenCenter". With "startInTopView", missions start in the top view.
//
// A replay rewind reloads the map. The view at the rewind comes back once the replay runs again.
//
// The rotate keys turn in fixed steps with "rotateStep" (XCOM style). The reset key glides back to north-up and keeps
// the tilt.
//
// WASD panning already follows the view, because the game takes its directions from m_matView. Edge-scroll is in world
// X/Z, so it's re-expressed along the same screen axes.
//
// Once engaged, the game's own tilt hotkeys (stock m_beautyAngles) become freecam tilt (see kStockTiltSign).
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <algorithm>
#include <cmath>
#include <cstring>

#include "Freecam.h"
#include "game/Game.h"
#include "settings/Settings.h"

namespace {

constexpr float kStockPitch = -90.0f;
constexpr float kMouseDegreesPerPixel = 0.25f;
constexpr DWORD kOrbitIdleMs = 500; // the pivot is kept this long after the angles stop changing
constexpr float kMinOrbitDistance = 1.0f;
constexpr float kSnapDegrees = 0.05f; // easing snaps to the target below these
constexpr float kSnapDistance = 0.01f;
constexpr float kPlayerImpulseEpsilon = 1e-3f; // m_impulse above this means the player is panning or zooming
constexpr float kMaxLookTilt = 89.0f; // looking in place stops just short of the horizon, so "down" stays well defined

// Raycasts from the screen center or the cursor. A hit farther than kRaycastRange camera heights is the sky or the
// far horizon. The height counts as at least kRaycastMinHeight, so a camera near the ground keeps a usable range.
constexpr float kRaycastRange = 12.0f;
constexpr float kRaycastMinHeight = 10.0f;
constexpr float kPivotMinAlignment = 0.95f; // cosine; a trusted pivot hit is within ~18 degrees of the view axis
constexpr float kPivotGroundMaxForwardY = -0.2f; // forward.y below this: the view ray meets the ground close enough
constexpr float kMinDownForZoomClamp = 0.1f; // view axis pointing down less than this: distance barely changes height
constexpr float kMinGlideTau = 0.1f; // seconds; the toggle's glide stays visible with smoothing 0
constexpr float kOnGridTolerance = 1e-3f; // in steps; a heading this close to a step multiple counts as on it

// --- angles: input sets the targets, the applied values ease toward them ---
// Degrees. Yaw is in (-180, 180]. Tilt is the angle away from straight down, in [0, maxTilt] when orbiting and up to
// kMaxLookTilt when looking in place.
float g_targetYaw = 0;
float g_targetTilt = 0;
float g_yaw = 0; // applied this frame
float g_tilt = 0;
float g_appliedYaw = 0; // applied last frame, to detect a change
float g_appliedTilt = 0;

// --- orbit ---
bool g_hasOrbit = false;
bool g_lookInPlace = false; // the current angle change turns the camera in place (look modifier) instead of orbiting
Vector3 g_pivot = {};
float g_distance = 0;

float g_targetDistance = 0;
bool g_animatingDistance = false; // toggle animation of the zoom
Vector3 g_targetPivot = {};
bool g_animatingPivot = false; // toggle animation of the pivot, to the chosen trooper
float g_animationTau = 0; // seconds; >0 while a toggle animation runs

DWORD g_lastOrientTick = 0;
Vector3 g_lastPos = {}; // camera position at the end of our last frame, to detect pans and zooms
float g_lastGroundY = 0; // height of the last good pivot, for when the center ray finds nothing

bool g_dragging = false;
POINT g_dragAnchor = {};
bool g_dragLook = false; // the drag is a look in place (look modifier), not an orbit

// The game never calls SetCursor, so Windows shows its window class cursor. While dragging, we swap that for a "move"
// cursor (or none, which hides it) and put the original back afterwards.
HWND g_cursorWindow = nullptr;
HCURSOR g_savedClassCursor = nullptr;

// Key edges, for the toggle and stepped rotation
bool g_toggleWasDown = false;
bool g_rotateLeftWasDown = false;
bool g_rotateRightWasDown = false;
bool g_resetWasDown = false;

bool g_toggleRequested = false; // applied in Camera_BeforeUpdate, where the camera is available
bool g_toggleAtCursor = false; // requested by the key (for the settings window's button the cursor is on the button)

// The toggle key flips between the top view and a remembered angled view. The angled view saves its angles, the ground
// point at the screen center and its zoom (orbit pivot and distance) when it's left, so panning in the top view doesn't
// move it. The top view needs nothing saved: it's north-up, tilted by "topDownTilt", at the zoom set by "topViewZoom".
// Rotating or tilting in the top view makes that view the angled view for the next toggle.
bool g_hasSavedView = false;
float g_savedYaw = 0;
float g_savedTilt = 0;
Vector3 g_savedPivot = {};
float g_savedDistance = 0;

// The toggle's top view is showing. The toggle sets it, and any rotate or tilt input clears it. It's needed because
// the top view is tilted ("topDownTilt"), so "targets all 0" can't identify it.
bool g_topDownLatched = false;

// Camera limits. The game sets m_bounds once per map load. The map is centered on the origin, X/Z are its extents and
// Y is the zoom height range. Camera::CollideWithBounds clamps the camera position to it. That fights a tilted camera,
// which has to stand outside the map to look at an edge. CollideWithBounds also shrinks the X/Z limits linearly with
// height, to zero at max.y, which pins a zoomed-out camera to the map center. So once engaged:
//  - The game's clamp only handles height. X/Z are unlimited, and max.y (where that area shrinks to zero) sits at
//    kApexScale times our own zoom-out limit (g_zoomCap).
//  - ClampToMap keeps the camera over the map. It keeps the point the camera looks at over the map, not the camera.
constexpr float kUnlimited = 9999.0f; // what Camera::SetDefaults uses for "no limits"
constexpr float kApexScale = 2.0f;
constexpr float kNearHorizontalY = -0.25f; // forward.y above this puts the look-at point too far away to clamp
constexpr float kSnapOvershoot = 2.0f; // beyond the edge by more than this, the camera slides back
constexpr float kSlideBackTau = 0.15f; // seconds

// Far plane while engaged (Camera_NeededFarPlane)
constexpr float kFarPlaneHeightScale = 1.25f; // times the height above ground, so the ground below stays in range
constexpr float kFarPlaneMargin = 50.0f;
constexpr float kFallbackMapDiagonal = 500.0f; // when the map bounds give no usable size
constexpr float kFarPlaneTiltedPerHeight = 4.0f; // tilted, the view also reaches this far per unit of camera height
constexpr float kMaxFarPlane = 20000.0f;

bool g_haveGameBounds = false;
Bounds g_gameBounds = {};
Bounds g_wantedBounds = {}; // what CollideWithBounds gets while engaged
bool g_haveWantedBounds = false;
Bounds g_swappedOutBounds = {}; // the game's m_bounds during a CollideWithBounds call
bool g_boundsSwapped = false;
float g_zoomCap = kUnlimited;
int g_lastDt = 16; // ms, from the last GameClient::UpdateCamera, for Camera_AfterUpdate

// Each mission starts dormant. The camera is exactly stock through troop placement, and m_bounds isn't touched. With
// "startInTopView" we take over in the top view as soon as the mission runs. Otherwise we stay dormant until the
// player uses a freecam control or pushes the zoom past the stock limits. Camera_OnMissionStart resets this.
bool g_engaged = false;
bool g_startTopViewPending = false; // "startInTopView": go to the top view once the mission runs
bool g_wasEngaged = false; // detects the frame we take over (TakeOverStockView)

// A replay rewind restarts the replay and fast-forwards, so it reloads the map. The view at the rewind is kept and put
// back once the replay runs again, as a forward skip (no reload) keeps it too.
struct KeptView {
    Vector3 pos;
    float yaw;
    float tilt;
    float targetYaw;
    float targetTilt;
    bool hasSavedView;
    float savedYaw;
    float savedTilt;
    Vector3 savedPivot;
    float savedDistance;
    bool topDownLatched;
    float lastGroundY;
};

KeptView g_keptView = {};
bool g_rewindKept = false; // a rewind kept the view, and the reload hasn't happened yet
bool g_restoreKeptView = false; // the reload happened: the kept view comes back once the replay runs

// The game's own tilt hotkeys don't touch m_rotAngles. GameInput::UpdateCameraControls moves m_beautyAngles.x
// (clamped to -20..0), which Camera::Update applies on top of the view while m_beautyMode is on. Camera::Update also
// adds a zoom-dependent part ("tilt when zooming"). Once engaged, we own the orientation. Beauty mode stays off and
// those hotkeys become freecam tilt, so no stock rotation is added on top of our angles.
constexpr float kStockTiltSign = 1.0f; // freecam tilt per degree of -m_beautyAngles.x
constexpr float kStockTiltBaseline = -10.0f; // mid-range, so keys in both directions survive the game's -20..0 clamp
constexpr float kStockZoomTiltHeight = 40.0f; // Camera::Update: zoom tilt fades out at this height...
constexpr float kStockZoomTiltMax = 7.0f; // ...and is this many degrees at m_minHeight
constexpr float kZoomLimitSlack = 0.5f; // "at the stock zoom limit" tolerance, in height units

// Whether this frame's Camera::Update applied the stock "tilt when zooming" rotation. UpdateCamera sets the flag for
// the next frame afterwards, so it's read before.
bool g_beautyThisFrame = false;

bool KeyDown(int vk)
{
    return (GetAsyncKeyState(vk) & 0x8000) != 0;
}

// The game's main window. IsGameFocused accepts any window of the process, so the foreground window may be another.
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

// Unit vector the camera looks along, from the view matrix UpdateViewMatrix builds. The matrix is row-major
// [R | -R*pos], so the rows of R are the camera axes in world space and the view axis is +-row 2. Tilt never reaches
// 90 degrees, so the camera always looks downward and the sign that points down is the right one.
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

// Unit X/Z vectors along the screen's right and up. Right is row 0 of the view matrix. Row 1's sign doesn't match
// screen-up, so up is right's X/Z perpendicular instead, turned the way the stock edge-scroll has it at yaw 0
// (right +X, up +Z). Yaw only rotates the screen around the vertical, so that pairing holds at every angle.
void ScreenAxes(void* camera, Vector3& right, Vector3& up)
{
    const float* m = &game::Camera_m_matView(camera);
    Vector3 rowRight = {m[0], 0, m[2]};
    float rightLen = Length(rowRight);
    right = rightLen < 1e-6f ? Vector3{1, 0, 0} : Mul(rowRight, 1.0f / rightLen);
    up = {-right.z, 0, right.x};
}

// Camera height above the last known ground, at least 1.
float HeightAboveGround(Vector3 pos)
{
    return std::max(pos.y - g_lastGroundY, 1.0f);
}

// A raycast hit farther away than this is the sky or the far horizon.
float MaxRaycastDistance(float height)
{
    return std::max(height, kRaycastMinHeight) * kRaycastRange;
}

// Orbiting is limited to maxTilt. If a look in place left the camera tilted beyond that, an orbit may only bring it
// down, so it doesn't snap back to maxTilt.
float OrbitTiltLimit(float tilt)
{
    return std::max(g_settings.maxTilt, std::min(tilt, kMaxLookTilt));
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

// Works out the limits Camera::CollideWithBounds uses. m_bounds itself is never written, because other parts of the
// game read it as the map rectangle. Our wider limits exist only during the CollideWithBounds call
// (Camera_SwapInCollisionBounds), so m_bounds always holds the game's own value.
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

    // height (zoom) is still clamped by the game, within our range
    float stockMin = g_gameBounds.min.y;
    float stockMax = g_gameBounds.max.y;
    wanted.min.y = stockMin * g_settings.zoomInFactor;
    if (stockMax > stockMin) {
        g_zoomCap = stockMax * g_settings.zoomOutFactor;
        wanted.max.y = g_zoomCap * kApexScale;
    } else {
        g_zoomCap = kUnlimited;
    }

    // sideways the game never clamps (see above), and ClampToMap keeps the view over the map
    wanted.min.x = wanted.min.z = -kUnlimited;
    wanted.max.x = wanted.max.z = kUnlimited;

    g_wantedBounds = wanted;
    g_haveWantedBounds = true;
}

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

// Where ClampToMap would stop the camera anyway, so it doesn't fight the end of a glide to this point.
void ClampPointToMap(Vector3* p)
{
    if (g_settings.allowOutsideMap || !g_haveGameBounds) {
        return;
    }

    float room = g_settings.edgeRoom;
    p->x = std::clamp(p->x, g_gameBounds.min.x - room, g_gameBounds.max.x + room);
    p->z = std::clamp(p->z, g_gameBounds.min.z - room, g_gameBounds.max.z + room);
}

// The ground point under the mouse cursor. Like CapturePivot's raycast, a hit beyond MaxRaycastDistance doesn't count.
// So a cursor over the sky or the far horizon gives nothing, and the top view uses the screen center instead.
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

// The top view centers on one of your troopers (unless "topViewScreenCenter"). It's the one nearest the reference
// point (the ground under the cursor), however many stand around it.
constexpr int kMaxTroopers = 64;

bool NearestTrooper(void* client, Vector3 reference, Vector3* out)
{
    Vector3 troopers[kMaxTroopers];
    int n = game::OwnTroopers(client, troopers, kMaxTroopers);
    if (n == 0) {
        game::api->Log("top view: no troopers found, centering on the screen center");
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

// The pivot is the point the screen center looks at, in the current view (the previous frame's). Called when an orbit
// starts. The raycast counts only if it hits roughly along the view axis at a sane distance. Otherwise the view ray is
// intersected with the last known ground height.
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

    // The view ray meets the last ground height. Looking near the horizon that point is too far away, so the pivot
    // goes two heights out instead.
    float dist = height * 2.0f;
    if (f.y < kPivotGroundMaxForwardY) {
        dist = (g_lastGroundY - pos.y) / f.y;
    }
    g_distance = std::clamp(dist, kMinOrbitDistance, maxDistance);
    g_pivot = Add(pos, Mul(f, g_distance));
    g_hasOrbit = true;
}

// The camera moved since our last frame (pan, zoom, bounds push). The orbit moves with it, split into a change of
// distance (along the view axis) and a shift of the pivot (the rest), so the camera stays exactly where it is.
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

// Sorts out camera movement since our last frame. Player pans and zooms (keys, wheel, edge-scroll) all go through
// m_impulse, which the toggle zeroes when it starts. Other moves during the toggle's animation (the game's height
// clamp in Camera::Update, ClampToMap, CapZoom) don't cancel it. They aren't the player, and the animation puts the
// camera back on its orbit anyway.
void TrackOrbit(void* camera)
{
    if (!g_hasOrbit) {
        return;
    }

    Vector3 impulse = game::Camera_m_impulse(camera);
    bool playerMoving = Dot(impulse, impulse) > kPlayerImpulseEpsilon * kPlayerImpulseEpsilon;
    if ((g_animatingDistance || g_animatingPivot) && playerMoving) {
        // the player zoomed or panned during the toggle animation, so the zoom is theirs now
        g_animatingDistance = false;
        g_animatingPivot = false;
        g_targetDistance = g_distance;
    }
    if (!g_animatingDistance && !g_animatingPivot) {
        FollowExternalMove(camera);
    }
}

// The orbit distance that puts the camera at a height within the current zoom range, for a view tilted by tiltDeg.
// During a glide to another pivot, the height counts above the pivot the glide ends at.
float ClampDistanceToZoomRange(float distance, float tiltDeg)
{
    float down = std::cos(tiltDeg * kDegToRad); // how much of the view axis points down
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

// The top view's orbit distance. "topViewZoom" picks a camera height within the stock zoom range, which is an
// absolute height like the stock zoom itself. The distance puts the camera there, above groundY, tilted by
// "topDownTilt" (at most 30 degrees, so the cosine is never small).
float TopViewDistance(float groundY)
{
    float stockMin = g_gameBounds.min.y;
    float stockMax = g_gameBounds.max.y;
    if (!g_haveGameBounds || stockMax <= stockMin) {
        return g_distance;
    }

    float height = stockMin + (stockMax - stockMin) * g_settings.topViewZoom;
    float down = std::cos(g_settings.topDownTilt * kDegToRad);
    return std::max((height - groundY) / down, kMinOrbitDistance);
}

float GlideTau()
{
    return std::max(g_settings.smoothing * 2.0f, kMinGlideTau);
}

// The toggle latches its top view (see g_topDownLatched). A flat view with nothing latched counts too, as at the
// start of a mission before anything is saved.
bool InTopDown()
{
    return g_topDownLatched || !TargetActive();
}

// Stepped rotation goes to the next multiple of the step in that direction, so an off-grid heading lands on the grid.
float StepYaw(float yaw, int direction)
{
    float step = g_settings.rotateStep;
    float k = 0;
    if (direction < 0) {
        k = std::ceil(yaw / step - kOnGridTolerance) - 1.0f;
    } else {
        k = std::floor(yaw / step + kOnGridTolerance) + 1.0f;
    }
    return WrapDegrees(k * step);
}

// Setting "cursorToCenterAfterDrag": the middle of the game window, where an orbit's pivot is.
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

// One step of a drag. The cursor's offset from the anchor becomes rotation and tilt, and the cursor goes back to the
// anchor, so the drag is unlimited and doesn't edge-scroll. Returns true if the mouse moved.
bool ApplyDragDelta(POINT cursor, bool look)
{
    bool reverse = look ? g_settings.reverseLookDrag : g_settings.reverseOrbitDrag;
    float sens = kMouseDegreesPerPixel * g_settings.mouseSensitivity * (reverse ? -1.0f : 1.0f);
    g_targetYaw += (cursor.x - g_dragAnchor.x) * sens;
    g_targetTilt += (cursor.y - g_dragAnchor.y) * sens;
    SetCursorPos(g_dragAnchor.x, g_dragAnchor.y);
    if (cursor.x == g_dragAnchor.x && cursor.y == g_dragAnchor.y) {
        return false;
    }

    g_lookInPlace = look;
    return true;
}

// Toggle key: top view <-> saved angled view. Shift + the key opens the settings window instead.
void ReadToggleKey(bool listening)
{
    bool toggleHeld = KeyDown(g_settings.toggleViewKey);
    if (listening && toggleHeld && !g_toggleWasDown) {
        if (KeyDown(VK_SHIFT)) {
            g_windowOpen = !g_windowOpen;
        } else {
            Camera_ToggleView();
            g_toggleAtCursor = true;
        }
    }
    g_toggleWasDown = toggleHeld;
}

// Modifier + mouse: orbit (rotate modifier) or look around in place (look modifier). The cursor stays pinned at the
// anchor, so the drag is unlimited and doesn't edge-scroll. Returns true if the drag steered.
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
        // The mouse moved since the last warp. That last bit becomes rotation too, and the cursor goes back to the
        // anchor (or the screen center). Skipped after an alt-tab, because the cursor belongs to the other window.
        if (game::api->IsGameFocused()) {
            POINT cursor;
            GetCursorPos(&cursor);
            steering |= ApplyDragDelta(cursor, g_dragLook);
            if (g_settings.cursorToCenterAfterDrag) {
                CursorToScreenCenter(); // the point the orbit turned around
            }
        }
        g_dragging = false;
        EndDragCursor();
    }
    return steering;
}

// Rotate keys turn while held, or one step per press (setting "rotateStep"). The reset key glides to north-up.
// Returns true if the keys steered. *glide is set if a step or the reset started a glide.
bool ReadRotateKeys(bool inputAllowed, int dt, bool* glide)
{
    bool keysAllowed = inputAllowed && !g_dragging;
    bool leftHeld = keysAllowed && KeyDown(g_settings.rotateLeftKey);
    bool rightHeld = keysAllowed && KeyDown(g_settings.rotateRightKey);
    bool resetHeld = keysAllowed && KeyDown(g_settings.resetHeadingKey);
    bool steering = false;
    bool glided = false;

    if (g_settings.rotateStep > 0) {
        if (leftHeld && !g_rotateLeftWasDown) {
            g_targetYaw = StepYaw(g_targetYaw, -1);
            glided = true;
        }
        if (rightHeld && !g_rotateRightWasDown) {
            g_targetYaw = StepYaw(g_targetYaw, 1);
            glided = true;
        }
    } else {
        float step = g_settings.keyRotateSpeed * dt / 1000.0f;
        if (leftHeld) {
            g_targetYaw -= step;
            steering = true;
        }
        if (rightHeld) {
            g_targetYaw += step;
            steering = true;
        }
    }
    if (resetHeld && !g_resetWasDown && g_targetYaw != 0) {
        g_targetYaw = 0;
        glided = true;
    }

    g_rotateLeftWasDown = leftHeld;
    g_rotateRightWasDown = rightHeld;
    g_resetWasDown = resetHeld;
    if (leftHeld || rightHeld || glided) {
        steering |= glided;
        g_lookInPlace = false;
    }

    *glide = glided;
    return steering;
}

void ReadInput(int dt)
{
    // Nothing reacts while a game menu (Esc menu etc.) is open. Keys still held when it closes don't fire either.
    bool listening = game::api->IsGameFocused() && !Ui_IsRebinding() && !Freecam_GameMenuOpen();
    bool inputAllowed = listening && !g_windowOpen;

    ReadToggleKey(listening);

    float tiltBefore = g_targetTilt;
    bool steering = ReadDrag(inputAllowed);
    bool glide = false;
    steering |= ReadRotateKeys(inputAllowed, dt, &glide);

    // steering by hand takes over from a running toggle animation
    if (steering) {
        g_animationTau = 0;
        g_animatingPivot = false;
        g_engaged = true;
        g_topDownLatched = false; // rotating out of the top view makes this the angled view
    }
    if (glide) {
        g_animationTau = GlideTau();
    }

    // Looking in place may go up to the horizon. Orbiting is limited by OrbitTiltLimit, from the tilt before this
    // frame's input.
    float tiltLimit = kMaxLookTilt;
    if (!g_lookInPlace) {
        tiltLimit = OrbitTiltLimit(tiltBefore);
    }
    g_targetYaw = WrapDegrees(g_targetYaw);
    g_targetTilt = std::clamp(g_targetTilt, 0.0f, tiltLimit);
}

// Starts the glide to the targets set by GoToTopView / ToggleView.
void StartViewGlide(void* camera)
{
    // a saved zoom may be outside the current zoom range (settings changed, other part of the map)
    g_targetDistance = ClampDistanceToZoomRange(g_targetDistance, g_targetTilt);

    g_animatingDistance = true;
    g_animationTau = GlideTau();
    game::Camera_m_impulse(camera) = {0, 0, 0}; // no leftover drift during the animation
}

// The point the top view centers on, unless "topViewScreenCenter". It's the trooper nearest the cursor. From the
// settings button, or with the cursor over the sky, it's the trooper nearest the screen center. Returns false to keep
// the screen center.
bool TopViewTarget(void* client, void* camera, bool atCursor, Vector3* out)
{
    if (g_settings.topViewScreenCenter) {
        return false;
    }

    Vector3 reference = g_pivot;
    bool cursorHit = atCursor && CursorOnMap(client, camera, &reference);
    if (!cursorHit) {
        reference = g_pivot; // CursorOnMap may have written a rejected hit
    }
    return NearestTrooper(client, reference, out);
}

// The top view is north-up, tilted by "topDownTilt", at the zoom set by "topViewZoom". It centers on the trooper
// nearest the cursor, or keeps the screen center with "topViewScreenCenter". Coming from another view saves that view
// for the toggle. Already in the top view, it only resets the zoom and the centering.
// atCursor is true for the key, because for the settings window's button the cursor is on the button.
void GoToTopView(void* client, void* camera, bool atCursor)
{
    g_lookInPlace = false; // the switch orbits around the screen center
    if (!g_hasOrbit) {
        CapturePivot(client, camera);
    }

    // A glide back to the angled view may still be running, so its destination is what's saved, like the angles.
    if (!InTopDown()) {
        g_savedYaw = g_targetYaw;
        g_savedTilt = g_targetTilt;
        g_savedPivot = g_animatingPivot ? g_targetPivot : g_pivot;
        g_savedDistance = g_animatingDistance ? g_targetDistance : g_distance;
        g_hasSavedView = true;
    }
    g_animatingPivot = false; // "topViewScreenCenter" keeps the current spot, not a running glide's destination
    g_topDownLatched = true;
    g_targetYaw = 0;
    g_targetTilt = g_settings.topDownTilt;

    // The orbit keeps the camera at pivot - forward * distance. So moving the pivot to the chosen point while the
    // angles ease ends with that point at the screen center, without a jump.
    float groundY = g_pivot.y;
    Vector3 hit;
    if (TopViewTarget(client, camera, atCursor, &hit)) {
        g_targetPivot = hit;
        g_animatingPivot = true;
        g_lastGroundY = hit.y;
        groundY = hit.y;
    }
    g_targetDistance = TopViewDistance(groundY);

    // Setting "cursorToCenterOnTopView" moves the cursor to the window center, where the view ends up centered. It
    // applies only to the key. From the settings button the cursor stays on the window, and a mission start has no
    // key press.
    if (g_settings.cursorToCenterOnTopView && atCursor && game::api->IsGameFocused()) {
        CursorToScreenCenter();
    }
    StartViewGlide(camera);
}

// Flips the targets between the top view and the saved angled view. The angled view glides back to its own spot,
// wherever the top view was panned to. With no angled view saved yet (e.g. right after a mission started in the top
// view), it goes to the top view again, which resets its zoom and centering.
void ToggleView(void* client, void* camera)
{
    if (!InTopDown() || !g_hasSavedView) {
        GoToTopView(client, camera, g_toggleAtCursor);
        return;
    }

    g_lookInPlace = false; // the switch orbits around the screen center
    if (!g_hasOrbit) {
        CapturePivot(client, camera);
    }

    g_targetYaw = g_savedYaw;
    g_targetTilt = std::min(g_savedTilt, kMaxLookTilt);
    g_targetDistance = g_savedDistance;
    g_targetPivot = g_savedPivot;
    g_animatingPivot = true;
    g_lastGroundY = g_savedPivot.y;
    g_topDownLatched = false;
    StartViewGlide(camera);
}

// Moves the applied angles (and a toggle's zoom and pivot) toward their targets.
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

} // namespace

void Camera_ToggleView()
{
    g_toggleRequested = true;
    g_toggleAtCursor = false;
    g_engaged = true;
}

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
    g_toggleRequested = false;

    g_hasSavedView = false;
    g_topDownLatched = false;
    g_startTopViewPending = g_settings.startInTopView;

    if (g_dragging) {
        g_dragging = false;
        EndDragCursor();
    }

    // The new map's limits are set right after this. Until ApplyBounds runs (not during troop placement), no widened
    // limits from the last map reach CollideWithBounds.
    g_haveGameBounds = false;
    g_haveWantedBounds = false;
    g_zoomCap = kUnlimited;
    g_engaged = false;
    g_wasEngaged = false;
}

// Only here, not in Camera_OnMissionStart, does a kept view become due. Troop placement and the editor reset through
// Camera_OnMissionStart too, and a map load that isn't the rewind's drops any older kept view.
void Camera_OnMapLoaded()
{
    bool rewound = g_rewindKept;
    g_rewindKept = false;
    Camera_OnMissionStart();

    g_restoreKeptView = rewound;
    if (rewound) {
        g_startTopViewPending = false; // the kept view, not the top view
    }
}

// Dormant, the camera is stock, and the stock camera resets on a rewind too.
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
    k.hasSavedView = g_hasSavedView;
    k.savedYaw = g_savedYaw;
    k.savedTilt = g_savedTilt;
    k.savedPivot = g_savedPivot;
    k.savedDistance = g_savedDistance;
    k.topDownLatched = g_topDownLatched;
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

bool Camera_InExtraCloseZoom(float cameraHeight)
{
    return g_engaged && g_haveGameBounds && cameraHeight < g_gameBounds.min.y;
}

float Camera_GroundY()
{
    return g_lastGroundY;
}

float Camera_NeededFarPlane(float cameraHeight)
{
    if (!g_engaged || !g_haveGameBounds) {
        return 0;
    }

    // The game's own far plane assumes the stock zoom range, because it's derived from m_bounds.max.y, which stays
    // stock. Zoomed out beyond that, the ground below must still be in range.
    float needed = std::max(cameraHeight - g_lastGroundY, 0.0f) * kFarPlaneHeightScale + kFarPlaneMargin;
    if (Active()) {
        // tilted, the view reaches across the map
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

// Clicking a portrait, cycling the selection and scripted focus points all ask for the camera at (point.x, current
// height, point.z), right above the point. That centers it only when looking straight down. Tilted, the camera stands
// back along the view axis instead, at the same height, so the screen center looks at the point on the ground. Near
// the horizon ClampToMap keeps the camera position on the map, not the look-at point, so the target stays as asked.
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

namespace {

// The stock tilt currently shown (hotkey tilt + zoom tilt), in freecam tilt degrees; 0 if beauty mode is off.
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

// Taking over from the stock camera starts from the angle it was showing (its own m_rotAngles plus the stock tilt
// layered on top), so the view doesn't jump.
void TakeOverStockView(void* camera)
{
    Vector3 rot = game::Camera_m_rotAngles(camera);
    float tilt = std::clamp(rot.x - kStockPitch + StockTilt(camera), 0.0f, kMaxLookTilt);
    game::Camera_m_beautyAngles(camera) = {0, 0, 0};
    float yaw = WrapDegrees(rot.y);

    // While dormant the targets are 0, so what they hold now is this frame's input (a drag, a rotate step). It's kept
    // on top of the stock view, so the first press isn't lost.
    float yawInput = g_targetYaw;
    float tiltInput = g_targetTilt;
    g_tilt = g_appliedTilt = tilt;
    g_yaw = g_appliedYaw = yaw;
    g_targetTilt = std::clamp(tilt + tiltInput, 0.0f, kMaxLookTilt);
    g_targetYaw = WrapDegrees(yaw + yawInput);
}

// The player zooms against a stock zoom limit.
bool PushingStockZoomLimit(void* camera)
{
    float height = Pos(camera).y;
    float push = game::Camera_m_impulse(camera).y;
    bool pushingIn = push < 0 && height <= g_gameBounds.min.y + kZoomLimitSlack;
    bool pushingOut = push > 0 && height >= g_gameBounds.max.y - kZoomLimitSlack;
    return pushingIn || pushingOut;
}

// Puts back the view kept at a replay rewind. It's engaged as it was, so TakeOverStockView doesn't replace the angles
// with the stock view's.
void RestoreKeptView(void* camera)
{
    const KeptView& k = g_keptView;
    g_yaw = g_appliedYaw = k.yaw;
    g_tilt = g_appliedTilt = k.tilt;
    g_targetYaw = k.targetYaw;
    g_targetTilt = k.targetTilt;
    g_hasSavedView = k.hasSavedView;
    g_savedYaw = k.savedYaw;
    g_savedTilt = k.savedTilt;
    g_savedPivot = k.savedPivot;
    g_savedDistance = k.savedDistance;
    g_topDownLatched = k.topDownLatched;
    g_lastGroundY = k.lastGroundY;

    SetPos(camera, k.pos);
    game::Camera_m_impulse(camera) = {0, 0, 0};
    game::Camera_m_beautyAngles(camera) = {0, 0, 0};

    g_restoreKeptView = false;
    g_startTopViewPending = false;
    g_engaged = true;
    g_wasEngaged = true;
}

// Engages the mod when the zoom pushes a stock limit or "startInTopView" is due, and takes over the stock view on the
// frame it engages. Returns true if this frame starts the mission's top view.
bool UpdateEngaged(void* camera)
{
    // after a replay rewind, once the replay runs again (GameClient::m_state), like "startInTopView"
    if (g_restoreKeptView && game::MissionRunning()) {
        RestoreKeptView(camera);
    }

    // Zooming against the stock limit engages the mod, so the wider zoom range is reachable without rotating.
    if (!g_engaged && g_haveGameBounds && PushingStockZoomLimit(camera)) {
        g_engaged = true;
    }

    // "startInTopView" takes over once the mission runs (GameClient::m_state), not during the loading frames.
    bool startTopView = g_startTopViewPending && game::MissionRunning();
    if (startTopView) {
        g_startTopViewPending = false;
        g_engaged = true;
    }

    if (g_engaged && !g_wasEngaged) {
        TakeOverStockView(camera);
    }
    g_wasEngaged = g_engaged;
    return startTopView;
}

// Applies a requested toggle or the mission's start in the top view, and the top view's live tilt setting.
void ApplyViewRequests(void* client, void* camera, bool startTopView)
{
    if (g_toggleRequested) {
        g_toggleRequested = false;
        ToggleView(client, camera);
    }
    if (startTopView) {
        GoToTopView(client, camera, false); // the trooper nearest the screen center
        g_hasSavedView = false; // the stock start view isn't an angled view to toggle back to
    }

    // The top view follows its setting live. Any manual input has already cleared the latch.
    if (g_topDownLatched && g_targetYaw == 0) {
        g_targetTilt = g_settings.topDownTilt;
    }
}

// Captures the pivot when an orbit starts and drops it kOrbitIdleMs after the view stops changing. Returns true while
// the angles, or a toggle's zoom or pivot, are changing.
bool UpdateOrbitPivot(void* client, void* camera)
{
    bool orienting = g_yaw != g_appliedYaw || g_tilt != g_appliedTilt || g_animatingDistance || g_animatingPivot;
    DWORD now = GetTickCount();
    if (orienting) {
        if (g_lookInPlace) {
            g_hasOrbit = false; // turning in place keeps the camera put, and the next orbit finds a fresh pivot
        } else if (!g_hasOrbit) {
            CapturePivot(client, camera); // with the view as it was, which holds the point to keep centered
        }
        g_lastOrientTick = now;
    } else if (g_hasOrbit && now - g_lastOrientTick > kOrbitIdleMs) {
        g_hasOrbit = false; // the next rotation captures a new pivot, where the player has panned to
    }
    return orienting;
}

// Writes the eased angles and keeps the stock tilt off while engaged.
void WriteAngles(void* camera)
{
    // While dormant nothing is written, so the camera is entirely the game's.
    if (g_engaged) {
        game::Camera_m_rotAngles(camera) = {kStockPitch + g_tilt, g_yaw, 0.0f};
    }
    g_appliedYaw = g_yaw;
    g_appliedTilt = g_tilt;

    // The stock tilt (hotkeys + "tilt when zooming") adds its own rotation after UpdateViewMatrix. It's off while
    // we're engaged, so the view is exactly what UpdateViewMatrix builds and the top view really stays top-down.
    if (g_engaged) {
        game::Camera_m_beautyMode(camera) = false;
    }
    g_beautyThisFrame = game::Camera_m_beautyMode(camera);
}

} // namespace

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
    if (delta == 0) {
        return;
    }

    // a stock tilt key tilts the freecam camera by orbiting, like Alt + mouse
    float tiltLimit = OrbitTiltLimit(g_targetTilt);
    g_targetTilt = std::clamp(g_targetTilt - delta * kStockTiltSign, 0.0f, tiltLimit);
    g_lookInPlace = false;
    g_animationTau = 0;
    g_topDownLatched = false; // tilting out of the top view, as with the mouse
}

void Camera_BeforeUpdate(void* client, int dt)
{
    if (game::FreelookEnabled(client)) {
        g_hasOrbit = false;
        return; // the dev menu's FPS camera owns m_rotAngles
    }

    // Troop placement uses the stock camera, so nothing reacts until the mission itself starts. The editor has its own
    // camera, which the game copies over this one after this hook, and it loads maps without a MAP_LOADED. So in the
    // editor, freecam drops whatever a mission left engaged and ignores its keys (its Ctrl+Z / Ctrl+C, Alt + mouse).
    void* camera = game::Camera(client);
    bool editing = game::Editing();
    if (game::Deploying() || editing) {
        if (g_engaged && !editing) {
            game::Camera_m_rotAngles(camera) = {kStockPitch, 0.0f, 0.0f};
        }
        if (g_engaged || g_dragging) {
            Camera_OnMissionStart(); // also ends a drag
        }
        return;
    }

    g_lastDt = dt;
    ReadInput(dt);

    bool startTopView = UpdateEngaged(camera);
    ApplyBounds(camera);

    TrackOrbit(camera);
    ApplyViewRequests(client, camera, startTopView);

    Ease(dt);
    bool orienting = UpdateOrbitPivot(client, camera);
    WriteAngles(camera);

    if (orienting && g_hasOrbit) {
        game::Camera_UpdateViewMatrix(camera); // the new rotation, to read the new view axis from
        SetPos(camera, Sub(g_pivot, Mul(Forward(camera), g_distance)));
    }
    g_lastPos = Pos(camera);
}

namespace {

// Our zoom-out limit (see kApexScale). Returns true if the camera was moved.
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

// Keeps the point the camera looks at over the map, plus edge room. That point is the screen-center ground point,
// computed from the view axis and the last known ground height. It uses no raycast, so it's steady. Looking toward
// the horizon puts that point far away, so the camera position is kept inside instead.
// A small overshoot (panning into the edge) is corrected at once. A big one slides back. Big overshoots happen when
// "allowOutsideMap" is turned off or "edgeRoom" is lowered in the settings, and after a toggle animation.
// Returns true if the camera was moved.
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

    // stop momentum pushing further out, so panning into the edge doesn't bounce
    Vector3& impulse = game::Camera_m_impulse(camera);
    if (impulse.x * dx < 0) {
        impulse.x = 0;
    }
    if (impulse.z * dz < 0) {
        impulse.z = 0;
    }
    return true;
}

} // namespace

void Camera_AfterUpdate(void* client)
{
    if (game::FreelookEnabled(client)) {
        return;
    }
    void* camera = game::Camera(client);

    bool moved = CapZoom(camera);
    moved |= ClampToMap(camera, g_lastDt);

    // Rebuild this frame's view if we moved the camera. Skipped while the stock "tilt when zooming" rotation is on,
    // because it's applied inside Camera::Update and a rebuild would drop it. The correction then shows next frame.
    if (moved && !g_beautyThisFrame) {
        game::Camera_UpdateViewMatrix(camera);
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

    // the game's edge-scroll is +X toward the right edge and +Z toward the top edge, the screen axes at yaw 0
    Vector3 right;
    Vector3 up;
    ScreenAxes(camera, right, up);
    impulse.x = impulseBefore.x + dx * right.x + dz * up.x;
    impulse.z = impulseBefore.z + dx * right.z + dz * up.z;
}
